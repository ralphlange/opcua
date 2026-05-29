/*************************************************************************\
* Copyright (c) 2026 ITER Organization.
* This module is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
\*************************************************************************/

/*
 *  Author: Ralph Lange <ralph.lange@gmx.de>
 *  Co-authored-by: Gemini (Design), Jules (Implementation)
 */

#ifndef DEVOPCUA_ADAPTIVECONCURRENCY_H
#define DEVOPCUA_ADAPTIVECONCURRENCY_H

#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <unordered_map>

namespace DevOpcua
{

/**
 * @class SlidingAverageTimer
 * @brief Tracks average of values over a sliding time window.
 */
class SlidingAverageTimer
{
public:
    explicit SlidingAverageTimer(std::chrono::milliseconds windowDuration = std::chrono::seconds(5))
        : windowDuration(windowDuration)
    {}

    void addValue(double value)
    {
        auto now = std::chrono::steady_clock::now();
        data.push_back({now, value});
        cleanUpOldData(now);
    }

    double getAverage()
    {
        auto now = std::chrono::steady_clock::now();
        cleanUpOldData(now);
        if (data.empty())
            return 0.0;

        double sum = 0.0;
        for (const auto &entry : data) {
            sum += entry.value;
        }
        return sum / data.size();
    }

    void reset() { data.clear(); }

private:
    struct DataEntry {
        std::chrono::steady_clock::time_point timestamp;
        double value;
    };
    std::deque<DataEntry> data;
    std::chrono::milliseconds windowDuration;

    void cleanUpOldData(std::chrono::steady_clock::time_point now)
    {
        while (!data.empty() && (now - data.front().timestamp) > windowDuration) {
            data.pop_front();
        }
    }
};

/**
 * @class AdaptiveConcurrencyManager
 * @brief Manages concurrency window size using AIMD algorithm based on response time.
 */
class AdaptiveConcurrencyManager
{
public:
    AdaptiveConcurrencyManager(double initialWindow = 5.0,
                               double minWindow = 1.0,
                               double maxWindow = 50.0,
                               std::chrono::milliseconds responseTimeThreshold = std::chrono::milliseconds(200),
                               double increaseFactor = 1.0,
                               double decreaseFactor = 0.5)
        : currentWindowSize(initialWindow)
        , minWindowSize(std::max(1.0, minWindow))
        , maxWindowSize(std::max(minWindowSize, maxWindow))
        , responseTimeThreshold(responseTimeThreshold)
        , increaseFactor(increaseFactor)
        , decreaseFactor(decreaseFactor)
        , responseTimeAvg(std::chrono::seconds(5))
        , enabled(false)
    {}

    void setEnabled(bool enabled)
    {
        std::unique_lock<std::mutex> lock(mutex);
        this->enabled = enabled;
        if (!this->enabled) {
            cv.notify_all();
        }
    }

    bool isEnabled() const
    {
        std::unique_lock<std::mutex> lock(mutex);
        return enabled;
    }

    void setParams(double minWindow, double maxWindow, std::chrono::milliseconds threshold)
    {
        std::unique_lock<std::mutex> lock(mutex);
        minWindowSize = std::max(1.0, minWindow);
        maxWindowSize = std::max(minWindowSize, maxWindow);
        responseTimeThreshold = threshold;
        if (currentWindowSize < minWindowSize)
            currentWindowSize = minWindowSize;
        if (currentWindowSize > maxWindowSize)
            currentWindowSize = maxWindowSize;
        cv.notify_all();
    }

    double getMinWindowSize() const
    {
        std::unique_lock<std::mutex> lock(mutex);
        return minWindowSize;
    }

    double getMaxWindowSize() const
    {
        std::unique_lock<std::mutex> lock(mutex);
        return maxWindowSize;
    }

    std::chrono::milliseconds getResponseTimeThreshold() const
    {
        std::unique_lock<std::mutex> lock(mutex);
        return responseTimeThreshold;
    }

    void canSendRequest()
    {
        std::unique_lock<std::mutex> lock(mutex);
        if (!enabled)
            return;
        cv.wait(lock, [this] {
            return !enabled || outstandingRequests.size() < static_cast<size_t>(std::ceil(currentWindowSize));
        });
    }

    void requestSent(uint32_t correlationId)
    {
        std::unique_lock<std::mutex> lock(mutex);
        outstandingRequests[correlationId] = {std::chrono::steady_clock::now()};
    }

    void requestCompleted(uint32_t correlationId)
    {
        std::unique_lock<std::mutex> lock(mutex);
        auto it = outstandingRequests.find(correlationId);
        if (it != outstandingRequests.end()) {
            auto now = std::chrono::steady_clock::now();
            auto responseTime = std::chrono::duration_cast<std::chrono::milliseconds>(now - it->second.sentTime);
            responseTimeAvg.addValue(static_cast<double>(responseTime.count()));
            outstandingRequests.erase(it);
            adjustWindow();
            cv.notify_all();
        }
    }

    void requestFailed(uint32_t correlationId)
    {
        std::unique_lock<std::mutex> lock(mutex);
        outstandingRequests.erase(correlationId);
        // Treat failure as congestion
        currentWindowSize = std::max(minWindowSize, currentWindowSize * decreaseFactor);
        cv.notify_all();
    }

    void reset()
    {
        std::unique_lock<std::mutex> lock(mutex);
        outstandingRequests.clear();
        responseTimeAvg.reset();
        cv.notify_all();
    }

    double getCurrentWindowSize() const
    {
        std::unique_lock<std::mutex> lock(mutex);
        return currentWindowSize;
    }

    double getAverageResponseTime()
    {
        std::unique_lock<std::mutex> lock(mutex);
        return responseTimeAvg.getAverage();
    }

private:
    void adjustWindow()
    {
        double avgRt = responseTimeAvg.getAverage();
        if (avgRt <= 0) {
            currentWindowSize = std::min(maxWindowSize, currentWindowSize + increaseFactor);
        } else if (std::chrono::milliseconds(static_cast<long long>(avgRt)) > responseTimeThreshold) {
            currentWindowSize = std::max(minWindowSize, currentWindowSize * decreaseFactor);
        } else {
            currentWindowSize = std::min(maxWindowSize, currentWindowSize + increaseFactor);
        }
    }

    mutable std::mutex mutex;
    std::condition_variable cv;

    double currentWindowSize;
    double minWindowSize;
    double maxWindowSize;
    std::chrono::milliseconds responseTimeThreshold;
    const double increaseFactor;
    const double decreaseFactor;

    SlidingAverageTimer responseTimeAvg;
    bool enabled;

    struct PendingRequest {
        std::chrono::steady_clock::time_point sentTime;
    };
    std::unordered_map<uint32_t, PendingRequest> outstandingRequests;
};

} // namespace DevOpcua

#endif // DEVOPCUA_ADAPTIVECONCURRENCY_H
