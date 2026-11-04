/*************************************************************************\
* Copyright (c) 2026 ITER Organization.
* This module is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
\*************************************************************************/

/*
 *  Author: Ralph Lange <ralph.lange@gmx.de>
 */

/*
 * Benchmark for dissecting incoming structured data into the data element tree
 * (open62541 client library). Counterpart of the UA SDK benchmark with the same
 * name and parameters.
 *
 * A single item receives a nested structure, with every leaf element connected
 * to its own (fake) record. The structure types (one per level, members m0, m1,
 * ...; innermost members are doubles) are described as open62541 custom data
 * types, like the ones the client creates from the server's type dictionary.
 *
 * With open62541, the client library decodes the ExtensionObject into native
 * memory while decoding the service response; the device support only maps the
 * members. For a fair comparison with the UA SDK (where decoding is part of the
 * dissection), every update is measured as
 *   - decoding the binary encoded value (UA_decodeBinary, custom types)
 *   - dissection: ItemOpen62541::setIncomingData()
 * The leaf queues are drained between updates (outside the timed region).
 *
 * Limits of open62541 1.3 type descriptions: max. 255 members per structure
 * and max. 64 KiB in-memory size (UA_DataType::memSize is 16 bits) - i.e. a
 * structure with mandatory members can hold at most 8191 doubles.
 * The default target is therefore 8000 leafs.
 *
 * Environment variables to change the parameters:
 *   DEVOPCUA_BENCH_LEAFS       target number of leaf elements        (8000)
 *   DEVOPCUA_BENCH_DEPTH       number of nested structure levels     (5)
 *   DEVOPCUA_BENCH_FANOUT      fixed number of members per level, overrides
 *                              DEVOPCUA_BENCH_LEAFS (fan-out^depth leafs)
 *   DEVOPCUA_BENCH_ITERATIONS  number of steady state updates        (10)
 */

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <list>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <open62541/types.h>
#include <open62541/types_generated.h>

#include <callback.h>
#include <dbCommon.h>
#include <epicsEvent.h>
#include <epicsThread.h>
#include <menuPriority.h>

#include "DataElement.h"
#include "ItemOpen62541.h"
#include "RecordConnector.h"
#include "Session.h"
#include "SessionOpen62541.h"
#include "devOpcua.h"

#if UA_OPEN62541_VER_MAJOR == 1 && UA_OPEN62541_VER_MINOR < 3

// open62541 before 1.3 describes member types by index (not pointer)
// and has no UA_DecodeBinaryOptions
TEST(StructureDissectBenchmark, NestedStructure)
{
    GTEST_SKIP() << "the open62541 benchmark needs open62541 >= 1.3";
}

#else

namespace {

using namespace DevOpcua;
using Clock = std::chrono::steady_clock;

unsigned int
envOrDefault(const char *name, unsigned int dflt)
{
    const char *val = std::getenv(name);
    if (val && *val) {
        long v = std::strtol(val, nullptr, 10);
        if (v > 0)
            return static_cast<unsigned int>(v);
    }
    return dflt;
}

double
msSince(const Clock::time_point &start)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

std::string
fieldName(unsigned int i)
{
    return "m" + std::to_string(i);
}

// Fan-out per level that gets as close as possible to the target number of leafs
std::vector<unsigned int>
spreadFanout(unsigned int target, unsigned int depth, unsigned int fixed)
{
    std::vector<unsigned int> result;
    double remaining = target;
    for (unsigned int l = 0; l < depth; l++) {
        unsigned int f = fixed;
        if (!f) {
            f = static_cast<unsigned int>(std::lround(std::pow(remaining, 1.0 / (depth - l))));
            if (f < 1)
                f = 1;
        }
        result.push_back(f);
        remaining /= f;
    }
    return result;
}

// Callback that signals an event - used to wait for the callback queue to drain
struct CallbackFence
{
    epicsCallback cb;
    epicsEventId done;

    CallbackFence()
        : done(epicsEventMustCreate(epicsEventEmpty))
    {
        callbackSetCallback(&CallbackFence::run, &cb);
        callbackSetUser(this, &cb);
        callbackSetPriority(priorityLow, &cb);
    }
    ~CallbackFence() { epicsEventDestroy(done); }

