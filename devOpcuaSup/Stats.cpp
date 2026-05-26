/*************************************************************************\
* Copyright (c) 2025 ITER Organization.
* This module is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
\*************************************************************************/

/*
 *  Author: Ralph Lange <ralph.lange@gmx.de>
 */

#include "Stats.h"

#include <algorithm>
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

void StatsHistogram::reset()
{
    for (auto &count : counts) {
        count->store(0, std::memory_order_relaxed);
    }
}

StatsSlidingAverage::StatsSlidingAverage(size_t windowSize)
    : windowSize(windowSize > 0 ? windowSize : 1)
    , nextIndex(0)
    , count(0)
{
    buffer.resize(this->windowSize);
}

void StatsSlidingAverage::record(double value)
{
    Guard G(lock);
    buffer[nextIndex] = value;
    nextIndex = (nextIndex + 1) % windowSize;
    if (count < windowSize)
        count++;
}

void StatsSlidingAverage::reset()
{
    Guard G(lock);
    nextIndex = 0;
    count = 0;
}

double StatsSlidingAverage::getAverage() const
{
    Guard G(lock);
    if (count == 0)
        return 0.0;
    double sum = 0.0;
    for (size_t i = 0; i < count; ++i) {
        sum += buffer[i];
    }
    return sum / count;
}

double StatsSlidingAverage::getMedian() const
{
    Guard G(lock);
    if (count == 0)
        return 0.0;
    std::vector<double> sorted(buffer.begin(), buffer.begin() + count);
    std::sort(sorted.begin(), sorted.end());
    if (count % 2 == 0) {
        return (sorted[count / 2 - 1] + sorted[count / 2]) / 2.0;
    } else {
        return sorted[count / 2];
    }
}

double StatsSlidingAverage::getMin() const
{
    Guard G(lock);
    if (count == 0)
        return 0.0;
    double minVal = buffer[0];
    for (size_t i = 1; i < count; ++i) {
        if (buffer[i] < minVal)
            minVal = buffer[i];
    }
    return minVal;
}

double StatsSlidingAverage::getMax() const
{
    Guard G(lock);
    if (count == 0)
        return 0.0;
    double maxVal = buffer[0];
    for (size_t i = 1; i < count; ++i) {
        if (buffer[i] > maxVal)
            maxVal = buffer[i];
    }
    return maxVal;
}

void StatsSlidingAverage::print(std::ostream &os, int verbosity) const
{
    Guard G(lock);
    os << "    Average: " << getAverage() << std::endl;
    if (verbosity > 0) {
        os << "    Median:  " << getMedian() << std::endl;
        os << "    Min:     " << getMin() << std::endl;
        os << "    Max:     " << getMax() << std::endl;
        os << "    Count:   " << count << " (window: " << windowSize << ")" << std::endl;
    }
}

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

std::shared_ptr<StatsSlidingAverage> StatsManager::getSlidingAverage(
    const std::string &name, size_t windowSize)
{
    Guard G(lock);
    if (slidingAverages.find(name) == slidingAverages.end()) {
        slidingAverages[name] = std::make_shared<StatsSlidingAverage>(windowSize);
    }
    return slidingAverages[name];
}

void StatsManager::report(std::ostream &os, int verbosity, const std::string &pattern) const
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

    os << "\n-- Sliding Averages --" << std::endl;
    for (const auto &pair : slidingAverages) {
        if (epicsStrGlobMatch(pair.first.c_str(), pattern.c_str())) {
            os << pair.first << ":" << std::endl;
            pair.second->print(os, verbosity);
        }
    }

    os << "-------------------------" << std::endl;
}

void StatsManager::reset(const std::string &name)
{
    Guard G(lock);
    for (auto &pair : counters) {
        if (epicsStrGlobMatch(pair.first.c_str(), name.c_str()))
            pair.second->reset();
    }
    for (auto &pair : execTimes) {
        if (epicsStrGlobMatch(pair.first.c_str(), name.c_str()))
            pair.second->reset();
    }
    for (auto &pair : slidingAverages) {
        if (epicsStrGlobMatch(pair.first.c_str(), name.c_str()))
            pair.second->reset();
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
    for (auto &pair : slidingAverages) {
        pair.second->reset();
    }
}

} // namespace DevOpcua
