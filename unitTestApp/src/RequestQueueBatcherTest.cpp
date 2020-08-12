/*************************************************************************\
* Copyright (c) 2020 ITER Organization.
* This module is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
\*************************************************************************/

/*
 *  Author: Ralph Lange <ralph.lange@gmx.de>
 */

#include <memory>
#include <vector>
#include <cstdlib>
#include <ctime>
#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <epicsTime.h>
#include <epicsEvent.h>
#include <epicsMutex.h>
#include <menuPriority.h>

#include "devOpcua.h"
#include "RequestQueueBatcher.h"

#define TAG_FINISHED UINT_MAX

namespace {

using namespace DevOpcua;
using namespace testing;

class TestCargo {
public:
    TestCargo(unsigned int val)
        : tag(val) {}
    unsigned int tag;
};

class TestReceptor : public RequestConsumer<TestCargo> {
public:
    TestReceptor() {}
    virtual ~TestReceptor() {}
    virtual void processRequests(std::vector<std::shared_ptr<TestCargo>> &batch) override;
    void reset() {
        noOfBatches = 0;
        batchSizes.clear();
        batchData.clear();
    }

    epicsEvent finished;
    unsigned int noOfBatches;
    std::vector<unsigned int> batchSizes;
    std::vector<std::vector<unsigned int>> batchData;
};

void
TestReceptor::processRequests(std::vector<std::shared_ptr<TestCargo>> &batch)
{
    noOfBatches++;
    batchSizes.push_back(batch.size());
    std::vector<unsigned int> data;
    bool done = false;
    for (const auto &p : batch) {
        if (p->tag == TAG_FINISHED) {
            done = true;
        } else {
            data.push_back(p->tag);
        }
    }
    batchData.push_back(std::move(data));
    if (done)
        finished.signal();
}

TestReceptor dump;

// Fixture for testing RequestQueueBatcher queues only (no worker thread)
class RQBQueuePushOnlyTest : public ::testing::Test {
protected:
    RQBQueuePushOnlyTest()
        : b0("test batcher 0", dump, 0, false)
        , c0(std::make_shared<TestCargo>(0))
        , c1(std::make_shared<TestCargo>(1))
        , c2(std::make_shared<TestCargo>(2))
    {
        b0.pushRequest(c0, menuPriorityLOW);
        b0.pushRequest(c1, menuPriorityMEDIUM);
        b0.pushRequest(c2, menuPriorityHIGH);
    }

    RequestQueueBatcher<TestCargo> b0;
    std::shared_ptr<TestCargo> c0, c1, c2;
};

TEST_F(RQBQueuePushOnlyTest, status_OncePerQueue_SizesRefCountsCorrect) {
    EXPECT_EQ(b0.size(menuPriorityLOW), 1lu) << "Queue LOW of size 1 returns wrong size";
    EXPECT_EQ(b0.size(menuPriorityMEDIUM), 1lu) << "Queue MEDIUM of size 1 returns wrong size";
    EXPECT_EQ(b0.size(menuPriorityHIGH), 1lu) << "Queue HIGH of size 1 returns wrong size";
    EXPECT_EQ(c0.use_count(), 2l) << "c0 does not have the correct reference count";
    EXPECT_EQ(c1.use_count(), 2l) << "c1 does not have the correct reference count";
    EXPECT_EQ(c2.use_count(), 2l) << "c2 does not have the correct reference count";
}

TEST_F(RQBQueuePushOnlyTest, status_TwicePerQueue_SizesRefCountsCorrect) {
    b0.pushRequest(c0, menuPriorityLOW);
    b0.pushRequest(c1, menuPriorityMEDIUM);
    b0.pushRequest(c2, menuPriorityHIGH);

    EXPECT_EQ(b0.size(menuPriorityLOW), 2lu) << "Queue[LOW] returns wrong size";
    EXPECT_EQ(b0.size(menuPriorityMEDIUM), 2lu) << "Queue[MEDIUM] returns wrong size";
    EXPECT_EQ(b0.size(menuPriorityHIGH), 2lu) << "Queue[HIGH] returns wrong size";
    EXPECT_EQ(c0.use_count(), 3l) << "c0 does not have the correct reference count";
    EXPECT_EQ(c1.use_count(), 3l) << "c1 does not have the correct reference count";
    EXPECT_EQ(c2.use_count(), 3l) << "c2 does not have the correct reference count";
}

// Fixture for testing the batcher (with worker thread)
class RQBBatcherTest : public ::testing::Test {
protected:
    RQBBatcherTest()
        : nextTag{2000000, 1000000, 0} // so that any batch must end up sorted
        , b0("test batcher 0", dump, 0, false)
        , b10("test batcher 10", dump, 10, false)
        , b100("test batcher 100", dump, 100)
    {
        dump.reset();
        allSentCargo.clear();
    }

    virtual void TearDown() override
    {
        // use_count() = 1 for elements of allCargo => no reference lost
        unsigned int wrong = 0;
        for ( const auto &p : allSentCargo ) {
            if (p.use_count() != 1) wrong++;
        }
        EXPECT_EQ(wrong, 0u) << "members of cargo have use_count() not 1 after finish";

        // Strict PQ means each batch is sorted HIGH - MEDIUM - LOW and in the order of the queues
        for ( auto data : dump.batchData ) {
            if (data.size() > 1) {
                for ( unsigned int i = 0; i < data.size()-1; i++ ) {
                    EXPECT_LT(data[i], data[i+1]) << "Requests inside a batch out of order";
                }
            }
        }

    }

    RQBBatcherTest &fixture() { return *this; }