    static void
    run(epicsCallback *pcb)
    {
        void *usr;
        callbackGetUser(usr, pcb);
        epicsEventSignal(static_cast<CallbackFence *>(usr)->done);
    }

    // Queued after all record processing requests (same priority, FIFO)
    void
    wait()
    {
        callbackRequest(&cb);
        epicsEventMustWait(done);
    }
};

struct Stats
{
    double mean = 0.0, median = 0.0, min = 0.0, max = 0.0;
};

Stats
statsOf(std::vector<double> v)
{
    Stats s;
    if (v.empty())
        return s;
    double sum = 0.0;
    for (double t : v)
        sum += t;
    s.mean = sum / v.size();
    std::sort(v.begin(), v.end());
    s.median = v[v.size() / 2];
    s.min = v.front();
    s.max = v.back();
    return s;
}

class StructureDissectBenchmark : public ::testing::Test
{
protected:
    void
    SetUp() override
    {
        epicsThreadOnce(&Session::onceId, &Session::initOnce, nullptr);

        unsigned int target = envOrDefault("DEVOPCUA_BENCH_LEAFS", 8000);
        unsigned int fixed = envOrDefault("DEVOPCUA_BENCH_FANOUT", 0);
        depth = envOrDefault("DEVOPCUA_BENCH_DEPTH", 5);
        iterations = envOrDefault("DEVOPCUA_BENCH_ITERATIONS", 10);
        fanout = spreadFanout(target, depth, fixed);
        noOfLeafs = 1;
        noOfNodes = 0;
        for (unsigned int l = 0; l < depth; l++) {
            noOfNodes += noOfLeafs;
            noOfLeafs *= fanout[l];
        }
        UA_ByteString_init(&encoded);
    }

    void
    TearDown() override
    {
        fence.reset();
        if (callbacksRunning) {
            callbackStop();
            callbackCleanup();
        }
        UA_ByteString_clear(&encoded);
        // The tree, item and session are intentionally not torn down (process exit cleans up)
    }

    // Custom data types: level 0 (outermost) ... depth-1 (members are doubles)
    // Returns false if the structure does not fit into open62541's type description
    bool
    createTypes()
    {
        types.resize(depth);
        members.resize(depth);
        typeNames.resize(depth);
        memberNames.resize(depth);
        for (int l = static_cast<int>(depth) - 1; l >= 0; l--) {
            const UA_DataType *memberType = (l == static_cast<int>(depth) - 1) ? &UA_TYPES[UA_TYPES_DOUBLE]
                                                                              : &types[l + 1];
            size_t memSize = static_cast<size_t>(fanout[l]) * memberType->memSize;
            if (fanout[l] > 255 || memSize > 0xffff) {
                std::cout << "Structure level " << l << " (" << fanout[l] << " members, " << memSize
                          << " bytes) exceeds the open62541 limits (255 members, 65535 bytes)" << std::endl;
                return false;
            }
            typeNames[l] = "BenchLevel" + std::to_string(l);
            memberNames[l].resize(fanout[l]);
            members[l].resize(fanout[l]);
            for (unsigned int i = 0; i < fanout[l]; i++) {
                memberNames[l][i] = fieldName(i);
                UA_DataTypeMember &m = members[l][i];
                std::memset(&m, 0, sizeof(m));
                m.memberName = memberNames[l][i].c_str();
                m.memberType = memberType;
                m.padding = 0; // doubles and structures of doubles: no padding
            }
            UA_DataType &t = types[l];
            std::memset(&t, 0, sizeof(t));
            t.typeName = typeNames[l].c_str();
            t.typeId = UA_NODEID_NUMERIC(1, 5000 + l);
            t.binaryEncodingId = UA_NODEID_NUMERIC(1, 6000 + l);
            t.memSize = static_cast<UA_UInt32>(memSize);
            t.typeKind = UA_DATATYPEKIND_STRUCTURE;
            t.pointerFree = true;
            t.overlayable = false;
            t.membersSize = static_cast<UA_UInt32>(fanout[l]);
            t.members = members[l].data();
        }
        typeArray.reset(new UA_DataTypeArray{nullptr, types.size(), types.data()});
        return true;
    }

