/*************************************************************************\
* Copyright (c) 2026 ITER Organization.
* This module is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
\*************************************************************************/

/*
 *  Author: Ralph Lange <ralph.lange@gmx.de>
 */

/*
 * Benchmark for dissecting incoming structured data into the data element tree.
 *
 * A single item receives a nested structure (default: about 100,000 leaf
 * elements, depth 5, i.e. fan-out 10 and 11,111 structure nodes). Every leaf
 * element is connected to its own (fake) record, as in an IOC that maps every
 * structure member to a record.
 *
 * For a given depth, the fan-out of each level is chosen so that the number of
 * leafs is as close as possible to the target (e.g. depth 2: 316 x 316 = 99,856;
 * depth 10: 3 and 4 members per level, 104,976 leafs), so that different depths
 * can be compared at about the same amount of data.
 *
 * Measured is ItemUaSdk::setIncomingData() for one update:
 *   - the first update (includes structure definition lookups and creating
 *     the child element maps)
 *   - the following updates (steady state)
 * The incoming queues of all leaf elements are drained between updates
 * (outside the timed region), so that every update takes the regular path
 * of queueing the value and requesting record processing.
 *
 * The structure definitions are served by a stub UaSession, so no OPC UA
 * server is needed.
 *
 * Environment variables to change the parameters:
 *   DEVOPCUA_BENCH_LEAFS       target number of leaf elements        (100000)
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
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <uaplatformlayer.h>
#include <uasession.h>
#include <uadatetime.h>
#include <uaextensionobject.h>
#include <uagenericstructurevalue.h>
#include <uastructuredefinition.h>
#include <uastructurefield.h>
#include <uavariant.h>

#include <callback.h>
#include <dbCommon.h>
#include <epicsEvent.h>
#include <epicsThread.h>
#include <menuPriority.h>

#include "DataElement.h"
#include "ItemUaSdk.h"
#include "RecordConnector.h"
#include "Session.h"
#include "SessionUaSdk.h"
#include "devOpcua.h"

namespace DevOpcua {

// Test hook (friend of SessionUaSdk): replace the low level SDK session
class SessionUaSdkTestAccess
{
public:
    static void
    replaceUaSession(SessionUaSdk &session, UaClientSdk::UaSession *uasession)
    {
        delete session.puasession;
        session.puasession = uasession;
    }
};

} // namespace DevOpcua

namespace {

using namespace DevOpcua;
using Clock = std::chrono::steady_clock;

// Low level session serving the structure definitions locally
class StubUaSession : public UaClientSdk::UaSession
{
public:
    void
    addDefinition(const UaStructureDefinition &def)
    {
        definitions.emplace_back(def.dataTypeId(), def);
        definitions.emplace_back(def.binaryEncodingId(), def);
    }

    virtual UaStructureDefinition
    structureDefinition(const UaNodeId &id) override
    {
        for (const auto &it : definitions)
            if (it.first == id)
                return it.second;
        return UaStructureDefinition();
    }

private:
    std::vector<std::pair<UaNodeId, UaStructureDefinition>> definitions;
};

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

std::string
fieldName(unsigned int i)
{
    return "m" + std::to_string(i);
}

double
msSince(const Clock::time_point &start)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
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

class StructureDissectBenchmark : public ::testing::Test
{
protected:
    void
    SetUp() override
    {
        epicsThreadOnce(&Session::onceId, &Session::initOnce, nullptr);

        unsigned int target = envOrDefault("DEVOPCUA_BENCH_LEAFS", 100000);
        unsigned int fixed = envOrDefault("DEVOPCUA_BENCH_FANOUT", 0);
        depth = envOrDefault("DEVOPCUA_BENCH_DEPTH", 5);
        iterations = envOrDefault("DEVOPCUA_BENCH_ITERATIONS", 10);

        // Fan-out per level: spread the target number of leafs over the levels
        double remaining = target;
        for (unsigned int l = 0; l < depth; l++) {
            unsigned int f = fixed;
            if (!f) {
                f = static_cast<unsigned int>(std::lround(std::pow(remaining, 1.0 / (depth - l))));
                if (f < 1)
                    f = 1;
            }
            fanout.push_back(f);
            remaining /= f;
        }
        noOfLeafs = 1;
        noOfNodes = 0;
        for (unsigned int l = 0; l < depth; l++) {
            noOfNodes += noOfLeafs;
            noOfLeafs *= fanout[l];
        }

        // Every leaf requests record processing once per update
        callbackSetQueueSize(static_cast<int>(noOfLeafs + 1000));
        callbackInit();
    }

    void
    TearDown() override
    {
        callbackStop();
        callbackCleanup();
    }

    // Structure types: level 0 (outermost) ... depth-1 (members are doubles)
    std::vector<UaStructureDefinition>
    createDefinitions()
    {
        std::vector<UaStructureDefinition> defs(depth);
        for (int l = static_cast<int>(depth) - 1; l >= 0; l--) {
            UaStructureDefinition &def = defs[l];
            def.setName(UaString(("BenchLevel" + std::to_string(l)).c_str()));
            def.setDataTypeId(UaNodeId(5000 + l, 1));
            def.setBinaryEncodingId(UaNodeId(6000 + l, 1));
            for (unsigned int i = 0; i < fanout[l]; i++) {
                UaStructureField field;
                field.setName(UaString(fieldName(i).c_str()));
                if (l == static_cast<int>(depth) - 1) {
                    field.setDataTypeId(UaNodeId(OpcUaId_Double));
                    field.setValueType(OpcUaType_Double);
                } else {
                    field.setDataTypeId(defs[l + 1].dataTypeId());
                    field.setStructureDefinition(defs[l + 1]);
                }
                field.setValueRank(-1);
                def.addChild(field);
            }
        }
        return defs;
    }

    // Leaf values are their member index, i.e. the last digit of their path
    UaVariant
    createValue(const std::vector<UaStructureDefinition> &defs)
    {
        UaGenericStructureValue value(defs[depth - 1]);
        for (unsigned int i = 0; i < fanout[depth - 1]; i++) {
            UaVariant v;
            v.setDouble(i);
            EXPECT_EQ(static_cast<OpcUa_StatusCode>(OpcUa_Good), value.setField(static_cast<int>(i), v));
        }
        for (int l = static_cast<int>(depth) - 2; l >= 0; l--) {
            UaGenericStructureValue outer(defs[l]);
            for (unsigned int i = 0; i < fanout[l]; i++)
                EXPECT_EQ(static_cast<OpcUa_StatusCode>(OpcUa_Good), outer.setField(static_cast<int>(i), value));
            value = outer;
        }
        UaExtensionObject eo;
        // UaGenericStructureValue::toExtensionObject() returns void in older SDK versions (<= 1.7.2)
        value.toExtensionObject(eo);
        EXPECT_TRUE(eo.encodingTypeId() == defs[0].binaryEncodingId()) << "encoding the structure value failed";
        UaVariant var;
        var.setExtensionObject(eo, OpcUa_True);
        return var;
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

    std::vector<unsigned int> fanout; /**< number of members, per level */
    unsigned int depth, iterations;
    unsigned int noOfLeafs, noOfNodes;
    std::vector<std::unique_ptr<dbCommon, decltype(&std::free)>> records;
    std::vector<std::unique_ptr<RecordConnector>> connectors;
};