    void addRequests(RequestQueueBatcher<TestCargo> &b, const menuPriority priority, const unsigned int no)
    {
        Guard G(lock);
        for (unsigned int i = 0; i < no; i++) {
            auto cargo = std::make_shared<TestCargo>(nextTag[priority]++);
            b.pushRequest(cargo, priority);
            allSentCargo.emplace_back(std::move(cargo));
        }
    }

    class Adder : public epicsThreadRunable
    {
    public:
        Adder(RQBBatcherTest &fix, RequestQueueBatcher<TestCargo> &b, const unsigned int no)
            : no(no)
            , parent(fix)
            , b(b)
            , t(*this, "adder", epicsThreadGetStackSize(epicsThreadStackSmall), epicsThreadPriorityMedium)
        {
            // Seed random number generator
            srand (static_cast<unsigned int>(time(nullptr)));
            t.start();
        }

        ~Adder() override { t.exitWait(); }

        virtual void run () override {
            for (unsigned int i = 0; i < no; i++) {
                parent.addRequests(b, static_cast<menuPriority>(rand() % 3), 1);
                if (!i % 100) epicsThreadSleep(0.0); // allow context switch
            }
            done.signal();
        }

        epicsEvent done;
    private:
        unsigned int no;
        RQBBatcherTest &parent;
        RequestQueueBatcher<TestCargo> &b;
        epicsThread t;
    };

    void finish_wait(RequestQueueBatcher<TestCargo> &b)
    {
        b.pushRequest(std::make_shared<TestCargo>(TAG_FINISHED), menuPriorityLOW);
        dump.finished.wait();
        epicsThreadSleep(0.001); // to let the batcher drop the reference
    }

    epicsMutex lock;
    unsigned int nextTag[menuPriority_NUM_CHOICES];
    RequestQueueBatcher<TestCargo> b0, b10, b100;
    std::vector<std::shared_ptr<TestCargo>> allSentCargo;
};

TEST_F(RQBBatcherTest, status_SizeUnlimited_90RequestsInOneBatch) {
    addRequests(b0, menuPriorityLOW, 15);
    addRequests(b0, menuPriorityMEDIUM, 15);
    addRequests(b0, menuPriorityLOW, 15);
    addRequests(b0, menuPriorityHIGH, 15);
    addRequests(b0, menuPriorityMEDIUM, 15);
    addRequests(b0, menuPriorityHIGH, 15);

    EXPECT_EQ(b0.size(menuPriorityLOW), 30u) << "Queue[LOW] returns wrong size";
    EXPECT_EQ(b0.size(menuPriorityMEDIUM), 30u) << "Queue[MEDIUM] returns wrong size";
    EXPECT_EQ(b0.size(menuPriorityHIGH), 30u) << "Queue[HIGH] of returns wrong size";

    b0.startWorker();
    finish_wait(b0);

    EXPECT_TRUE(b0.empty(menuPriorityLOW)) << "Queue[LOW] not empty";
    EXPECT_TRUE(b0.empty(menuPriorityMEDIUM)) << "Queue[MEDIUM] not empty";
    EXPECT_TRUE(b0.empty(menuPriorityHIGH)) << "Queue[HIGH] not empty";

    EXPECT_EQ(allSentCargo.size(), 90u) << "Not all cargo sent";
    EXPECT_EQ(dump.noOfBatches, 1u) << "Cargo not processed in single batch";
    EXPECT_EQ(dump.batchSizes[0], 91u) << "Batch[0] did not contain all cargo";
}

TEST_F(RQBBatcherTest, status_Size10_90RequestsManyBatches) {
    addRequests(b10, menuPriorityLOW, 30);
    addRequests(b10, menuPriorityMEDIUM, 30);
    addRequests(b10, menuPriorityHIGH, 30);

    b10.startWorker();
    finish_wait(b10);

    EXPECT_EQ(b10.size(menuPriorityLOW), 0u) << "Queue[LOW] returns wrong size";
    EXPECT_EQ(b10.size(menuPriorityMEDIUM), 0u) << "Queue[MEDIUM] returns wrong size";
    EXPECT_EQ(b10.size(menuPriorityHIGH), 0u) << "Queue[HIGH] of returns wrong size";

    EXPECT_EQ(allSentCargo.size(), 90u) << "Not all cargo sent";
    EXPECT_EQ(dump.noOfBatches, 10u) << "Cargo not processed in 10 batches";
    EXPECT_THAT(dump.batchSizes, Each(Le(10u))) << "Some batches are exceeding the size limit";
}

TEST_F(RQBBatcherTest, status_Size100_100kRequests4ThreadsManyBatches) {
    Adder a1(fixture(), b100, 25000);
    Adder a2(fixture(), b100, 25000);
    Adder a3(fixture(), b100, 25000);
    Adder a4(fixture(), b100, 25000);
    a1.done.wait();
    a2.done.wait();
    a3.done.wait();
    a4.done.wait();

    // b100 is auto-started
    finish_wait(b100);

    EXPECT_EQ(b100.size(menuPriorityLOW), 0lu) << "Queue[LOW] returns wrong size";
    EXPECT_EQ(b100.size(menuPriorityMEDIUM), 0lu) << "Queue[MEDIUM] returns wrong size";
    EXPECT_EQ(b100.size(menuPriorityHIGH), 0lu) << "Queue[HIGH] of returns wrong size";

    EXPECT_EQ(allSentCargo.size(), 100000u) << "Not all cargo sent";
    EXPECT_GE(dump.noOfBatches, 1000u) << "Cargo processed < 1000 batches";
    EXPECT_THAT(dump.batchSizes, Each(Le(100u))) << "Some batches are exceeding the size limit";
}

} // namespace
