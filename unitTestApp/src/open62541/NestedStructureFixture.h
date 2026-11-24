/*************************************************************************\
* Copyright (c) 2026 ITER Organization.
* This module is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
\*************************************************************************/

/*
 *  Author: Ralph Lange <ralph.lange@gmx.de>
 */

/*
 * Test fixture (open62541): a single item with a nested structure, every leaf
 * element connected to its own (fake) record - without an OPC UA server.
 * Counterpart of the UA SDK fixture with the same name.
 *
 * The structure has one structure type per level (level 0 = outermost),
 * with fanout[l] members named m0, m1, ... The members of the innermost
 * level are doubles; by default, the value of each leaf is its member index
 * (i.e. the last index of its path). The leaf values can be changed through
 * leafValues (then call createValue() again).
 *
 * The structure types are described as open62541 custom data types, like the
 * ones the client creates from the server's type dictionary. As the client
 * library does for a service response, decode() decodes the binary encoded
 * value using these types.
 *
 * The records' processing callbacks are no-ops (dpvt is NULL); the leaf
 * queues are drained with drainAndCheck().
 *
 * Needs open62541 >= 1.3 (member types by pointer, UA_DecodeBinaryOptions).
 *
 * Limits of open62541 1.3 type descriptions: max. 255 members per structure
 * and max. 64 KiB in-memory size (UA_DataType::memSize is 16 bits) - i.e. a
 * structure with mandatory members can hold at most 8191 doubles.
 */

#ifndef DEVOPCUA_NESTEDSTRUCTUREFIXTURE_H
#define DEVOPCUA_NESTEDSTRUCTUREFIXTURE_H

#include <cmath>
#include <cstdlib>
#include <cstring>
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
#include <epicsTime.h>
#include <menuPriority.h>

// The classes under test are compiled into the test executable (not taken from
// the DLL): declare them like in the library sources, or MinGW fails to link
// the (otherwise dllimport) references to Session's static members.
// (EPICS Base headers are included above, so that they keep their import declarations.)
#define epicsExportSharedSymbols
#include "DataElement.h"
#include "ItemOpen62541.h"
#include "RecordConnector.h"
#include "Session.h"
#include "SessionOpen62541.h"
#include "devOpcua.h"

