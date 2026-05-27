/*************************************************************************\
* Copyright (c) 2026 ITER Organization.
* This module is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
\*************************************************************************/

#include <gtest/gtest.h>
#include <memory>
#include <sstream>
#include <vector>
#include <algorithm>

#include "Stats.h"

namespace {

using namespace DevOpcua;

TEST(StatsSlidingAverageTest, InitialState) {
    StatsSlidingAverage sa(5);
    EXPECT_DOUBLE_EQ(sa.getAverage(), 0.0);
    EXPECT_DOUBLE_EQ(sa.getMedian(), 0.0);
    EXPECT_DOUBLE_EQ(sa.getMin(), 0.0);
    EXPECT_DOUBLE_EQ(sa.getMax(), 0.0);
}

TEST(StatsSlidingAverageTest, RecordAndAverage) {
    StatsSlidingAverage sa(5);
    sa.record(10.0);
    EXPECT_DOUBLE_EQ(sa.getAverage(), 10.0);
    sa.record(20.0);
    EXPECT_DOUBLE_EQ(sa.getAverage(), 15.0);
    sa.record(30.0);
    EXPECT_DOUBLE_EQ(sa.getAverage(), 20.0);
}

TEST(StatsSlidingAverageTest, Median) {
    StatsSlidingAverage sa(5);
    sa.record(10.0);
    EXPECT_DOUBLE_EQ(sa.getMedian(), 10.0);
    sa.record(30.0);
    EXPECT_DOUBLE_EQ(sa.getMedian(), 20.0);
    sa.record(20.0);
    EXPECT_DOUBLE_EQ(sa.getMedian(), 20.0);
    sa.record(40.0);
    EXPECT_DOUBLE_EQ(sa.getMedian(), 25.0);
    sa.record(50.0);
    EXPECT_DOUBLE_EQ(sa.getMedian(), 30.0);
}

TEST(StatsSlidingAverageTest, MinMax) {
    StatsSlidingAverage sa(5);
    sa.record(10.0);
    sa.record(5.0);
    sa.record(15.0);
    EXPECT_DOUBLE_EQ(sa.getMin(), 5.0);
    EXPECT_DOUBLE_EQ(sa.getMax(), 15.0);
}

TEST(StatsSlidingAverageTest, CircularBuffer) {
    StatsSlidingAverage sa(3);
    sa.record(10.0);
    sa.record(20.0);
    sa.record(30.0);
    EXPECT_DOUBLE_EQ(sa.getAverage(), 20.0);

    sa.record(40.0); // Should replace 10.0
    // Buffer: [40, 20, 30]
    EXPECT_DOUBLE_EQ(sa.getAverage(), 30.0);
    EXPECT_DOUBLE_EQ(sa.getMin(), 20.0);
    EXPECT_DOUBLE_EQ(sa.getMax(), 40.0);
    EXPECT_DOUBLE_EQ(sa.getMedian(), 30.0);

    sa.record(50.0); // Should replace 20.0
    // Buffer: [40, 50, 30]
    EXPECT_DOUBLE_EQ(sa.getAverage(), 40.0);
    EXPECT_DOUBLE_EQ(sa.getMedian(), 40.0);
}

TEST(StatsSlidingAverageTest, Reset) {
    StatsSlidingAverage sa(5);
    sa.record(10.0);
    sa.record(20.0);
    sa.reset();
    EXPECT_DOUBLE_EQ(sa.getAverage(), 0.0);
    EXPECT_DOUBLE_EQ(sa.getMedian(), 0.0);
    sa.record(30.0);
    EXPECT_DOUBLE_EQ(sa.getAverage(), 30.0);
}

TEST(StatsManagerTest, SlidingAverageIntegration) {
    StatsManager &mgr = StatsManager::getInstance();
    auto sa = mgr.getSlidingAverage("test.sa", 5);
    ASSERT_NE(sa, nullptr);

    sa->record(100.0);

    std::stringstream ss;
    mgr.report(ss, 1, "test.sa");
    std::string report = ss.str();
    EXPECT_NE(report.find("test.sa:"), std::string::npos);
    EXPECT_NE(report.find("Average: 100"), std::string::npos);
    EXPECT_NE(report.find("Median:  100"), std::string::npos);

    mgr.reset("test.sa");
    EXPECT_DOUBLE_EQ(sa->getAverage(), 0.0);
}

TEST(StatsHistogramTest, ResetFix) {
    StatsHistogram hist({10.0, 20.0});
    hist.record(5.0);
    hist.record(15.0);
    hist.record(25.0);

    std::stringstream ss1;
    hist.print(ss1);
    EXPECT_NE(ss1.str().find(": 1"), std::string::npos);

    hist.reset();
    std::stringstream ss2;
    hist.print(ss2);
    // After reset, all counts should be 0.
    EXPECT_EQ(ss2.str().find(": 1"), std::string::npos);
    EXPECT_NE(ss2.str().find(": 0"), std::string::npos);
}

} // namespace
