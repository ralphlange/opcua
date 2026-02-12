/*************************************************************************\
* Copyright (c) 2025 ITER Organization.
* This module is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
\*************************************************************************/

/*
 *  Author: Ralph Lange <ralph.lange@gmx.de>
 */

#ifndef STATS_H
#define STATS_H

#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <epicsGuard.h>
#include <epicsMutex.h>

typedef epicsGuard<epicsMutex> Guard;
typedef epicsGuardRelease<epicsMutex> UnGuard;

namespace DevOpcua {

/**
 * @brief A simple thread-safe counter.
 */
class StatsCounter
{
public:
    StatsCounter();

    void increment(long long val = 1);
    void set(long long val);

    long long get() const;
    void reset();

private:
    std::atomic<long long> value;
};

/**
 * @brief A thread-safe histogram for storing execution time distributions.
 */
class StatsHistogram
{
public:
    StatsHistogram(const std::vector<double> &buckets);

    void record(const double value);

    void print(std::ostream &os) const;
    void reset();

private:
    std::vector<double> buckets;
    std::vector<std::shared_ptr<std::atomic<long long>>> counts;
};

/**
 * @brief Collects and calculates statistics for execution times.
 */
class StatsExecTime
{
public:
    StatsExecTime(const std::vector<double> &buckets);

    void record(const long long nanoseconds);
    void reset();

    long long getTotalExecutionTimeNs() const;
    long long getCount() const;
    double getAverageTimeUs() const;
    void print(std::ostream &os) const;

private:
    std::atomic<long long> totalExecutionTime;
    std::atomic<long long> count;
    StatsHistogram histogram;
};

/**
 * @brief An RAII-style timer to measure execution time of a scope.
 */
class StatsTimer
{
public:
    explicit StatsTimer(std::shared_ptr<StatsExecTime> stats);
    ~StatsTimer();

private:
    std::shared_ptr<StatsExecTime> stats;
    std::chrono::time_point<std::chrono::high_resolution_clock> startTime;
};

/**
 * @brief Thread-safe singleton for creating and managing named statistics.
 *
 * Provides a central point of access for all statistics in the application.
 * Ensures that statistics are uniquely named
 * and that access to them is thread-safe.
 */
class StatsManager
{
public:
    /**
     * @brief Gets the singleton instance of the registry.
     * @return Reference to the singleton StatsManager instance.
     */
    static StatsManager &getInstance();

    /**
     * @brief Gets or creates a named counter.
     * @param name Name of the counter.
     * @return Shared pointer to the Counter.
     */
    std::shared_ptr<StatsCounter> getCounter(const std::string &name);

    /**
     * @brief Gets or creates a named execution statistics collector.
     * @param name Name of the execution statistics.
     * @param histogram_buckets A vector of upper bounds for the histogram buckets in microseconds.
     * @return A shared pointer to the ExecutionStats.
     */
    std::shared_ptr<StatsExecTime> getExecutionStats(
        const std::string &name, const std::vector<double> &histogram_buckets = {});

    /**
     * @brief Prints a report of all registered statistics to the given output stream.
     * @param os Output stream to print to.
     */
    void report(std::ostream &os) const;

    void reset(const std::string &name);
    void reset_all();

private:
    StatsManager() = default;
    ~StatsManager() = default;
    StatsManager(const StatsManager &) = delete;
    StatsManager &operator=(const StatsManager &) = delete;

    mutable epicsMutex lock;
    std::unordered_map<std::string, std::shared_ptr<StatsCounter>> counters;
    std::unordered_map<std::string, std::shared_ptr<StatsExecTime>> execTimes;
};

} // namespace DevOpcua

#endif // STATS_H
