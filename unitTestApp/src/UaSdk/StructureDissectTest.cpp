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
 */

#include <gtest/gtest.h>

#include <epicsTime.h>

#include <uaextensionobject.h>
#include <uagenericstructurevalue.h>
#include <uavariant.h>

#include "NestedStructureFixture.h"

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
    item->copyAndClearOutgoingData(wv);
    UaVariant out(wv.Value.Value);
    OpcUa_WriteValue_Clear(&wv);

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

} // namespace