namespace {

using namespace DevOpcua;

inline std::string
fieldName(unsigned int i)
{
    return "m" + std::to_string(i);
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

class NestedStructureFixture : public ::testing::Test
{
protected:
    void
    SetUp() override
    {
        epicsThreadOnce(&Session::onceId, &Session::initOnce, nullptr);
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

    // Fan-out per level that gets as close as possible to the target number of leafs
    // (fixed > 0: use that fan-out on every level)
    static std::vector<unsigned int>
    spreadFanout(unsigned int target, unsigned int depth, unsigned int fixed = 0)
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

    void
    setShape(const std::vector<unsigned int> &fanouts)
    {
        fanout = fanouts;
        depth = static_cast<unsigned int>(fanout.size());
        noOfLeafs = 1;
        noOfNodes = 0;
        for (unsigned int l = 0; l < depth; l++) {
            noOfNodes += noOfLeafs;
            noOfLeafs *= fanout[l];
        }
        leafValues.resize(noOfLeafs);
        for (unsigned int n = 0; n < noOfLeafs; n++)
            leafValues[n] = static_cast<double>(n % fanout[depth - 1]);
    }

    std::string
    shapeString() const
    {
        std::string s;
        for (unsigned int l = 0; l < depth; l++)
            s += (l ? "x" : "") + std::to_string(fanout[l]);
        return s;
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

    // Binary encoded value (Variant with ExtensionObject) from leafValues.
    // As all levels are stored in place, the native value is a plain array of doubles.
    void
    createValue()
    {
        double *data = static_cast<double *>(UA_new(&types[0]));
        for (unsigned int n = 0; n < noOfLeafs; n++)
            data[n] = leafValues[n];
        UA_Variant value;
        UA_Variant_init(&value);
        UA_Variant_setScalar(&value, data, &types[0]);
        UA_ByteString_clear(&encoded);
        ASSERT_EQ(UA_STATUSCODE_GOOD, UA_encodeBinary(&value, &UA_TYPES[UA_TYPES_VARIANT], &encoded));
        UA_Variant_clear(&value);
    }

    // Decode the incoming value, as the client library does for the service response
    void
    decode(UA_DataValue &dv, UA_StatusCode status = UA_STATUSCODE_GOOD)
    {
        UA_DataValue_init(&dv);
        UA_DecodeBinaryOptions options;
        std::memset(&options, 0, sizeof(options));
        options.customTypes = typeArray.get();
        EXPECT_EQ(UA_STATUSCODE_GOOD, UA_decodeBinary(&encoded, &dv.value, &UA_TYPES[UA_TYPES_VARIANT], &options));
        dv.hasValue = true;
        dv.status = status;
        dv.hasStatus = (status != UA_STATUSCODE_GOOD);
        dv.sourceTimestamp = UA_DateTime_now();
        dv.serverTimestamp = dv.sourceTimestamp;
        dv.hasSourceTimestamp = true;
        dv.hasServerTimestamp = true;
    }

    // Push an incoming data value into the item (decoded from the encoded value)
    void
    update(UA_StatusCode status = UA_STATUSCODE_GOOD, ProcessReason reason = ProcessReason::incomingData)
    {
        UA_DataValue dv;
        decode(dv, status);
        item->setIncomingData(dv, reason); // takes ownership of the value
    }

    // Session, item and one record per leaf element
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
        if (itemUpdate != UpdateOption::notSet)
            iinfo.updateOnChange = (itemUpdate == UpdateOption::change);

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

    // Session, item with a scalar (double) value and one record connected to it (no element path)
    void
    buildScalar(const std::string &sessionName)
    {
        callbackSetQueueSize(1000);
        callbackInit();
        callbacksRunning = true;
        fence.reset(new CallbackFence);

        new SessionOpen62541(sessionName, "opc.tcp://localhost:4840"); // registers itself

        linkInfo iinfo;
        iinfo.session = sessionName;
        iinfo.identifierString = sessionName;
        iinfo.monitor = false;
        iinfo.isOutput = false;
        iinfo.samplingInterval = 0.0;
        iinfo.queueSize = 1;
        iinfo.clientQueueSize = 3;
        if (itemUpdate != UpdateOption::notSet)
            iinfo.updateOnChange = (itemUpdate == UpdateOption::change);

        dbCommon *prec = static_cast<dbCommon *>(std::calloc(1, sizeof(dbCommon)));
        ASSERT_NE(nullptr, prec);
        records.emplace_back(prec, &std::free);
        strncpy(prec->name, sessionName.c_str(), sizeof(prec->name) - 1);
        prec->prio = menuPriorityLOW;

        RecordConnector *pcon = new RecordConnector(prec);
        connectors.emplace_back(pcon);
        pcon->plinkinfo.reset(new linkInfo(iinfo));
        pcon->plinkinfo->linkedToItem = true;
        item = new ItemOpen62541(*pcon->plinkinfo);
        item->rebuildNodeId();
        item->recConnector = pcon;
        pcon->plinkinfo->item = item;
        pcon->pitem = item;
        DataElement::addElementToTree(item, pcon, pcon->plinkinfo->elementPath);
        item->setState(ConnectionStatus::up);
    }

    // Push an incoming scalar (double) data value into the item
    void
    updateScalar(double value, ProcessReason reason = ProcessReason::incomingData)
    {
        UA_DataValue dv;
        UA_DataValue_init(&dv);
        EXPECT_EQ(UA_STATUSCODE_GOOD, UA_Variant_setScalarCopy(&dv.value, &value, &UA_TYPES[UA_TYPES_DOUBLE]));
        dv.hasValue = true;
        dv.sourceTimestamp = UA_DateTime_now();
        dv.serverTimestamp = dv.sourceTimestamp;
        dv.hasSourceTimestamp = true;
        dv.hasServerTimestamp = true;
        item->setIncomingData(dv, reason); // takes ownership of the value
    }

    // Wait for all requested record processing callbacks to have run
    void
    waitForCallbacks()
    {
        fence->wait();
    }

    // Drain the incoming queues of all leafs, checking the values against leafValues
    // Returns the number of leafs with missing or wrong data
    unsigned int
    drainAndCheck()
    {
        unsigned int bad = 0;
        for (unsigned int n = 0; n < connectors.size(); n++) {
            epicsFloat64 val = -1.0;
            ProcessReason next = ProcessReason::none;
            long status = connectors[n]->readScalar(&val, &next);
            if (status || val != leafValues[n] || next != ProcessReason::none)
                bad++;
        }
        return bad;
    }

    // Drain the incoming queues of all leafs
    // Returns the number of leafs that had an update (reading an update sets the record's TIME,
    // reading an empty queue doesn't); values: the values read (-1.0 if none)
    unsigned int
    drainChanged(std::vector<double> &values)
    {
        unsigned int n = 0;
        values.assign(connectors.size(), -1.0);
        for (unsigned int i = 0; i < connectors.size(); i++) {
            ProcessReason next = ProcessReason::none;
            records[i]->time = epicsTimeStamp();
            connectors[i]->readScalar(&values[i], &next);
            if (records[i]->time.secPastEpoch || records[i]->time.nsec)
                n++;
        }
        return n;
    }

    std::vector<unsigned int> fanout; /**< number of members, per level */
    unsigned int depth = 0;
    unsigned int noOfLeafs = 0;
    unsigned int noOfNodes = 0;
    std::vector<double> leafValues; /**< value per leaf (record) */
    enum class UpdateOption { notSet, always, change };
    UpdateOption itemUpdate = UpdateOption::always; /**< item option update (notSet: default from linkInfo) */
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

} // namespace

#endif // DEVOPCUA_NESTEDSTRUCTUREFIXTURE_H