TEST_F(StructureDissectBenchmark, NestedStructure)
{
    auto t0 = Clock::now();
    std::vector<UaStructureDefinition> defs = createDefinitions();
    UaVariant value = createValue(defs);
    double tEncode = msSince(t0);

    OpcUa_DataValue dv;
    OpcUa_DataValue_Initialize(&dv);
    value.copyTo(&dv.Value);
    dv.StatusCode = OpcUa_Good;
    dv.SourceTimestamp = UaDateTime::now();
    dv.ServerTimestamp = dv.SourceTimestamp;

    // Session with stub low level session
    SessionUaSdk *session = new SessionUaSdk("bench", "opc.tcp://localhost:4840");
    StubUaSession *stub = new StubUaSession;
    for (const auto &def : defs)
        stub->addDefinition(def);
    SessionUaSdkTestAccess::replaceUaSession(*session, stub);

    // Link info of the records (item: not monitored, not an opcuaItem record)
    linkInfo iinfo;
    iinfo.session = "bench";
    iinfo.identifierString = "bench";
    iinfo.monitor = false;
    iinfo.isOutput = false;
    iinfo.samplingInterval = 0.0;
    iinfo.queueSize = 1;
    iinfo.clientQueueSize = 3;
    ItemUaSdk *item = nullptr;

    // One record per leaf element
    t0 = Clock::now();
    records.reserve(noOfLeafs);
    connectors.reserve(noOfLeafs);
    std::vector<unsigned int> idx(depth, 0);
    for (unsigned int n = 0; n < noOfLeafs; n++) {
        std::list<std::string> path;
        std::string recname("BENCH");
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
            item = new ItemUaSdk(*pcon->plinkinfo);
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
    double tSetup = msSince(t0);

    item->setState(ConnectionStatus::up);
    CallbackFence fence;

    // First update: structure definition lookups and child maps
    t0 = Clock::now();
    item->setIncomingData(dv, ProcessReason::incomingData);
    double tFirst = msSince(t0);
    fence.wait();
    EXPECT_EQ(0u, drainAndCheck()) << "leafs with missing or wrong data after first update";

    // Steady state
    std::vector<double> times;
    unsigned int bad = 0;
    for (unsigned int i = 0; i < iterations; i++) {
        t0 = Clock::now();
        item->setIncomingData(dv, ProcessReason::incomingData);
        times.push_back(msSince(t0));
        fence.wait();
        bad += drainAndCheck();
    }
    EXPECT_EQ(0u, bad) << "leafs with missing or wrong data after steady state updates";

    double sum = 0.0;
    for (double t : times)
        sum += t;
    double mean = times.empty() ? 0.0 : sum / times.size();
    std::sort(times.begin(), times.end());
    double median = times.empty() ? 0.0 : times[times.size() / 2];

    std::string fanouts;
    for (unsigned int l = 0; l < depth; l++)
        fanouts += (l ? "x" : "") + std::to_string(fanout[l]);

    std::cout << std::fixed << std::setprecision(2)
              << "\nStructure dissection benchmark (UA SDK)\n"
              << "  structure:      depth " << depth << ", fan-out " << fanouts << " -> " << noOfLeafs << " leafs, "
              << noOfNodes << " nodes\n"
              << "  encoded value:  " << tEncode << " ms (definitions + value, not part of dissection)\n"
              << "  tree setup:     " << tSetup << " ms (" << noOfLeafs << " records/elements)\n"
              << "  first update:   " << tFirst << " ms (incl. definition lookups and mapping)\n";
    if (!times.empty())
        std::cout << "  steady state:   " << iterations << " updates: mean " << mean << " ms, median " << median
                  << " ms, min " << times.front() << " ms, max " << times.back() << " ms\n"
                  << "                  " << std::setprecision(1) << mean * 1e6 / noOfLeafs
                  << " ns per leaf element\n";
    std::cout << std::endl;

    OpcUa_DataValue_Clear(&dv);
    // The tree, item and session are intentionally not torn down (process exit cleans up)
}

} // namespace