    // Binary encoded value (Variant with ExtensionObject); leaf values are their member index.
    // As all levels are stored in place, the native value is a plain array of doubles.
    void
    createValue()
    {
        double *data = static_cast<double *>(UA_new(&types[0]));
        for (unsigned int n = 0; n < noOfLeafs; n++)
            data[n] = static_cast<double>(n % fanout[depth - 1]);
        UA_Variant value;
        UA_Variant_init(&value);
        UA_Variant_setScalar(&value, data, &types[0]);
        ASSERT_EQ(UA_STATUSCODE_GOOD, UA_encodeBinary(&value, &UA_TYPES[UA_TYPES_VARIANT], &encoded));
        UA_Variant_clear(&value);
    }

    // Decode the incoming value, as the client library does for the service response
    void
    decode(UA_DataValue &dv)
    {
        UA_DataValue_init(&dv);
        UA_DecodeBinaryOptions options;
        std::memset(&options, 0, sizeof(options));
        options.customTypes = typeArray.get();
        UA_StatusCode status = UA_decodeBinary(&encoded, &dv.value, &UA_TYPES[UA_TYPES_VARIANT], &options);
        EXPECT_EQ(UA_STATUSCODE_GOOD, status);
        dv.hasValue = true;
        dv.status = UA_STATUSCODE_GOOD;
        dv.sourceTimestamp = UA_DateTime_now();
        dv.serverTimestamp = dv.sourceTimestamp;
        dv.hasSourceTimestamp = true;
        dv.hasServerTimestamp = true;
    }

    void
    build(const std::string &sessionName)
    {
        // Every leaf requests record processing once per update
        callbackSetQueueSize(static_cast<int>(noOfLeafs + 1000));
        callbackInit();
        callbacksRunning = true;
        fence.reset(new CallbackFence);

        new SessionOpen62541(sessionName, "opc.tcp://localhost:4840"); // registers itself

        // Link info of the records (item: not monitored, not an opcuaItem record)
        linkInfo iinfo;
        iinfo.session = sessionName;
        iinfo.identifierString = sessionName;
        iinfo.monitor = false;
        iinfo.isOutput = false;
        iinfo.samplingInterval = 0.0;
        iinfo.queueSize = 1;
        iinfo.clientQueueSize = 3;

        records.reserve(noOfLeafs);
        connectors.reserve(noOfLeafs);
        std::vector<unsigned int> idx(depth, 0);
        for (unsigned int n = 0; n < noOfLeafs; n++) {
            std::list<std::string> path;
            std::string recname(sessionName);
            for (unsigned int l = 0; l < depth; l++) {
                path.push_back(fieldName(idx[l]));
                recname += ":" + std::to_string(idx[l]);
            }
            dbCommon *prec = static_cast<dbCommon *>(std::calloc(1, sizeof(dbCommon)));
            ASSERT_NE(nullptr, prec);
            records.emplace_back(prec, &std::free);
            strncpy(prec->name, recname.c_str(), sizeof(prec->name) - 1);
            prec->prio = menuPriorityLOW;
            // prec->dpvt stays NULL: the record processing callbacks become no-ops

            RecordConnector *pcon = new RecordConnector(prec);
            connectors.emplace_back(pcon);
            std::unique_ptr<linkInfo> pinfo(new linkInfo(iinfo));
            pinfo->linkedToItem = (n == 0);
            pinfo->elementPath = path;
            pcon->plinkinfo = std::move(pinfo);
            if (n == 0) {
                // As in the device support: the item is created from (and keeps a reference to)
                // the link info of its first record
                item = new ItemOpen62541(*pcon->plinkinfo);
                item->rebuildNodeId();
                item->recConnector = pcon;
            }
            pcon->plinkinfo->item = item;
            pcon->pitem = item;
            DataElement::addElementToTree(item, pcon, pcon->plinkinfo->elementPath);

            // next index (odometer)
            for (int l = static_cast<int>(depth) - 1; l >= 0; l--) {
                if (++idx[l] < fanout[l])
                    break;
                idx[l] = 0;
            }
        }
        item->setState(ConnectionStatus::up);
    }

    // Drain the incoming queues of all leafs, checking the values
    unsigned int
    drainAndCheck()
    {
        unsigned int bad = 0;
        for (unsigned int n = 0; n < connectors.size(); n++) {
            epicsFloat64 val = -1.0;
            ProcessReason next = ProcessReason::none;
            long status = connectors[n]->readScalar(&val, &next);
            if (status || val != static_cast<double>(n % fanout[depth - 1]) || next != ProcessReason::none)
                bad++;
        }
        return bad;
    }

