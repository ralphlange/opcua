/*************************************************************************\
* Copyright (c) 2025 ITER Organization.
* This module is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
\*************************************************************************/

/*
 *  Author: Ralph Lange <ralph.lange@gmx.de>
 */

#include "Stats.h"

#include <iomanip>

#include <epicsString.h>

namespace DevOpcua {

StatsCounter::StatsCounter()
    : value(0)
{}

void StatsCounter::increment(long long val)
{
    value.fetch_add(val, std::memory_order_relaxed);
}

void StatsCounter::set(long long val)
{
    value.store(val, std::memory_order_relaxed);
}

long long StatsCounter::get() const
{
    return value.load(std::memory_order_relaxed);
}

void StatsCounter::reset()
{
    set(0);
}

StatsHistogram::StatsHistogram(const std::vector<double> &buckets)
    : buckets(buckets)
{
    counts.resize(buckets.size() + 1);
    for (auto &count : counts) {
        count = std::make_shared<std::atomic<long long>>();
    }
}

void StatsHistogram::record(const double value)
{
    auto it = std::lower_bound(buckets.begin(), buckets.end(), value);
    size_t index = std::distance(buckets.begin(), it);
    counts[index]->fetch_add(1, std::memory_order_relaxed);
}

void StatsHistogram::print(std::ostream &os) const
{
    for (size_t i = 0; i < buckets.size(); ++i) {
        if (i == 0) {
            os << "                    <= " << std::setw(10) << buckets[i] << " us: " << counts[i]->load()
               << std::endl;
        } else {
            os << "  >  " << std::setw(10) << buckets[i - 1] << " and <= " << std::setw(10)
               << buckets[i] << " us: " << counts[i]->load() << std::endl;
        }
    }
    os << "                    >  " << std::setw(10) << buckets.back() << " us: " << counts.back()->load()
       << std::endl;
}

void StatsHistogram::reset() {}

StatsExecTime::StatsExecTime(const std::vector<double> &buckets)
    : totalExecutionTime(0)
    , count(0)
    , histogram(buckets.empty()
                    ? std::vector<double>{1, 5, 10, 50, 100, 500, 1000, 5000, 10000, 50000, 100000}
                    : buckets)
{}

void StatsExecTime::record(const long long nanoseconds)
{
    totalExecutionTime.fetch_add(nanoseconds, std::memory_order_relaxed);
    count.fetch_add(1, std::memory_order_relaxed);
    histogram.record(static_cast<double>(nanoseconds) / 1e3); // Record in microseconds
}

void StatsExecTime::reset()
{
    totalExecutionTime.store(0);
    count.store(0);
    histogram.reset();
}

long long StatsExecTime::getTotalExecutionTimeNs() const
{
    return totalExecutionTime.load(std::memory_order_relaxed);
}

long long StatsExecTime::getCount() const
{
    return count.load(std::memory_order_relaxed);
}

double StatsExecTime::getAverageTimeUs() const
{
    long long count = getCount();
    if (count == 0)
        return 0.0;
    return (static_cast<double>(getTotalExecutionTimeNs()) / 1000.) / static_cast<double>(count);
}

void StatsExecTime::print(std::ostream &os) const
{
    os << "    Total Exec Time: " << static_cast<double>(getTotalExecutionTimeNs()) / 1e9 << " s"
       << std::endl;
    os << "    Count: " << getCount() << std::endl;
    os << "    Average Time: " << getAverageTimeUs() << " us" << std::endl;
    os << "    Histogram (us):" << std::endl;
    histogram.print(os);
}

StatsTimer::StatsTimer(std::shared_ptr<StatsExecTime> stats)
    : stats(stats)
    , startTime(std::chrono::high_resolution_clock::now())
{}

StatsTimer::~StatsTimer()
{
    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(endTime - startTime).count();
    stats->record(duration);
}

StatsManager &StatsManager::getInstance()
{
    static StatsManager instance;
    return instance;
}

std::shared_ptr<StatsCounter> StatsManager::getCounter(const std::string &name)
{
    Guard G(lock);
    if (counters.find(name) == counters.end()) {
        counters[name] = std::make_shared<StatsCounter>();
    }
    return counters[name];
}

std::shared_ptr<StatsExecTime> StatsManager::getExecutionStats(
    const std::string &name, const std::vector<double> &histogram_buckets)
{
    Guard G(lock);
    if (execTimes.find(name) == execTimes.end()) {
        execTimes[name] = std::make_shared<StatsExecTime>(histogram_buckets);
    }
    return execTimes[name];

}

void StatsManager::report(std::ostream &os, const std::string &pattern) const
{
    Guard G(lock);
    os << "--- Statistics Report ---" << std::endl;

    os << "\n-- Counters --" << std::endl;
    for (const auto& pair : counters) {
        if (epicsStrGlobMatch(pair.first.c_str(), pattern.c_str()))
            os << pair.first << ": " << pair.second->get() << std::endl;
    }

    os << "\n-- Execution Timers --" << std::endl;
    for (const auto &pair : execTimes) {
        if (epicsStrGlobMatch(pair.first.c_str(), pattern.c_str())) {
            os << pair.first << ":" << std::endl;
            pair.second->print(os);
        }
    }

    os << "-------------------------" << std::endl;

}

void StatsManager::reset(const std::string &name)
{
    Guard G(lock);
    if (counters.count(name)) {
        counters[name]->reset();
    }
    if (execTimes.count(name)) {
        execTimes[name]->reset();
    }
}

void StatsManager::reset_all()
{
    Guard G(lock);
    for (auto &pair : counters) {
        pair.second->reset();
    }
    for (auto &pair : execTimes) {
        pair.second->reset();
    }
}

} // namespace DevOpcua
