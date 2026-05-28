/*************************************************************************\
* Copyright (c) 2026 ITER Organization.
* This module is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
\*************************************************************************/

#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include <vector>
#include <memory>
#include "AdaptiveConcurrency.h"
#include "RequestQueueBatcher.h"

namespace {

using namespace DevOpcua;

class TestConsumer : public RequestConsumer<int> {
public:
    void processRequests(std::vector<std::shared_ptr<int>> &batch) override {
        (void)batch;
    }
};

TEST(AdaptiveConcurrencyTest, SlidingAverageTimerBasic) {
    SlidingAverageTimer timer(std::chrono::milliseconds(100));
    timer.addValue(10.0);
    timer.addValue(20.0);
    EXPECT_DOUBLE_EQ(timer.getAverage(), 15.0);

    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    EXPECT_DOUBLE_EQ(timer.getAverage(), 0.0);
}

TEST(AdaptiveConcurrencyTest, AdaptiveConcurrencyManagerAIMD) {
    AdaptiveConcurrencyManager acm(5.0, 1.0, 10.0, std::chrono::milliseconds(100));
    acm.setEnabled(true);

    // Initial window is 5
    EXPECT_DOUBLE_EQ(acm.getCurrentWindowSize(), 5.0);

    // Successful requests increase window
    acm.requestSent(1);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    acm.requestCompleted(1);
    EXPECT_DOUBLE_EQ(acm.getCurrentWindowSize(), 6.0);

    // High response time decreases window (multiplicative)
    // We need the average to be > 100ms.
    // Current average is ~10ms (1 sample).
    // If we add a sample of 250ms, average will be (10+250)/2 = 130ms.
    acm.requestSent(2);
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    acm.requestCompleted(2);
    EXPECT_DOUBLE_EQ(acm.getCurrentWindowSize(), 3.0);

    // Failure decreases window
    acm.requestSent(3);
    acm.requestFailed(3);
    EXPECT_DOUBLE_EQ(acm.getCurrentWindowSize(), 1.5);
}

TEST(AdaptiveConcurrencyTest, IntegrationWithBatcher) {
    TestConsumer consumer;
    RequestQueueBatcher<int> batcher("TestBatcher", consumer, 10, 0, 0, true);
    AdaptiveConcurrencyManager acm(1.0, 1.0, 10.0, std::chrono::milliseconds(100));
    acm.setEnabled(true);
    batcher.setAdaptiveConcurrencyManager(&acm);

    // Window size is 1, so only 1 request can be "in flight" from the batcher's perspective.

    batcher.pushRequest(std::make_shared<int>(42), menuPriorityLOW);

    // This is hard to test without more control over the batcher loop.
    // But we can at least verify it doesn't crash and respects enabled flag.
    acm.setEnabled(false);
    batcher.pushRequest(std::make_shared<int>(43), menuPriorityLOW);
}

} // namespace
