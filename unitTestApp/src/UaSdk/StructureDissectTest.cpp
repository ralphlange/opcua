/*************************************************************************\
* Copyright (c) 2026 ITER Organization.
* This module is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
\*************************************************************************/

/*
 *  Author: Ralph Lange <ralph.lange@gmx.de>
 */

/*
 * Dissection of nested structures into the data element tree (UA SDK):
 * reading (incoming data into all leafs), writing (one changed leaf
 * encoded back into the nested structure) and the data timestamp taken
 * from a structure member (timestamp=data with element).
 * Also: members without elements (skipped while decoding), optional members,
 * different member types and a nested union.
 */

#include <gtest/gtest.h>

#include <epicsTime.h>
#include <errlog.h>

#include <uaarraytemplates.h>
#include <uaextensionobject.h>
#include <uagenericstructurevalue.h>
#include <uagenericunionvalue.h>
#include <uavariant.h>

#include "NestedStructureFixture.h"
#include "SubscriptionUaSdk.h"

namespace {

class StructureDissectTest : public NestedStructureFixture
{
protected:
    void
    SetUp() override
    {
        NestedStructureFixture::SetUp();
        setShape({3, 3, 3});
        createDefinitions();
        createValue();
    }

    // Scalar item: the update option makes no difference
    void checkScalarIgnoresUpdateOption(const std::string &sessionName);

    // Read all leafs; check that every record got the expected time stamp
    void
    expectRecordTimes(const epicsTime &expected)
    {
        update();
        waitForCallbacks();
        ASSERT_EQ(0u, drainAndCheck()) << "leafs with missing or wrong data";
        unsigned int wrong = 0;
        for (const auto &prec : records)
            if (epicsTime(prec->time) != expected)
                wrong++;
        EXPECT_EQ(0u, wrong) << "records with unexpected time stamp";
    }

    // Data timestamp setup: "ts" member clearly different from the source timestamp
    void
    useTimestampElement(const std::string &element)
    {
        withTimestampMember = true;
        tsValue = UaDateTime::fromTime_t(1700000000);
        timestampElement = element;
        createDefinitions();
        createValue();
    }

