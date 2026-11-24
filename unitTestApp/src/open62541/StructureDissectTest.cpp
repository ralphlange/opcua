/*************************************************************************\
* Copyright (c) 2026 ITER Organization.
* This module is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
\*************************************************************************/

/*
 *  Author: Ralph Lange <ralph.lange@gmx.de>
 */

/*
 * Tests for dissecting incoming structured data into the data element tree
 * (open62541 client library), using the nested structure fixture
 * (3 levels with 3 members each, i.e. 27 leafs).
 */

#include <vector>

#include <gtest/gtest.h>

#include <open62541/types.h>

#include <errlog.h>

#if UA_OPEN62541_VER_MAJOR == 1 && UA_OPEN62541_VER_MINOR < 3

// The fixture needs open62541 >= 1.3
TEST(StructureDissectTest, UpdateOnChange)
{
    GTEST_SKIP() << "the open62541 structure fixture needs open62541 >= 1.3";
}

#else

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
        ASSERT_TRUE(createTypes());
        createValue();
    }

    // Scalar item: the update option makes no difference
    void checkScalarIgnoresUpdateOption(const std::string &sessionName);

    // Index of the leaf with the path m<a>.m<b>.m<c>
    static unsigned int leafIndex(unsigned int a, unsigned int b, unsigned int c) { return (a * 3 + b) * 3 + c; }
};

TEST_F(StructureDissectTest, ReadNestedStructure)
{
    build("dissectRead");
    ASSERT_EQ(27u, connectors.size());

    for (int i = 0; i < 2; i++) {
        update();
        waitForCallbacks();
        EXPECT_EQ(0u, drainAndCheck()) << "leafs with missing or wrong data after update " << i;
    }
}

TEST_F(StructureDissectTest, UpdateOnChange)
{
    itemUpdate = UpdateOption::change;
    build("dissectOnChange");
    ASSERT_EQ(27u, connectors.size());
    // Draining empty queues logs an error for every leaf
    eltc(0);
    std::vector<double> values;

    update();
    waitForCallbacks();
    EXPECT_EQ(0u, drainAndCheck()) << "leafs with missing or wrong data after first update";

    update();
    waitForCallbacks();
    EXPECT_EQ(0u, drainChanged(values)) << "leafs updated without a change";

    // Change a single leaf (m1.m1.m1)
    const unsigned int changed = leafIndex(1, 1, 1);
    leafValues[changed] = 42.0;
    createValue();
    update();
    waitForCallbacks();
    EXPECT_EQ(1u, drainChanged(values)) << "leafs updated after changing one leaf";
    EXPECT_EQ(42.0, values[changed]);

    // A status change updates all leafs
    update(UA_STATUSCODE_UNCERTAINLASTUSABLEVALUE);
    waitForCallbacks();
    EXPECT_EQ(27u, drainChanged(values)) << "leafs updated after a status change";
    update(UA_STATUSCODE_UNCERTAINLASTUSABLEVALUE);
    waitForCallbacks();
    EXPECT_EQ(0u, drainChanged(values)) << "leafs updated without a status change";
    update();
    waitForCallbacks();
    EXPECT_EQ(27u, drainChanged(values)) << "leafs updated after the status went back";

    // A record writing to its leaf gets the next incoming value (readback)
    ASSERT_EQ(0, connectors[leafIndex(2, 0, 1)]->writeScalar(static_cast<epicsFloat64>(7.0)));
    update();
    waitForCallbacks();
    EXPECT_EQ(1u, drainChanged(values)) << "leafs updated after a record wrote";
    EXPECT_EQ(1.0, values[leafIndex(2, 0, 1)]) << "readback of the written leaf";

    // After a connection loss, all leafs get the next incoming value
    item->setIncomingEvent(ProcessReason::connectionLoss);
    waitForCallbacks();
    drainChanged(values);
    update();
    waitForCallbacks();
    EXPECT_EQ(27u, drainChanged(values)) << "leafs updated after a connection loss";

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
        EXPECT_EQ(27u, drainChanged(values)) << "leafs updated by unchanged monitor update " << i;
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
    EXPECT_EQ(1u, drainChanged(values)) << "leafs updated without a change";
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
    EXPECT_EQ(26u, drainChanged(values)) << "leafs updated without a change";
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
    EXPECT_EQ(0u, drainChanged(values)) << "leafs updated by an unchanged monitor update";

    update(UA_STATUSCODE_GOOD, ProcessReason::readComplete);
    waitForCallbacks();
    EXPECT_EQ(27u, drainChanged(values)) << "leafs updated by unchanged read data";

    update();
    waitForCallbacks();
    EXPECT_EQ(27u, drainChanged(values)) << "leafs updated by the first monitor update after a read";
    update();
    waitForCallbacks();
    EXPECT_EQ(0u, drainChanged(values)) << "leafs updated by a second unchanged monitor update";

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
    EXPECT_EQ(0u, drainChanged(values)) << "leafs updated without a change";

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
    EXPECT_EQ(1u, drainChanged(values)) << "leafs updated without a change";
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
        updateScalar(5.0);
        waitForCallbacks();
        EXPECT_EQ(1u, drainChanged(values)) << "record not updated by unchanged monitor update " << i;
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

} // namespace

#endif // open62541 >= 1.3
