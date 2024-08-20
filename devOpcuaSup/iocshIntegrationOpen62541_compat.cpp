/*************************************************************************\
* Copyright (c) 2024 ITER Organization.
* This module is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
\*************************************************************************/

/*
 *  Author: Ralph Lange <ralph.lange@gmx.de>
 */

static const iocshArg iocshOpen62541ConnectionSetupArg0 = {"connection ID", iocshArgString};
static const iocshArg iocshOpen62541ConnectionSetupArg1 = {"endpoint URL", iocshArgString};
static const iocshArg iocshOpen62541ConnectionSetupArg2 = {"username", iocshArgString};
static const iocshArg iocshOpen62541ConnectionSetupArg3 = {"password", iocshArgString};

static const iocshArg *const iocshOpen62541ConnectionSetupArgs[]
    = {&iocshOpen62541ConnectionSetupArg0,
       &iocshOpen62541ConnectionSetupArg1,
       &iocshOpen62541ConnectionSetupArg2,
       &iocshOpen62541ConnectionSetupArg3};

static const iocshFuncDef iocshOpen62541ConnectionSetupFuncDef = {"open62541ConnectionSetup",
                                                                  4,
                                                                  iocshOpen62541ConnectionSetupArgs};

/**
 * Implementation of the iocsh open62541ConnectionSetup function. This function
 * creates a connection to an OPC UA server.
 */
static void iocshOpen62541ConnectionSetupFunc(const iocshArgBuf *args) noexcept
{
    char *connectionId = args[0].sval;
    char *endpointUrl = args[1].sval;
    char *username = args[2].sval;
    char *password = args[3].sval;
    Session *s = nullptr;
    Subscription *sub = nullptr;
    int debug = 0;

    // Verify the parameters
    if (!connectionId || !std::strlen(connectionId)) {
        errlogPrintf("Could not setup the connection: Connection ID must be specified.");
        return;
    }
    if (!endpointUrl || !std::strlen(endpointUrl)) {
        errlogPrintf("Could not setup the connection: Endpoint URL must be specified.");
        return;
    }

    errlogPrintf("open62541_compat mode: calling \"opcuaSession %s %s sec-mode=None\"\n",
                 connectionId,
                 endpointUrl);

    s = Session::createSession(connectionId, endpointUrl);
    if (s && debug)
        errlogPrintf("opcuaSession: successfully created session '%s'\n", args[0].sval);
    s->setOption("sec-mode", "None");

    errlogPrintf("open62541_compat mode: calling \"opcuaSubscription %s-default %s 500.0\"\n",
                 connectionId,
                 connectionId);

    std::string name = connectionId;
    name.append("-default");
    sub = Subscription::createSubscription(name.c_str(), connectionId, 500.0);
    if (sub && debug)
        errlogPrintf("opcuaSubscription: successfully created subscription '%s'\n", name.c_str());
}

/**
 * Implementation of the iocsh open62541Set... functions.
 * These functions ignore the requested parameter for a specific subscription
 * associated with a specific connection.
 */

static const iocshArg iocshOpen62541IgnoreSubscriptionSettingArg0 = {"connection ID",
                                                                     iocshArgString};
static const iocshArg iocshOpen62541IgnoreSubscriptionSettingArg1 = {"subscription ID",
                                                                     iocshArgString};
static const iocshArg iocshOpen62541IgnoreSubscriptionSettingArg2I = {"setting", iocshArgInt};

static const iocshArg iocshOpen62541IgnoreSubscriptionSettingArg2D = {"setting", iocshArgDouble};

static const iocshArg *const iocshOpen62541IgnoreSubscriptionSettingIArgs[]
    = {&iocshOpen62541IgnoreSubscriptionSettingArg0,
       &iocshOpen62541IgnoreSubscriptionSettingArg1,
       &iocshOpen62541IgnoreSubscriptionSettingArg2I};

static const iocshArg *const iocshOpen62541IgnoreSubscriptionSettingDArgs[]
    = {&iocshOpen62541IgnoreSubscriptionSettingArg0,
       &iocshOpen62541IgnoreSubscriptionSettingArg1,
       &iocshOpen62541IgnoreSubscriptionSettingArg2D};

static const iocshFuncDef iocshOpen62541SetSubscriptionLifetimeCountFuncDef
    = {"open62541SetSubscriptionLifetimeCount", 3, iocshOpen62541IgnoreSubscriptionSettingIArgs};
static const iocshFuncDef iocshOpen62541SetSubscriptionMaxKeepAliveCountFuncDef
    = {"open62541SetSubscriptionMaxKeepAliveCount", 3, iocshOpen62541IgnoreSubscriptionSettingIArgs};
static const iocshFuncDef iocshOpen62541SetSubscriptionPublishingIntervalFuncDef
    = {"open62541SetSubscriptionPublishingInterval", 3, iocshOpen62541IgnoreSubscriptionSettingDArgs};

static void iocshOpen62541IgnoreSubscriptionSettingIFunc(const iocshArgBuf *args) noexcept
{
    char *connectionId = args[0].sval;
    char *subscriptionId = args[1].sval;
    int integerValue = args[2].ival;
    Subscription *sub = nullptr;

    // Verify the parameters
    if (!connectionId || !std::strlen(connectionId)) {
        errlogPrintf("Error: Connection ID must be specified.");
        return;
    }
    if (!subscriptionId || !std::strlen(subscriptionId)) {
        errlogPrintf("Error: Subscription ID must be specified.");
        return;
    }
    if (!Session::find(connectionId)) {
        errlogPrintf("Connection (session) %s does not exist\n", connectionId);
        return;
    }
    std::string name = connectionId;
    name.append("-");
    name.append(subscriptionId);
    if (Subscription::find(name))
        return;
    errlogPrintf("open62541_compat mode: calling \"opcuaSubscription %s %s 500.0\"\n",
                 name.c_str(),
                 connectionId);
    sub = Subscription::createSubscription(name.c_str(), connectionId, 500.0);
}

static void iocshOpen62541IgnoreSubscriptionSettingDFunc(const iocshArgBuf *args) noexcept
{
    char *connectionId = args[0].sval;
    char *subscriptionId = args[1].sval;
    int doubleValue = args[2].dval;
    Subscription *sub = nullptr;

    // Verify the parameters
    if (!connectionId || !std::strlen(connectionId)) {
        errlogPrintf("Error: Connection ID must be specified.");
        return;
    }
    if (!subscriptionId || !std::strlen(subscriptionId)) {
        errlogPrintf("Error: Subscription ID must be specified.");
        return;
    }
    if (!Session::find(connectionId)) {
        errlogPrintf("Connection (session) %s does not exist\n", connectionId);
        return;
    }
    std::string name = connectionId;
    name.append("-");
    name.append(subscriptionId);
    if (Subscription::find(name))
        return;
    errlogPrintf("open62541_compat mode: calling \"opcuaSubscription %s %s 500.0\"\n",
                 name.c_str(),
                 connectionId);
    sub = Subscription::createSubscription(name.c_str(), connectionId, doubleValue);
}