    // Index of the leaf with the path m<a>.m<b>.m<c>
    static unsigned int leafIndex(unsigned int a, unsigned int b, unsigned int c) { return (a * 3 + b) * 3 + c; }
};

TEST_F(StructureDissectTest, ReadNestedStructure)
{
    build("dissectRead");
    ASSERT_EQ(27u, connectors.size());

    update();
    waitForCallbacks();
    EXPECT_EQ(0u, drainAndCheck()) << "leafs with missing or wrong data after first update";

    // Nested nodes get their definition from the decoded value (no session queries)
    EXPECT_EQ(1u, stub->lookups) << "structure definition queries for a nested structure";

    update();
    waitForCallbacks();
    EXPECT_EQ(0u, drainAndCheck()) << "leafs with missing or wrong data after second update";
    EXPECT_EQ(1u, stub->lookups) << "structure definitions not cached";
}

TEST_F(StructureDissectTest, WriteNestedStructureMember)
{
    build("dissectWrite");
    update();
    waitForCallbacks();
    ASSERT_EQ(0u, drainAndCheck());

    ASSERT_EQ(0, connectors[leafIndex(1, 1, 1)]->writeScalar(static_cast<epicsFloat64>(42.0)));

    _OpcUa_WriteValue wv;
    OpcUa_WriteValue_Initialize(&wv);
    const unsigned int lookupsBefore = stub->lookups;
    item->copyAndClearOutgoingData(wv);
    UaVariant out(wv.Value.Value);
    OpcUa_WriteValue_Clear(&wv);
    EXPECT_EQ(lookupsBefore, stub->lookups) << "structure definition queries when writing";

    UaExtensionObject eo;
    ASSERT_EQ(static_cast<OpcUa_StatusCode>(OpcUa_Good), out.toExtensionObject(eo));
    UaGenericStructureValue root(eo, defs[0]);
    for (unsigned int a = 0; a < 3; a++)
        for (unsigned int b = 0; b < 3; b++)
            for (unsigned int c = 0; c < 3; c++) {
                OpcUa_Double val = -1.0;
                EXPECT_EQ(static_cast<OpcUa_StatusCode>(OpcUa_Good),
                          root.genericStructure(a).genericStructure(b).value(c).toDouble(val));
                double expected = (leafIndex(a, b, c) == leafIndex(1, 1, 1)) ? 42.0 : static_cast<double>(c);
                EXPECT_EQ(expected, val) << "member m" << a << ".m" << b << ".m" << c;
            }
}

TEST_F(StructureDissectTest, TimestampFromStructureMember)
{
    useTimestampElement("ts");
    build("dissectTsMember");
    expectRecordTimes(ItemUaSdk::uaToEpicsTime(tsValue, 0));
}

TEST_F(StructureDissectTest, TimestampMemberNotFound)
{
    // Falls back to the source timestamp
    useTimestampElement("nosuchmember");
    build("dissectTsNotFound");
    expectRecordTimes(ItemUaSdk::uaToEpicsTime(UaDateTime(dv.SourceTimestamp), 0));
}

TEST_F(StructureDissectTest, TimestampMemberWrongType)
{
    // m0 is a structure, not a DateTime: falls back to the source timestamp
    useTimestampElement("m0");
    build("dissectTsWrongType");
    expectRecordTimes(ItemUaSdk::uaToEpicsTime(UaDateTime(dv.SourceTimestamp), 0));
}

TEST_F(StructureDissectTest, DataChangeCallbackStatistics)
{
    build("dissectStats");
    // Subscription without a server: data changes are pushed directly into the callback
    // (intentionally not deleted, as the tree, item and session)
    SubscriptionUaSdk *sub = new SubscriptionUaSdk("dissectStatsSub", session, 100.0);
    sub->addItemUaSdk(item);

    UaDataNotifications notifications;
    notifications.create(1);
    notifications[0].ClientHandle = 0;
    OpcUa_DataValue_CopyTo(&dv, &notifications[0].Value);
    UaDiagnosticInfos diagnostics;
    for (int i = 0; i < 2; i++) {
        sub->dataChange(0, notifications, diagnostics);
        waitForCallbacks();
        ASSERT_EQ(0u, drainAndCheck()) << "leafs with missing or wrong data after callback " << i;
    }

    auto shown = [&]() {
        testing::internal::CaptureStdout();
        sub->show(1);
        return testing::internal::GetCapturedStdout();
    };
    std::string out = shown();
    EXPECT_NE(std::string::npos, out.find(" items=1 callbacks=2 notifications=2 avgMs=")) << out;
    EXPECT_NE(std::string::npos, out.find(std::string("(") + item->getNodeId().toXmlString().toUtf8() + ")\n"))
        << out;

    sub->setOption("resetstats", "1");
    out = shown();
    EXPECT_NE(std::string::npos, out.find(" items=1 callbacks=0 notifications=0 avgMs=0.000 maxMs=0.000 lastMs=0.000 overInterval=0\n")) << out;
}

// Drain the incoming queues of all leafs
// Returns the number of leafs that had an update (reading an update sets the record's TIME,
// reading an empty queue doesn't)
static unsigned int
drainChanged(const std::vector<std::unique_ptr<RecordConnector>> &connectors,
             const std::vector<std::unique_ptr<dbCommon, decltype(&std::free)>> &records,
             std::vector<double> &values)
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

TEST_F(StructureDissectTest, UpdateOnChange)
{
    itemUpdate = UpdateOption::change;
    build("dissectOnChange");
    ASSERT_EQ(27u, connectors.size());
    // Draining empty queues logs an error for every leaf
    eltc(0);
    std::vector<double> values;
    const unsigned int changed = leafIndex(1, 1, 1);

    update();
    waitForCallbacks();
    EXPECT_EQ(0u, drainAndCheck()) << "leafs with missing or wrong data after first update";

    update();
    waitForCallbacks();
    EXPECT_EQ(0u, drainChanged(connectors, records, values)) << "leafs updated without a change";

    // Change a single leaf (m1.m1.m1)
    {
        UaVariant var(dv.Value);
        UaExtensionObject eo;
        ASSERT_EQ(static_cast<OpcUa_StatusCode>(OpcUa_Good), var.toExtensionObject(eo));
        UaGenericStructureValue root(eo, defs[0]);
        UaGenericStructureValue l1 = root.genericStructure(1);
        UaGenericStructureValue l2 = l1.genericStructure(1);
        UaVariant v;
        v.setDouble(42.0);
        ASSERT_EQ(static_cast<OpcUa_StatusCode>(OpcUa_Good), l2.setField(1, v));
        ASSERT_EQ(static_cast<OpcUa_StatusCode>(OpcUa_Good), l1.setField(1, l2));
        ASSERT_EQ(static_cast<OpcUa_StatusCode>(OpcUa_Good), root.setField(1, l1));
        setDataValue(root);
    }
    update();
    waitForCallbacks();
    EXPECT_EQ(1u, drainChanged(connectors, records, values)) << "leafs updated after changing one leaf";
    EXPECT_EQ(42.0, values[changed]);

    // A status change updates all leafs
    dv.StatusCode = OpcUa_UncertainLastUsableValue;
    update();
    waitForCallbacks();
    EXPECT_EQ(27u, drainChanged(connectors, records, values)) << "leafs updated after a status change";
    update();
    waitForCallbacks();
    EXPECT_EQ(0u, drainChanged(connectors, records, values)) << "leafs updated without a status change";
    dv.StatusCode = OpcUa_Good;
    update();
    waitForCallbacks();
    EXPECT_EQ(27u, drainChanged(connectors, records, values)) << "leafs updated after the status went back";

    // A record writing to its leaf gets the next incoming value (readback)
    ASSERT_EQ(0, connectors[leafIndex(2, 0, 1)]->writeScalar(static_cast<epicsFloat64>(7.0)));
    update();
    waitForCallbacks();
    EXPECT_EQ(1u, drainChanged(connectors, records, values)) << "leafs updated after a record wrote";
    EXPECT_EQ(1.0, values[leafIndex(2, 0, 1)]) << "readback of the written leaf";

    // After a connection loss, all leafs get the next incoming value
    item->setIncomingEvent(ProcessReason::connectionLoss);
    waitForCallbacks();
    drainChanged(connectors, records, values);
    update();
    waitForCallbacks();
    EXPECT_EQ(27u, drainChanged(connectors, records, values)) << "leafs updated after a connection loss";

    errlogFlush();
    eltc(1);
}

// With update=always, the item's elements are updated for every monitor update
TEST_F(StructureDissectTest, UpdateAlways)
{
    itemUpdate = UpdateOption::always;
    build("dissectAlways");
    for (const auto &pcon : connectors)
        ASSERT_FALSE(pcon->plinkinfo->updateOnChange) << "setting of " << pcon->getRecordName();
    eltc(0);
    std::vector<double> values;

    update();
    waitForCallbacks();
    EXPECT_EQ(0u, drainAndCheck()) << "leafs with missing or wrong data after first update";
    for (int i = 0; i < 2; i++) {
        update();
        waitForCallbacks();
        EXPECT_EQ(27u, drainChanged(connectors, records, values)) << "leafs updated by unchanged monitor update " << i;
    }

    errlogFlush();
    eltc(1);
}

// The update option of an element record overrides the item's setting (its default)
// (the parser sets the element's default from the item: here, the element's setting is changed directly)
TEST_F(StructureDissectTest, UpdateOnChangeElementOverridesItem)
{
    itemUpdate = UpdateOption::change;
    build("dissectOnChangeOverride");
    const unsigned int always = leafIndex(0, 1, 2);
    connectors[always]->plinkinfo->updateOnChange = false;
    eltc(0);
    std::vector<double> values;

    update();
    waitForCallbacks();
    EXPECT_EQ(0u, drainAndCheck()) << "leafs with missing or wrong data after first update";
    update();
    waitForCallbacks();
    EXPECT_EQ(1u, drainChanged(connectors, records, values)) << "leafs updated without a change";
    EXPECT_EQ(2.0, values[always]) << "element with update=always";

    errlogFlush();
    eltc(1);
}

TEST_F(StructureDissectTest, UpdateAlwaysElementOverridesItem)
{
    itemUpdate = UpdateOption::always;
    build("dissectAlwaysOverride");
    const unsigned int onChange = leafIndex(2, 1, 0);
    connectors[onChange]->plinkinfo->updateOnChange = true;
    eltc(0);
    std::vector<double> values;

    update();
    waitForCallbacks();
    EXPECT_EQ(0u, drainAndCheck()) << "leafs with missing or wrong data after first update";
    update();
    waitForCallbacks();
    EXPECT_EQ(26u, drainChanged(connectors, records, values)) << "leafs updated without a change";
    EXPECT_EQ(-1.0, values[onChange]) << "element with update=change";

    errlogFlush();
    eltc(1);
}

// update=chg only applies to data updates from the monitored item:
// read data is always passed, and the next monitor update after a read is passed, too
TEST_F(StructureDissectTest, UpdateOnChangeOnlyFiltersMonitorUpdates)
{
    itemUpdate = UpdateOption::change;
    build("dissectOnChangeRead");
    eltc(0);
    std::vector<double> values;

    update();
    waitForCallbacks();
    EXPECT_EQ(0u, drainAndCheck()) << "leafs with missing or wrong data after first update";
    update();
    waitForCallbacks();
    EXPECT_EQ(0u, drainChanged(connectors, records, values)) << "leafs updated by an unchanged monitor update";

    update(ProcessReason::readComplete);
    waitForCallbacks();
    EXPECT_EQ(27u, drainChanged(connectors, records, values)) << "leafs updated by unchanged read data";

    update();
    waitForCallbacks();
    EXPECT_EQ(27u, drainChanged(connectors, records, values)) << "leafs updated by the first monitor update after a read";
    update();
    waitForCallbacks();
    EXPECT_EQ(0u, drainChanged(connectors, records, values)) << "leafs updated by a second unchanged monitor update";

    errlogFlush();
    eltc(1);
}

// Without an update option, the item and its elements use the default (update=change)
TEST_F(StructureDissectTest, UpdateDefaultIsChange)
{
    itemUpdate = UpdateOption::notSet;
    build("dissectDefault");
    for (const auto &pcon : connectors)
        ASSERT_TRUE(pcon->plinkinfo->updateOnChange) << "default of " << pcon->getRecordName();
    eltc(0);
    std::vector<double> values;

    update();
    waitForCallbacks();
    EXPECT_EQ(0u, drainAndCheck()) << "leafs with missing or wrong data after first update";
    update();
    waitForCallbacks();
    EXPECT_EQ(0u, drainChanged(connectors, records, values)) << "leafs updated without a change";

    errlogFlush();
    eltc(1);
}

// An element record with update=always overrides the item's default (update=change)
TEST_F(StructureDissectTest, UpdateAlwaysElementOverridesDefault)
{
    itemUpdate = UpdateOption::notSet;
    build("dissectDefaultOverride");
    const unsigned int always = leafIndex(1, 2, 0);
    connectors[always]->plinkinfo->updateOnChange = false;
    eltc(0);
    std::vector<double> values;

    update();
    waitForCallbacks();
    EXPECT_EQ(0u, drainAndCheck()) << "leafs with missing or wrong data after first update";
    update();
    waitForCallbacks();
    EXPECT_EQ(1u, drainChanged(connectors, records, values)) << "leafs updated without a change";
    EXPECT_EQ(0.0, values[always]) << "element with update=always";

    errlogFlush();
    eltc(1);
}

// For a scalar (non-structure) item, the update option is ignored:
// every monitor update is passed, even if the value has not changed
void
StructureDissectTest::checkScalarIgnoresUpdateOption(const std::string &sessionName)
{
    buildScalar(sessionName);
    ASSERT_EQ(1u, connectors.size());
    std::vector<double> values;

    for (int i = 0; i < 3; i++) {
        setScalarValue(5.0);
        update();
        waitForCallbacks();
        EXPECT_EQ(1u, drainChanged(connectors, records, values)) << "record not updated by unchanged monitor update " << i;
        EXPECT_EQ(5.0, values[0]);
    }
}

TEST_F(StructureDissectTest, ScalarItemIgnoresUpdateDefault)
{
    itemUpdate = UpdateOption::notSet;
    checkScalarIgnoresUpdateOption("scalarDefault");
}

TEST_F(StructureDissectTest, ScalarItemIgnoresUpdateChange)
{
    itemUpdate = UpdateOption::change;
    checkScalarIgnoresUpdateOption("scalarChange");
}

TEST_F(StructureDissectTest, ScalarItemIgnoresUpdateAlways)
{
    itemUpdate = UpdateOption::always;
    checkScalarIgnoresUpdateOption("scalarAlways");
}

TEST_F(StructureDissectTest, ReadPartiallyConnectedStructure)
{
    // No elements for m1 (a nested structure), m2.m0 (a nested structure) and all m*.m*.m2 (leafs):
    // these members are decoded without data elements
    build("dissectPartial", [](const std::vector<unsigned int> &idx) {
        return idx[0] != 1 && !(idx[0] == 2 && idx[1] == 0) && idx[2] != 2;
    });
    ASSERT_EQ(10u, connectors.size());

    for (int i = 0; i < 2; i++) {
        update();
        waitForCallbacks();
        EXPECT_EQ(0u, drainAndCheck()) << "leafs with missing or wrong data after update " << i;
    }
}

TEST_F(StructureDissectTest, ReadPartiallyConnectedStructureWithTimestampMember)
{
    useTimestampElement("ts");
    build("dissectPartialTs", [](const std::vector<unsigned int> &idx) { return idx[0] != 1 && idx[2] != 2; });
    expectRecordTimes(ItemUaSdk::uaToEpicsTime(tsValue, 0));
}

// Structure with members of different kinds:
//   Mixed  { d: Double, s: String, arr: Int32[], o1: Double (optional), o2: Double (optional),
//            in: Inner, skip: Inner, u: Choice, ts: DateTime }
//   Inner  { x: Double, y: Double (optional), z: String }
//   Choice { a: Double | b: Int32 } (union)
// Value: o2, in.y absent; skip.y present; u = a
class StructureDissectMixedTest : public NestedStructureFixture
{
protected:
    enum { iD, iS, iArr, iO1, iO2, iIn, iSkip, iU, iTs };

