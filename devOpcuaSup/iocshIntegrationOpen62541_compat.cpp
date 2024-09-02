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

/**
 * Mechanism (driven through initHooks) to swap the open62541 links with OPCUA links while iocInit() is running
 */

#include <string>
#include <vector>

std::vector<std::string> tokenize_string(const std::string &input,
                                         const std::string &delimiters,
                                         bool use_quoted_strings = false,
                                         char escape_char = '\\')
{
    std::vector<std::string> tokens;
    std::string token;

    bool in_quotes = false;
    size_t pos = 0;

    while (pos < input.length()) {
        char c = input[pos];

        if (c == escape_char) {
            ++pos;
            if (pos >= input.length()) {
                break; // Error: Unexpected end of line
            }
            token += input[pos];
        } else if (use_quoted_strings && c == '"') {
            in_quotes = !in_quotes;
        } else {
            if (!in_quotes && delimiters.find(c) != std::string::npos) {
                if (token.length())
                    tokens.push_back(token);
                token.clear();
            } else {
                token += c;
            }
        }
        ++pos;
    }
    tokens.push_back(token);

    return tokens;
}

std::string convertToOpcuaLink(const std::string &open62541Link)
{
    std::string subscriptionId;
    std::string sampling, bini, ns, id, typ;
    size_t pos = 0;

    std::vector<std::string> tokens = tokenize_string(open62541Link, " ");

    std::string connectionId = tokens[pos].substr(1);
    subscriptionId = connectionId + "-default";

    ++pos;

    // Options
    if (tokens[pos].at(0) == '(' && tokens[pos].back() == ')') {
        std::string optionString = tokens[pos].substr(1, tokens[pos].size() - 2);

        std::vector<std::string> options = tokenize_string(optionString, ",");
        for (const auto &option : options) {
            std::vector<std::string> kv = tokenize_string(option, "=");

            if (kv[0] == "subscription")
                subscriptionId = connectionId + "-" + kv[1];
            if (kv[0] == "sampling_interval")
                sampling = kv[1];
            if (kv[0] == "no_read_on_init")
                bini = "ignore";
        }
        ++pos;
    }

    // nodeID
    std::vector<std::string> NodeId = tokenize_string(tokens[pos], ":,");
    if (NodeId.size() != 3) {
        errlogPrintf("Invalid node ID '%s'\n", tokens[pos].c_str());
        return "";
    }
    if (NodeId[0] == "str")
        typ = "s";
    else if (NodeId[0] == "num")
        typ = "i";
    ns = NodeId[1];
    id = NodeId[2];

    std::string result = "@" + subscriptionId + " ns=" + ns + ";" + typ + "=" + id + "";
    if (sampling.size())
        result.append(" sampling=" + sampling);
    if (bini.size())
        result.append(" bini=" + bini);
    return result;
}

void linkConvert(initHookState state)
{
    switch (state) {
    case initHookAfterInitDevSup: {
        DBENTRY entry;
        long status;
        char *l;

        errlogPrintf("OPC UA: open62541_compat mode is converting INP/OUT links\n");

        dbInitEntry(pdbbase, &entry);

        status = dbFirstRecordType(&entry);
        if (status) {
            errlogPrintf("No record types\n");
            dbFinishEntry(&entry);
            break;
        }
        while (!status) {
            status = dbFirstRecord(&entry);
            while (!status) {
                if (!dbIsAlias(&entry)) {
                    status = dbFindField(&entry, "DTYP");
                    if (!status) {
                        l = dbGetString(&entry);
                        std::string dtyp = l ? l : "";
                        if (dtyp == "open62541") {
                            status = dbFindField(&entry, "INP");
                            if (status) {
                                status = dbFindField(&entry, "OUT");
                                if (status) {
                                    errlogPrintf("DTYP=open62541, but no INP or OUT link\n");
                                    break;
                                }
                            }
                            l = dbGetString(&entry);
                            std::string oldLink = l ? l : "";
                            status = dbPutString(&entry, convertToOpcuaLink(oldLink).c_str());
                        }
                    }
                }
                status = dbNextRecord(&entry);
            }
            status = dbNextRecordType(&entry);
        }
        dbFinishEntry(&entry);
    } break;
    default:
        break;
    }
}
