/*************************************************************************\
* Copyright (c) 2026 ITER Organization.
* This module is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
\*************************************************************************/

/*
 *  Author: Ralph Lange <ralph.lange@gmx.de>
 */

#include <sstream>
#include <string>

#include <gtest/gtest.h>

#include "DataChangeStats.h"

namespace {

using namespace DevOpcua;

std::string
shown(const DataChangeStats &stats)
{
    std::ostringstream os;
    stats.show(os);
    return os.str();
}

TEST(DataChangeStatsTest, EmptyShowsZeros)
{
    DataChangeStats stats;
    EXPECT_EQ(" callbacks=0 notifications=0 avgMs=0.000 maxMs=0.000 lastMs=0.000 overInterval=0", shown(stats));
}

TEST(DataChangeStatsTest, RecordsCallbacks)
{
    DataChangeStats stats;
    int nameCalls = 0;
    // 2 ms, 3 items, interval 100 ms, slowest item 1.5 ms
    stats.record(2000000, 3, 100.0, 1500000, [&]() { nameCalls++; return std::string("itemA"); });
    // 300 ms (longer than the interval), 1 item
    stats.record(300000000, 1, 100.0, 300000000, [&]() { nameCalls++; return std::string("itemB"); });
    // 4 ms, slowest item 1 ms (not a new maximum: name not needed)
    stats.record(4000000, 2, 100.0, 1000000, [&]() { nameCalls++; return std::string("itemC"); });

    EXPECT_EQ(" callbacks=3 notifications=6 avgMs=102.000 maxMs=300.000 lastMs=4.000 overInterval=1"
              " slowestItemMs=300.000(itemB)",
              shown(stats));
    EXPECT_EQ(2, nameCalls) << "item name created only for a new slowest item";
}

TEST(DataChangeStatsTest, CallbackWithoutNotifications)
{
    DataChangeStats stats;
    stats.record(1000000, 0, 100.0, 0, []() { return std::string("never"); });
    EXPECT_EQ(" callbacks=1 notifications=0 avgMs=1.000 maxMs=1.000 lastMs=1.000 overInterval=0",
              shown(stats));
}

TEST(DataChangeStatsTest, Reset)
{
    DataChangeStats stats;
    stats.record(2000000, 3, 100.0, 1500000, []() { return std::string("itemA"); });
    stats.reset();
    EXPECT_EQ(" callbacks=0 notifications=0 avgMs=0.000 maxMs=0.000 lastMs=0.000 overInterval=0", shown(stats));
}

} // namespace