    static UaStructureField
    field(const char *name, OpcUa_BuiltInType type, bool optional = false, bool array = false)
    {
        UaStructureField f;
        f.setName(name);
        f.setDataTypeId(UaNodeId(type));
        f.setValueType(type);
        f.setArrayType(array ? UaStructureField::ArrayType_Array : UaStructureField::ArrayType_Scalar);
        f.setOptional(optional);
        return f;
    }

    static UaStructureField
    field(const char *name, const UaStructureDefinition &def)
    {
        UaStructureField f;
        f.setName(name);
        f.setDataTypeId(def.dataTypeId());
        f.setStructureDefinition(def);
        f.setArrayType(UaStructureField::ArrayType_Scalar);
        return f;
    }

    static UaStructureDefinition
    definition(const char *name, unsigned int id, bool isUnion = false)
    {
        UaStructureDefinition def;
        def.setName(name);
        def.setDataTypeId(UaNodeId(id, 1));
        def.setBinaryEncodingId(UaNodeId(id + 100, 1));
        def.setUnion(isUnion);
        return def;
    }

    void
    SetUp() override
    {
        NestedStructureFixture::SetUp();

        UaStructureDefinition inner = definition("Inner", 7001);
        inner.addChild(field("x", OpcUaType_Double));
        inner.addChild(field("y", OpcUaType_Double, true));
        inner.addChild(field("z", OpcUaType_String));

        UaStructureDefinition choice = definition("Choice", 7002, true);
        choice.addChild(field("a", OpcUaType_Double));
        choice.addChild(field("b", OpcUaType_Int32));

        UaStructureDefinition mixed = definition("Mixed", 7000);
        mixed.addChild(field("d", OpcUaType_Double));
        mixed.addChild(field("s", OpcUaType_String));
        mixed.addChild(field("arr", OpcUaType_Int32, false, true));
        mixed.addChild(field("o1", OpcUaType_Double, true));
        mixed.addChild(field("o2", OpcUaType_Double, true));
        mixed.addChild(field("in", inner));
        mixed.addChild(field("skip", inner));
        mixed.addChild(field("u", choice));
        mixed.addChild(field("ts", OpcUaType_DateTime));
        defs = {mixed, inner, choice};

        tsValue = UaDateTime::fromTime_t(1700000000);
        UaVariant v;

        UaGenericStructureValue in(inner);
        v.setDouble(3.5);
        EXPECT_EQ(static_cast<OpcUa_StatusCode>(OpcUa_Good), in.setField("x", v));
        v.setString("zz");
        EXPECT_EQ(static_cast<OpcUa_StatusCode>(OpcUa_Good), in.setField("z", v));

        UaGenericStructureValue skip(inner);
        v.setDouble(9.0);
        EXPECT_EQ(static_cast<OpcUa_StatusCode>(OpcUa_Good), skip.setField("x", v));
        v.setDouble(8.0);
        EXPECT_EQ(static_cast<OpcUa_StatusCode>(OpcUa_Good), skip.setField("y", v));
        v.setString("q");
        EXPECT_EQ(static_cast<OpcUa_StatusCode>(OpcUa_Good), skip.setField("z", v));

        UaGenericUnionValue u(choice);
        v.setDouble(4.5);
        u.setValue("a", v); // returns void in older SDK versions; checked through the read value

        UaGenericStructureValue value(mixed);
        v.setDouble(1.5);
        EXPECT_EQ(static_cast<OpcUa_StatusCode>(OpcUa_Good), value.setField(iD, v));
        v.setString("hello");
        EXPECT_EQ(static_cast<OpcUa_StatusCode>(OpcUa_Good), value.setField(iS, v));
        UaInt32Array arr;
        arr.create(3);
        for (OpcUa_Int32 i = 0; i < 3; i++)
            arr[i] = i + 1;
        v.setInt32Array(arr);
        EXPECT_EQ(static_cast<OpcUa_StatusCode>(OpcUa_Good), value.setField(iArr, v));
        v.setDouble(2.5);
        EXPECT_EQ(static_cast<OpcUa_StatusCode>(OpcUa_Good), value.setField(iO1, v));
        EXPECT_EQ(static_cast<OpcUa_StatusCode>(OpcUa_Good), value.setField(iIn, in));
        EXPECT_EQ(static_cast<OpcUa_StatusCode>(OpcUa_Good), value.setField(iSkip, skip));
        EXPECT_EQ(static_cast<OpcUa_StatusCode>(OpcUa_Good), value.setField(iU, u));
        v.setDateTime(tsValue);
        EXPECT_EQ(static_cast<OpcUa_StatusCode>(OpcUa_Good), value.setField(iTs, v));
        setDataValue(value);
    }

