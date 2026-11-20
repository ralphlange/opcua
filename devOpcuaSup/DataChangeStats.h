/*************************************************************************\
* Copyright (c) 2026 ITER Organization.
* This module is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
\*************************************************************************/

/*
 *  Author: Ralph Lange <ralph.lange@gmx.de>
 */

#ifndef DEVOPCUA_DATACHANGESTATS_H
#define DEVOPCUA_DATACHANGESTATS_H

#include <iomanip>
#include <ostream>
#include <sstream>
#include <string>

#include <epicsGuard.h>
#include <epicsMutex.h>
#include <epicsTime.h>
#include <epicsTypes.h>
#include <epicsVersion.h>

#ifndef VERSION_INT
#  define VERSION_INT(V, R, M, P) (((V) << 24) | ((R) << 16) | ((M) << 8) | (P))
#endif
#ifndef EPICS_VERSION_INT
#  define EPICS_VERSION_INT VERSION_INT(EPICS_VERSION, EPICS_REVISION, EPICS_MODIFICATION, EPICS_PATCH_LEVEL)
#endif

namespace DevOpcua {

/**
 * @brief Timing statistics of the data change callbacks of a subscription.
 *
 * Measures the time spent in the client library's callback thread
 * (pushing the incoming data into the data elements and records).
 *
 * Thread-safe: callbacks are recorded by the client library's thread,
 * the statistics are shown and reset from the IOC shell.
 */
class DataChangeStats
{
public:
    /**
     * @brief Current time for measuring durations [ns].
     *
     * Monotonic clock where available (EPICS Base >= 3.16.1), else the current time.
     */
    static epicsUInt64
    now()
    {
#if EPICS_VERSION_INT >= VERSION_INT(3, 16, 1, 0)
        return epicsMonotonicGet();
#else
        epicsTimeStamp ts;
        epicsTimeGetCurrent(&ts);
        return static_cast<epicsUInt64>(ts.secPastEpoch) * 1000000000u + ts.nsec;
#endif
    }

    /**
     * @brief Record a data change callback.
     *
     * @param durationNs  duration of the callback [ns]
     * @param notifications  number of data notifications (items) in the callback
     * @param intervalMs  publishing interval of the subscription [ms]
     * @param maxItemNs  longest time for a single item in the callback [ns]
     * @param maxItemName  callable returning the name of that item
     *                     (only called if it becomes the slowest item)
     */
    template<typename F>
    void
    record(epicsUInt64 durationNs, epicsUInt64 notifications, double intervalMs, epicsUInt64 maxItemNs, F maxItemName)
    {
        epicsGuard<epicsMutex> G(lock);
        data.callbacks++;
        data.notifications += notifications;
        data.totalNs += durationNs;
        data.lastNs = durationNs;
        if (durationNs > data.maxNs)
            data.maxNs = durationNs;
        if (durationNs > intervalMs * 1e6)
            data.slow++;
        if (notifications && maxItemNs > data.maxItemNs) {
            data.maxItemNs = maxItemNs;
            data.maxItem = maxItemName();
        }
    }

    /** @brief Reset the statistics. */
    void
    reset()
    {
        epicsGuard<epicsMutex> G(lock);
        data = Data();
    }

    /**
     * @brief Print the statistics (name=value pairs, each with a leading space, no line end).
     *
     * Meant to be appended to the show output line of the subscription.
     * Times are in ms. The slowest item's node id follows its time in brackets.
     *
     * @param os  stream to print to
     */
    void
    show(std::ostream &os) const
    {
        Data d;
        {
            epicsGuard<epicsMutex> G(lock);
            d = data;
        }
        os << " callbacks=" << d.callbacks << " notifications=" << d.notifications
           << " avgMs=" << ms(d.callbacks ? d.totalNs / d.callbacks : 0) << " maxMs=" << ms(d.maxNs)
           << " lastMs=" << ms(d.lastNs) << " overInterval=" << d.slow;
        if (!d.maxItem.empty())
            os << " slowestItemMs=" << ms(d.maxItemNs) << "(" << d.maxItem << ")";
    }

private:
    static std::string
    ms(epicsUInt64 ns)
    {
        std::ostringstream o;
        o << std::fixed << std::setprecision(3) << ns / 1e6;
        return o.str();
    }

    struct Data
    {
        epicsUInt64 callbacks = 0;     /**< number of callbacks */
        epicsUInt64 notifications = 0; /**< number of data notifications (items) */
        epicsUInt64 slow = 0;          /**< callbacks that took longer than the publishing interval */
        epicsUInt64 totalNs = 0;       /**< sum of callback durations */
        epicsUInt64 maxNs = 0;         /**< longest callback */
        epicsUInt64 lastNs = 0;        /**< latest callback */
        epicsUInt64 maxItemNs = 0;     /**< longest time for a single item */
        std::string maxItem;           /**< item that took longest */
    };

    mutable epicsMutex lock;
    Data data;
};

} // namespace DevOpcua

#endif // DEVOPCUA_DATACHANGESTATS_H
