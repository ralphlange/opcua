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
#include <thread>
#include <chrono>

#include "Stats.h"

namespace {

using namespace DevOpcua;

TEST(StatsCounterTest, InitialState) {
    StatsCounter counter;
    EXPECT_EQ(counter.get(), 0);
}

TEST(StatsCounterTest, Increment) {
    StatsCounter counter;
    counter.increment();
    EXPECT_EQ(counter.get(), 1);
    counter.increment(5);
    EXPECT_EQ(counter.get(), 6);
}

TEST(StatsCounterTest, Set) {
    StatsCounter counter;
    counter.set(42);
    EXPECT_EQ(counter.get(), 42);
}

TEST(StatsCounterTest, Reset) {
    StatsCounter counter;
    counter.set(100);
    counter.reset();
    EXPECT_EQ(counter.get(), 0);
}

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

TEST(StatsHistogramTest, RecordAndDistribution) {
    StatsHistogram hist({10.0, 20.0});
    hist.record(5.0);  // bucket 0
    hist.record(10.0); // bucket 0 (inclusive upper bound)
    hist.record(15.0); // bucket 1
    hist.record(20.0); // bucket 1
    hist.record(25.0); // bucket 2

    std::stringstream ss;
    hist.print(ss);
    std::string s = ss.str();
    // Bucket <= 10: 2
    // Bucket > 10 and <= 20: 2
    // Bucket > 20: 1
    EXPECT_NE(s.find("<=         10 us: 2"), std::string::npos);
    EXPECT_NE(s.find(">         10 and <=         20 us: 2"), std::string::npos);
    EXPECT_NE(s.find(">         20 us: 1"), std::string::npos);
}

TEST(StatsHistogramTest, Reset) {
    StatsHistogram hist({10.0, 20.0});
    hist.record(5.0);
    hist.record(15.0);
    hist.record(25.0);

    hist.reset();
    std::stringstream ss;
    hist.print(ss);
    std::string s = ss.str();
    EXPECT_NE(s.find("<=         10 us: 0"), std::string::npos);
    EXPECT_NE(s.find(">         10 and <=         20 us: 0"), std::string::npos);
    EXPECT_NE(s.find(">         20 us: 0"), std::string::npos);
}

TEST(StatsExecTimeTest, RecordAndAverage) {
    StatsExecTime et({10.0, 20.0});
    et.record(10000); // 10 us
    et.record(20000); // 20 us

    EXPECT_EQ(et.getCount(), 2);
    EXPECT_EQ(et.getTotalExecutionTimeNs(), 30000);
    EXPECT_DOUBLE_EQ(et.getAverageTimeUs(), 15.0);
}

TEST(StatsExecTimeTest, Reset) {
    StatsExecTime et({10.0, 20.0});
    et.record(10000);
    et.reset();
    EXPECT_EQ(et.getCount(), 0);
    EXPECT_EQ(et.getTotalExecutionTimeNs(), 0);
    EXPECT_DOUBLE_EQ(et.getAverageTimeUs(), 0.0);
}

TEST(StatsTimerTest, Measure) {
    auto et = std::make_shared<StatsExecTime>(std::vector<double>{1000.0});
    {
        StatsTimer timer(et);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_EQ(et->getCount(), 1);
    EXPECT_GT(et->getTotalExecutionTimeNs(), 9000000); // At least 9ms
}

TEST(StatsManagerTest, CounterIntegration) {
    StatsManager &mgr = StatsManager::getInstance();
    auto c = mgr.getCounter("test.counter");
    ASSERT_NE(c, nullptr);
    c->increment(10);
    EXPECT_EQ(mgr.getCounter("test.counter")->get(), 10);

    std::stringstream ss;
    mgr.report(ss, 0, "test.counter");
    EXPECT_NE(ss.str().find("test.counter: 10"), std::string::npos);
}

TEST(StatsManagerTest, ExecutionStatsIntegration) {
    StatsManager &mgr = StatsManager::getInstance();
    auto et = mgr.getExecutionStats("test.exec", {100.0});
    ASSERT_NE(et, nullptr);
    et->record(50000); // 50 us

    std::stringstream ss;
    mgr.report(ss, 0, "test.exec");
    EXPECT_NE(ss.str().find("test.exec:"), std::string::npos);
    EXPECT_NE(ss.str().find("Average Time: 50 us"), std::string::npos);
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

TEST(StatsManagerTest, ResetAll) {
    StatsManager &mgr = StatsManager::getInstance();
    auto c = mgr.getCounter("test.c1");
    auto et = mgr.getExecutionStats("test.e1");
    auto sa = mgr.getSlidingAverage("test.s1");

    c->set(1);
    et->record(1000);
    sa->record(10.0);

    mgr.reset_all();

    EXPECT_EQ(c->get(), 0);
    EXPECT_EQ(et->getCount(), 0);
    EXPECT_DOUBLE_EQ(sa->getAverage(), 0.0);
}

TEST(StatsManagerTest, ReportPattern) {
    StatsManager &mgr = StatsManager::getInstance();
    mgr.getCounter("a.1")->set(1);
    mgr.getCounter("a.2")->set(2);
    mgr.getCounter("b.1")->set(3);

    std::stringstream ss;
    mgr.report(ss, 0, "a.*");
    std::string s = ss.str();
    EXPECT_NE(s.find("a.1: 1"), std::string::npos);
    EXPECT_NE(s.find("a.2: 2"), std::string::npos);
    EXPECT_EQ(s.find("b.1: 3"), std::string::npos);
}

} // namespace
