/*************************************************************************\
* Copyright (c) 2018-2021 ITER Organization.
* This module is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
\*************************************************************************/

/*
 *  Author: Ralph Lange <ralph.lange@gmx.de>
 *
 *  based on prototype work by Bernhard Kuner <bernhard.kuner@helmholtz-berlin.de>
 */

#include <errlog.h>

#include <epicsThread.h>
#include <epicsEvent.h>

#define epicsExportSharedSymbols
#include "Session.h"
#include "Subscription.h"
#include "SessionUaSdk.h"
#include "SubscriptionUaSdk.h"

namespace DevOpcua {

Subscription::~Subscription() {}

Subscription *
Subscription::createSubscription(const std::string &name,
                                 const std::string &session,
                                 const double publishingInterval)
{
    SessionUaSdk *s = SessionUaSdk::find(session);
    if (RegistryKeyNamespace::global.contains(name) || !s)
        return nullptr;
    return new SubscriptionUaSdk(name, s, publishingInterval);
}

Subscription *
Subscription::find (const std::string &name)
{
    return SubscriptionUaSdk::find(name);
}

std::set<Subscription *>
Subscription::glob(const std::string &pattern)
{
    return SubscriptionUaSdk::glob(pattern);
}

void
Subscription::showAll (const int level)
{
    SubscriptionUaSdk::showAll(level);
}

const char Subscription::optionUsage[]
    = "Valid subscription options are:\n"
      "debug              debug level [default 0 = no debug]\n"
      "priority           priority level [default 0(lowest) .. 255]\n"
      "";

} // namespace DevOpcua