    std::string
    shapeString() const
    {
        std::string s;
        for (unsigned int l = 0; l < depth; l++)
            s += (l ? "x" : "") + std::to_string(fanout[l]);
        return s;
    }

    std::vector<unsigned int> fanout; /**< number of members, per level */
    unsigned int depth = 0, iterations = 10;
    unsigned int noOfLeafs = 0, noOfNodes = 0;
    std::vector<UA_DataType> types;
    std::vector<std::vector<UA_DataTypeMember>> members;
    std::vector<std::string> typeNames;
    std::vector<std::vector<std::string>> memberNames;
    std::unique_ptr<UA_DataTypeArray> typeArray;
    UA_ByteString encoded;
    ItemOpen62541 *item = nullptr;
    std::vector<std::unique_ptr<dbCommon, decltype(&std::free)>> records;
    std::vector<std::unique_ptr<RecordConnector>> connectors;
    std::unique_ptr<CallbackFence> fence;
    bool callbacksRunning = false;
};

TEST_F(StructureDissectBenchmark, NestedStructure)
{
    auto t0 = Clock::now();
    if (!createTypes())
        GTEST_SKIP() << "structure too large for open62541 type descriptions";
    createValue();
    double tEncode = msSince(t0);

    t0 = Clock::now();
    build("bench");
    double tSetup = msSince(t0);
    ASSERT_EQ(noOfLeafs, connectors.size());

    // First update (incl. mapping)
    UA_DataValue dv;
    t0 = Clock::now();
    decode(dv);
    double tFirstDecode = msSince(t0);
    t0 = Clock::now();
    item->setIncomingData(dv, ProcessReason::incomingData); // takes ownership of the value
    double tFirst = msSince(t0);
    fence->wait();
    EXPECT_EQ(0u, drainAndCheck()) << "leafs with missing or wrong data after first update";

    // Steady state
    std::vector<double> tDecode, tDissect, tTotal;
    unsigned int bad = 0;
    for (unsigned int i = 0; i < iterations; i++) {
        t0 = Clock::now();
        decode(dv);
        auto t1 = Clock::now();
        item->setIncomingData(dv, ProcessReason::incomingData);
        double d1 = std::chrono::duration<double, std::milli>(t1 - t0).count();
        double d2 = msSince(t1);
        tDecode.push_back(d1);
        tDissect.push_back(d2);
        tTotal.push_back(d1 + d2);
        fence->wait();
        bad += drainAndCheck();
    }
    EXPECT_EQ(0u, bad) << "leafs with missing or wrong data after steady state updates";

    Stats sDec = statsOf(tDecode), sDis = statsOf(tDissect), sTot = statsOf(tTotal);
    std::cout << std::fixed << std::setprecision(2)
              << "\nStructure dissection benchmark (open62541)\n"
              << "  structure:      depth " << depth << ", fan-out " << shapeString() << " -> " << noOfLeafs
              << " leafs, " << noOfNodes << " nodes, " << encoded.length << " bytes encoded\n"
              << "  encoded value:  " << tEncode << " ms (types + value, not part of dissection)\n"
              << "  tree setup:     " << tSetup << " ms (" << noOfLeafs << " records/elements)\n"
              << "  first update:   " << tFirstDecode + tFirst << " ms (decode " << tFirstDecode
              << " ms + dissect " << tFirst << " ms, incl. mapping)\n";
    if (iterations)
        std::cout << "  steady state:   " << iterations << " updates: mean " << sTot.mean << " ms, median "
                  << sTot.median << " ms, min " << sTot.min << " ms, max " << sTot.max << " ms\n"
                  << "    decode:       mean " << sDec.mean << " ms, median " << sDec.median << " ms\n"
                  << "    dissect:      mean " << sDis.mean << " ms, median " << sDis.median << " ms\n"
                  << "                  " << std::setprecision(1) << sTot.mean * 1e6 / noOfLeafs
                  << " ns per leaf element (decode + dissect)\n";
    std::cout << std::endl;
}

} // namespace

#endif // open62541 >= 1.3
