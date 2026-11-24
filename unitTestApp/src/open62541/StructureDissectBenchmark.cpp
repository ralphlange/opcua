/*************************************************************************\
* Copyright (c) 2026 ITER Organization.
* This module is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
\*************************************************************************/

/*
 *  Author: Ralph Lange <ralph.lange@gmx.de>
 */

/*
 * Benchmark for dissecting incoming structured data into the data element tree
 * (open62541 client library). Counterpart of the UA SDK benchmark with the same
 * name and parameters.
 *
 * A single item receives a nested structure, with every leaf element connected
 * to its own (fake) record. The structure types (one per level, members m0, m1,
 * ...; innermost members are doubles) are described as open62541 custom data
 * types, like the ones the client creates from the server's type dictionary.
 *
 * With open62541, the client library decodes the ExtensionObject into native
 * memory while decoding the service response; the device support only maps the
 * members. For a fair comparison with the UA SDK (where decoding is part of the
 * dissection), every update is measured as
 *   - decoding the binary encoded value (UA_decodeBinary, custom types)
 *   - dissection: ItemOpen62541::setIncomingData()
 * The leaf queues are drained between updates (outside the timed region).
 *
 * The setup (types, value, item and records) is in NestedStructureFixture.h.
 * Because of the limits of open62541 type descriptions (see there),
 * the default target is 8000 leafs.
 *
 * Environment variables to change the parameters:
 *   DEVOPCUA_BENCH_LEAFS       target number of leaf elements        (8000)
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

#include <open62541/types.h>

#if UA_OPEN62541_VER_MAJOR == 1 && UA_OPEN62541_VER_MINOR < 3

// open62541 before 1.3 describes member types by index (not pointer)
// and has no UA_DecodeBinaryOptions
TEST(StructureDissectBenchmark, NestedStructure)
{
    GTEST_SKIP() << "the open62541 benchmark needs open62541 >= 1.3";
}

#else

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

struct Stats
{
    double mean = 0.0, median = 0.0, min = 0.0, max = 0.0;
};

Stats
statsOf(std::vector<double> v)
{
    Stats s;
    if (v.empty())
        return s;
    double sum = 0.0;
    for (double t : v)
        sum += t;
    s.mean = sum / v.size();
    std::sort(v.begin(), v.end());
    s.median = v[v.size() / 2];
    s.min = v.front();
    s.max = v.back();
    return s;
}

class StructureDissectBenchmark : public NestedStructureFixture
{
protected:
    void
    SetUp() override
    {
        NestedStructureFixture::SetUp();
        unsigned int target = envOrDefault("DEVOPCUA_BENCH_LEAFS", 8000);
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
    if (!createTypes())
        GTEST_SKIP() << "structure too large for open62541 type descriptions";
    createValue();
    double tEncode = msSince(t0);

    t0 = Clock::now();
    build("bench");
    double tSetup = msSince(t0);
    ASSERT_EQ(noOfLeafs, connectors.size());

    // First update (incl. mapping)
    UA_DataValue dv;
    t0 = Clock::now();
    decode(dv);
    double tFirstDecode = msSince(t0);
    t0 = Clock::now();
    item->setIncomingData(dv, ProcessReason::incomingData); // takes ownership of the value
    double tFirst = msSince(t0);
    fence->wait();
    EXPECT_EQ(0u, drainAndCheck()) << "leafs with missing or wrong data after first update";

    // Steady state
    std::vector<double> tDecode, tDissect, tTotal;
    unsigned int bad = 0;
    for (unsigned int i = 0; i < iterations; i++) {
        t0 = Clock::now();
        decode(dv);
        auto t1 = Clock::now();
        item->setIncomingData(dv, ProcessReason::incomingData);
        double d1 = std::chrono::duration<double, std::milli>(t1 - t0).count();
        double d2 = msSince(t1);
        tDecode.push_back(d1);
        tDissect.push_back(d2);
        tTotal.push_back(d1 + d2);
        fence->wait();
        bad += drainAndCheck();
    }
    EXPECT_EQ(0u, bad) << "leafs with missing or wrong data after steady state updates";

    Stats sDec = statsOf(tDecode), sDis = statsOf(tDissect), sTot = statsOf(tTotal);
    std::cout << std::fixed << std::setprecision(2)
              << "\nStructure dissection benchmark (open62541)\n"
              << "  structure:      depth " << depth << ", fan-out " << shapeString() << " -> " << noOfLeafs
              << " leafs, " << noOfNodes << " nodes, " << encoded.length << " bytes encoded\n"
              << "  encoded value:  " << tEncode << " ms (types + value, not part of dissection)\n"
              << "  tree setup:     " << tSetup << " ms (" << noOfLeafs << " records/elements)\n"
              << "  first update:   " << tFirstDecode + tFirst << " ms (decode " << tFirstDecode
              << " ms + dissect " << tFirst << " ms, incl. mapping)\n";
    if (iterations)
        std::cout << "  steady state:   " << iterations << " updates: mean " << sTot.mean << " ms, median "
                  << sTot.median << " ms, min " << sTot.min << " ms, max " << sTot.max << " ms\n"
                  << "    decode:       mean " << sDec.mean << " ms, median " << sDec.median << " ms\n"
                  << "    dissect:      mean " << sDis.mean << " ms, median " << sDis.median << " ms\n"
                  << "                  " << std::setprecision(1) << sTot.mean * 1e6 / noOfLeafs
                  << " ns per leaf element (decode + dissect)\n";
    std::cout << std::endl;
}

} // namespace

#endif // open62541 >= 1.3
