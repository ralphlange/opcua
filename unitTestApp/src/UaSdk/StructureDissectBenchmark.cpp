/*************************************************************************\
* Copyright (c) 2026 ITER Organization.
* This module is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
\*************************************************************************/

/*
 *  Author: Ralph Lange <ralph.lange@gmx.de>
 */

/*
 * Benchmark for dissecting incoming structured data into the data element tree.
 *
 * A single item receives a nested structure (default: about 100,000 leaf
 * elements, depth 5, i.e. fan-out 10 and 11,111 structure nodes). Every leaf
 * element is connected to its own (fake) record, as in an IOC that maps every
 * structure member to a record.
 *
 * For a given depth, the fan-out of each level is chosen so that the number of
 * leafs is as close as possible to the target (e.g. depth 2: 316 x 316 = 99,856;
 * depth 10: 3 and 4 members per level, 104,976 leafs), so that different depths
 * can be compared at about the same amount of data.
 *
 * Measured is ItemUaSdk::setIncomingData() for one update:
 *   - the first update (includes structure definition lookups and creating
 *     the child element maps)
 *   - the following updates (steady state)
 * The incoming queues of all leaf elements are drained between updates
 * (outside the timed region), so that every update takes the regular path
 * of queueing the value and requesting record processing.
 *
 * The structure definitions are served by a stub UaSession, so no OPC UA
 * server is needed.
 *
 * Environment variables to change the parameters:
 *   DEVOPCUA_BENCH_LEAFS       target number of leaf elements        (100000)
 *   DEVOPCUA_BENCH_DEPTH       number of nested structure levels     (5)
 *   DEVOPCUA_BENCH_FANOUT      fixed number of members per level, overrides
 *                              DEVOPCUA_BENCH_LEAFS (fan-out^depth leafs)
 *   DEVOPCUA_BENCH_ITERATIONS  number of steady state updates        (10)
 */

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "NestedStructureFixture.h"

namespace {

using Clock = std::chrono::steady_clock;

unsigned int
envOrDefault(const char *name, unsigned int dflt)
{
    const char *val = std::getenv(name);
    if (val && *val) {
        long v = std::strtol(val, nullptr, 10);
        if (v > 0)
            return static_cast<unsigned int>(v);
    }
    return dflt;
}

double
msSince(const Clock::time_point &start)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

class StructureDissectBenchmark : public NestedStructureFixture
{
protected:
    void
    SetUp() override
    {
        NestedStructureFixture::SetUp();
        unsigned int target = envOrDefault("DEVOPCUA_BENCH_LEAFS", 100000);
        unsigned int fixed = envOrDefault("DEVOPCUA_BENCH_FANOUT", 0);
        unsigned int levels = envOrDefault("DEVOPCUA_BENCH_DEPTH", 5);
        iterations = envOrDefault("DEVOPCUA_BENCH_ITERATIONS", 10);
        setShape(spreadFanout(target, levels, fixed));
    }

    unsigned int iterations = 10;
};

TEST_F(StructureDissectBenchmark, NestedStructure)
{
    auto t0 = Clock::now();
    createDefinitions();
    createValue();
    double tEncode = msSince(t0);

    t0 = Clock::now();
    build("bench");
    double tSetup = msSince(t0);
    ASSERT_EQ(noOfLeafs, connectors.size());

    // First update: structure definition lookups and child maps
    t0 = Clock::now();
    update();
    double tFirst = msSince(t0);
    waitForCallbacks();
    EXPECT_EQ(0u, drainAndCheck()) << "leafs with missing or wrong data after first update";

    // Steady state
    std::vector<double> times;
    unsigned int bad = 0;
    for (unsigned int i = 0; i < iterations; i++) {
        t0 = Clock::now();
        update();
        times.push_back(msSince(t0));
        waitForCallbacks();
        bad += drainAndCheck();
    }
    EXPECT_EQ(0u, bad) << "leafs with missing or wrong data after steady state updates";

    double sum = 0.0;
    for (double t : times)
        sum += t;
    double mean = times.empty() ? 0.0 : sum / times.size();
    std::sort(times.begin(), times.end());
    double median = times.empty() ? 0.0 : times[times.size() / 2];

    std::cout << std::fixed << std::setprecision(2)
              << "\nStructure dissection benchmark (UA SDK)\n"
              << "  structure:      depth " << depth << ", fan-out " << shapeString() << " -> " << noOfLeafs
              << " leafs, " << noOfNodes << " nodes\n"
              << "  encoded value:  " << tEncode << " ms (definitions + value, not part of dissection)\n"
              << "  tree setup:     " << tSetup << " ms (" << noOfLeafs << " records/elements)\n"
              << "  first update:   " << tFirst << " ms (incl. definition lookups and mapping)\n";
    if (!times.empty())
        std::cout << "  steady state:   " << iterations << " updates: mean " << mean << " ms, median " << median
                  << " ms, min " << times.front() << " ms, max " << times.back() << " ms\n"
                  << "                  " << std::setprecision(1) << mean * 1e6 / noOfLeafs
                  << " ns per leaf element\n";
    std::cout << std::endl;
}

} // namespace