    void
    build(const std::string &sessionName)
    {
        buildPaths(sessionName,
                   {{"d"}, {"s"}, {"arr"}, {"o1"}, {"o2"}, {"in", "x"}, {"in", "y"}, {"in", "z"}, {"u", "a"}, {"u", "b"}});
    }

    enum { rD, rS, rArr, rO1, rO2, rInX, rInY, rInZ, rUA, rUB, noOfRecords };

    // Check the values of all records (and their time stamp, if given)
    void
    checkRecords(const epicsTime *expectedTime = nullptr)
    {
        ASSERT_EQ(static_cast<size_t>(noOfRecords), connectors.size());
        epicsFloat64 dbl;
        char str[40];
        epicsInt32 arr[5] = {0};
        epicsUInt32 numRead = 0;

        EXPECT_EQ(0, connectors[rD]->readScalar(&dbl));
        EXPECT_EQ(1.5, dbl);
        EXPECT_EQ(0, connectors[rS]->readScalar(str, sizeof(str)));
        EXPECT_STREQ("hello", str);
        EXPECT_EQ(0, connectors[rArr]->readArray(arr, 5, &numRead));
        EXPECT_EQ(3u, numRead);
        EXPECT_EQ(1, arr[0]);
        EXPECT_EQ(2, arr[1]);
        EXPECT_EQ(3, arr[2]);
        EXPECT_EQ(0, connectors[rO1]->readScalar(&dbl));
        EXPECT_EQ(2.5, dbl);
        EXPECT_EQ(1, connectors[rO2]->readScalar(&dbl)) << "absent optional member: read failure";
        EXPECT_EQ(0, connectors[rInX]->readScalar(&dbl));
        EXPECT_EQ(3.5, dbl);
        EXPECT_EQ(1, connectors[rInY]->readScalar(&dbl)) << "absent optional member: read failure";
        EXPECT_EQ(0, connectors[rInZ]->readScalar(str, sizeof(str)));
        EXPECT_STREQ("zz", str);
        EXPECT_EQ(0, connectors[rUA]->readScalar(&dbl));
        EXPECT_EQ(4.5, dbl);
        EXPECT_EQ(1, connectors[rUB]->readScalar(&dbl)) << "not taken union choice: read failure";

        if (expectedTime)
            for (int r : {rD, rS, rArr, rO1, rInX, rInZ, rUA})
                EXPECT_TRUE(epicsTime(records[r]->time) == *expectedTime) << "time stamp of record " << r;
    }
};

TEST_F(StructureDissectMixedTest, ReadMixedMembers)
{
    build("dissectMixed");
    for (int i = 0; i < 2; i++) {
        update();
        waitForCallbacks();
        checkRecords();
    }
}

TEST_F(StructureDissectMixedTest, ReadMixedMembersWithTimestampMember)
{
    // Updates are held back until the time stamp (last member) has been decoded
    timestampElement = "ts";
    build("dissectMixedTs");
    update();
    waitForCallbacks();
    const epicsTime expected = ItemUaSdk::uaToEpicsTime(tsValue, 0);
    checkRecords(&expected);
}

TEST_F(StructureDissectMixedTest, WriteNestedMember)
{
    build("dissectMixedWrite");
    update();
    waitForCallbacks();
    checkRecords();

    ASSERT_EQ(0, connectors[rInX]->writeScalar(static_cast<epicsFloat64>(42.0)));

    _OpcUa_WriteValue wv;
    OpcUa_WriteValue_Initialize(&wv);
    item->copyAndClearOutgoingData(wv);
    UaVariant out(wv.Value.Value);
    OpcUa_WriteValue_Clear(&wv);

    UaExtensionObject eo;
    ASSERT_EQ(static_cast<OpcUa_StatusCode>(OpcUa_Good), out.toExtensionObject(eo));
    UaGenericStructureValue root(eo, defs[0]);
    OpcUa_StatusCode status;
    OpcUa_Double dbl = -1.0;
    UaString str;

    // Changed member
    EXPECT_EQ(static_cast<OpcUa_StatusCode>(OpcUa_Good), root.genericStructure(iIn).value(0).toDouble(dbl));
    EXPECT_EQ(42.0, dbl);
    // Unchanged members
    EXPECT_EQ(static_cast<OpcUa_StatusCode>(OpcUa_Good), root.value(iD).toDouble(dbl));
    EXPECT_EQ(1.5, dbl);
    EXPECT_STREQ("hello", root.value(iS).toString().toUtf8());
    root.value(iO2, &status);
    EXPECT_EQ(static_cast<OpcUa_StatusCode>(OpcUa_BadNoData), status) << "absent optional member o2";
    root.genericStructure(iIn).value(1, &status);
    EXPECT_EQ(static_cast<OpcUa_StatusCode>(OpcUa_BadNoData), status) << "absent optional member in.y";
    EXPECT_STREQ("zz", root.genericStructure(iIn).value(2).toString().toUtf8());
    EXPECT_EQ(static_cast<OpcUa_StatusCode>(OpcUa_Good), root.genericStructure(iSkip).value(1).toDouble(dbl));
    EXPECT_EQ(8.0, dbl);
    UaGenericUnionValue u = root.genericUnion(iU);
    EXPECT_EQ(1, u.switchValue());
    EXPECT_EQ(static_cast<OpcUa_StatusCode>(OpcUa_Good), u.value().toDouble(dbl));
    EXPECT_EQ(4.5, dbl);
}

// Members of all types that are decoded directly (not through the SDK's generic value)
class StructureDissectSimpleTypesTest : public StructureDissectMixedTest
{
protected:
    void
    SetUp() override
    {
        NestedStructureFixture::SetUp();
        UaStructureDefinition simple = definition("Simple", 7010);
        for (const auto &t : types)
            simple.addChild(field(t.name, t.type));
        defs = {simple};

        UaGenericStructureValue value(simple);
        for (int i = 0; i < static_cast<int>(types.size()); i++) {
            UaVariant v;
            v.setDouble(expectedRead(i));
            EXPECT_EQ(static_cast<OpcUa_StatusCode>(OpcUa_Good), v.changeType(types[i].type, OpcUa_False))
                << "member " << types[i].name;
            EXPECT_EQ(static_cast<OpcUa_StatusCode>(OpcUa_Good), value.setField(i, v)) << "member " << types[i].name;
        }
        setDataValue(value);
    }

