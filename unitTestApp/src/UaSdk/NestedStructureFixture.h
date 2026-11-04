/*************************************************************************\
* Copyright (c) 2026 ITER Organization.
* This module is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
\*************************************************************************/

/*
 *  Author: Ralph Lange <ralph.lange@gmx.de>
 */

/*
 * Test fixture: a single item with a nested structure, every leaf element
 * connected to its own (fake) record - without an OPC UA server.
 *
 * The structure has one structure type per level (level 0 = outermost),
 * with fanout[l] members named m0, m1, ... The members of the innermost
 * level are doubles; the value of each leaf is its member index (i.e. the
 * last index of its path).
 *
 * The structure definitions are served by a stub UaSession that replaces the
 * session's low level SDK session (through the SessionUaSdkTestAccess hook).
 * The records' processing callbacks are no-ops (dpvt is NULL); the leaf
 * queues are drained with drainAndCheck().
 */

#ifndef DEVOPCUA_NESTEDSTRUCTUREFIXTURE_H
#define DEVOPCUA_NESTEDSTRUCTUREFIXTURE_H

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <list>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

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
        lookups++;
        for (const auto &it : definitions)
            if (it.first == id)
                return it.second;
        return UaStructureDefinition();
    }

    unsigned int lookups = 0; /**< number of structure definition queries */

private:
    std::vector<std::pair<UaNodeId, UaStructureDefinition>> definitions;
};

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
        OpcUa_DataValue_Initialize(&dv);
    }

    void
    TearDown() override
    {
        fence.reset();
        if (callbacksRunning) {
            callbackStop();
            callbackCleanup();
        }
        OpcUa_DataValue_Clear(&dv);
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
    }

    std::string
    shapeString() const
    {
        std::string s;
        for (unsigned int l = 0; l < depth; l++)
            s += (l ? "x" : "") + std::to_string(fanout[l]);
        return s;
    }

    // Structure types: level 0 (outermost) ... depth-1 (members are doubles)
    void
    createDefinitions()
    {
        defs.assign(depth, UaStructureDefinition());
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
    }

    // Incoming data value: leaf values are their member index
    void
    createValue()
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

        OpcUa_DataValue_Clear(&dv);
        OpcUa_DataValue_Initialize(&dv);
        var.copyTo(&dv.Value);
        dv.StatusCode = OpcUa_Good;
        dv.SourceTimestamp = UaDateTime::now();
        dv.ServerTimestamp = dv.SourceTimestamp;
    }

    // Session (with stub low level session), item and one record per leaf element
    void
    build(const std::string &sessionName)
    {
        // Every leaf requests record processing once per update
        callbackSetQueueSize(static_cast<int>(noOfLeafs + 1000));
        callbackInit();
        callbacksRunning = true;
        fence.reset(new CallbackFence);

        SessionUaSdk *session = new SessionUaSdk(sessionName, "opc.tcp://localhost:4840");
        stub = new StubUaSession;
        for (const auto &def : defs)
            stub->addDefinition(def);
        SessionUaSdkTestAccess::replaceUaSession(*session, stub);

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

        item->setState(ConnectionStatus::up);
    }

    // Push the incoming data value into the item (the measured operation)
    void
    update()
    {
        item->setIncomingData(dv, ProcessReason::incomingData);
    }

    // Wait for all requested record processing callbacks to have run
    void
    waitForCallbacks()
    {
        fence->wait();
    }

    // Drain the incoming queues of all leafs, checking the values
    // Returns the number of leafs with missing or wrong data
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
    unsigned int depth = 0;
    unsigned int noOfLeafs = 0;
    unsigned int noOfNodes = 0;
    std::vector<UaStructureDefinition> defs;
    OpcUa_DataValue dv;
    StubUaSession *stub = nullptr;
    ItemUaSdk *item = nullptr;
    std::vector<std::unique_ptr<dbCommon, decltype(&std::free)>> records;
    std::vector<std::unique_ptr<RecordConnector>> connectors;
    std::unique_ptr<CallbackFence> fence;
    bool callbacksRunning = false;
};

} // namespace

#endif // DEVOPCUA_NESTEDSTRUCTUREFIXTURE_H