    static double expectedRead(int i) { return (types[i].type == OpcUaType_Boolean) ? 1.0 : 10.0 + i; }

    struct SimpleType
    {
        const char *name;
        OpcUa_BuiltInType type;
    };
    static const std::vector<SimpleType> types;
};

const std::vector<StructureDissectSimpleTypesTest::SimpleType> StructureDissectSimpleTypesTest::types = {
    {"bool", OpcUaType_Boolean},
    {"sbyte", OpcUaType_SByte},
    {"byte", OpcUaType_Byte},
    {"int16", OpcUaType_Int16},
    {"uint16", OpcUaType_UInt16},
    {"int32", OpcUaType_Int32},
    {"uint32", OpcUaType_UInt32},
    {"int64", OpcUaType_Int64},
    {"uint64", OpcUaType_UInt64},
    {"float", OpcUaType_Float},
    {"double", OpcUaType_Double},
};

TEST_F(StructureDissectSimpleTypesTest, ReadAndWriteSimpleTypes)
{
    std::vector<std::list<std::string>> paths;
    for (const auto &t : types)
        paths.push_back({t.name});
    buildPaths("dissectSimple", paths);
    update();
    waitForCallbacks();

    for (size_t i = 0; i < types.size(); i++) {
        epicsFloat64 val = -1.0;
        EXPECT_EQ(0, connectors[i]->readScalar(&val)) << "member " << types[i].name;
        EXPECT_EQ(expectedRead(static_cast<int>(i)), val) << "member " << types[i].name;
    }

    // Writing back keeps the OPC UA type of every member (taken from the incoming value)
    for (size_t i = 0; i < types.size(); i++)
        ASSERT_EQ(0, connectors[i]->writeScalar(static_cast<epicsFloat64>(types[i].type == OpcUaType_Boolean ? 0 : 7)))
            << "member " << types[i].name;

    _OpcUa_WriteValue wv;
    OpcUa_WriteValue_Initialize(&wv);
    item->copyAndClearOutgoingData(wv);
    UaVariant out(wv.Value.Value);
    OpcUa_WriteValue_Clear(&wv);

    UaExtensionObject eo;
    ASSERT_EQ(static_cast<OpcUa_StatusCode>(OpcUa_Good), out.toExtensionObject(eo));
    UaGenericStructureValue root(eo, defs[0]);
    for (size_t i = 0; i < types.size(); i++) {
        const UaVariant v = root.value(static_cast<int>(i));
        EXPECT_EQ(types[i].type, v.type()) << "member " << types[i].name;
        OpcUa_Double dbl = -1.0;
        EXPECT_EQ(static_cast<OpcUa_StatusCode>(OpcUa_Good), v.toDouble(dbl)) << "member " << types[i].name;
        EXPECT_EQ(types[i].type == OpcUaType_Boolean ? 0.0 : 7.0, dbl) << "member " << types[i].name;
    }
}

} // namespace
