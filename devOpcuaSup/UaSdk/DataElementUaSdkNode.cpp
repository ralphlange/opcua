/*************************************************************************\
* Copyright (c) 2018-2025 ITER Organization.
* This module is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
\*************************************************************************/

/*
 *  Author: Ralph Lange <ralph.lange@gmx.de>
 *
 *  based on prototype work by Bernhard Kuner <bernhard.kuner@helmholtz-berlin.de>
 *  and example code from the Unified Automation C++ Based OPC UA Client SDK
 */

#include "DataElementUaSdkNode.h"

#include <iostream>
#include <memory>
#include <string>

#include <opcua_builtintypes.h>
#include <statuscode.h>
#include <uaarraytemplates.h>
#include <uadatetime.h>
#include <uaenumdefinition.h>
#include <uaextensionobject.h>
#include <uagenericunionvalue.h>

#include <alarm.h>
#include <epicsTime.h>
#include <errlog.h>

#include "ItemUaSdk.h"
#include "RecordConnector.h"
#include "Stats.h"

namespace DevOpcua {

DataElementUaSdkNode::DataElementUaSdkNode (const std::string &name, class ItemUaSdk *item)
    : DataElementUaSdk(name, item)
    , timesrc(-1)
    , mapped(false)
{}

void
DataElementUaSdkNode::addChild (std::weak_ptr<DataElementUaSdk> elem)
{
    elements.push_back(elem);
}

std::shared_ptr<DataElementUaSdk>
DataElementUaSdkNode::findChild (const std::string &name) const
{
    for (const auto &it : elements)
        if (auto pit = it.lock())
            if (pit->name == name)
                return pit;
    return std::shared_ptr<DataElementUaSdk>();
}

void
DataElementUaSdkNode::show (const int level, const unsigned int indent) const
{
    std::string ind(indent * 2, ' ');
    std::cout << ind;
    std::cout << "node=" << name << " children=" << elements.size() << " mapped=" << (mapped ? "y" : "n") << "\n";
    for (const auto &it : elements) {
        if (auto pelem = it.lock()) {
            pelem->show(level, indent + 1);
        }
    }
}

void
DataElementUaSdkNode::createMap (const std::string *timefrom)
{
    if (debug() >= 5)
        std::cout << " ** creating index-to-element map for child elements" << std::endl;

    if (timefrom) {
        for (int i = 0; i < definition.childrenCount(); i++) {
            if (*timefrom == definition.child(i).name().toUtf8()) {
                timesrc = i;
            }
        }
        OpcUa_BuiltInType t = definition.child(timesrc).valueType();
        if (timesrc == -1) {
            errlogPrintf("%s: timestamp element %s not found - using source timestamp\n",
                         pitem->recConnector->getRecordName(),
                         timefrom->c_str());
        } else if (t != OpcUaType_DateTime) {
            errlogPrintf("%s: timestamp element %s has invalid type %s - using "
                         "source timestamp\n",
                         pitem->recConnector->getRecordName(),
                         timefrom->c_str(),
                         variantTypeString(t));
            timesrc = -1;
        }
    }
    for (const auto &it : elements) {
        auto pelem = it.lock();
        for (int i = 0; i < definition.childrenCount(); i++) {
            if (pelem->name == definition.child(i).name().toUtf8())
                elementMap.insert({i, it});
        }
    }
    if (debug() >= 5)
        std::cout << " ** " << elementMap.size() << "/" << elements.size() << " child elements mapped to a "
                  << "structure of " << definition.childrenCount() << " elements" << std::endl;
    mapped = true;
}

void
DataElementUaSdkNode::createMap (const UaLocalizedText &)
{
    for (const auto &it : elements) {
        auto pelem = it.lock();
        if (pelem->name == "locale")
            elementMap.insert({0, it});
        else if (pelem->name == "text")
            elementMap.insert({1, it});
        else
            errlogPrintf("Item %s %s element %s not found\n",
                         pitem->nodeid->toString().toUtf8(),
                         name.c_str(),
                         pelem->name.c_str());
    }
    mapped = true;
}

void
DataElementUaSdkNode::createMap (const UaQualifiedName &)
{
    for (const auto &it : elements) {
        auto pelem = it.lock();
        if (pelem->name == "namespaceIndex")
            elementMap.insert({0, it});
        else if (pelem->name == "name")
            elementMap.insert({1, it});
        else
            errlogPrintf("Item %s %s element %s not found\n",
                         pitem->nodeid->toString().toUtf8(),
                         name.c_str(),
                         pelem->name.c_str());
    }
    mapped = true;
}

void
DataElementUaSdkNode::setIncomingData (const UaVariant &value,
                                       ProcessReason reason,
                                       const std::string *timefrom,
                                       const UaNodeId *typeId)
{
    if (debug() >= 5)
        std::cout << "Element " << name << " splitting structured data to " << elements.size() << " child elements"
                  << std::endl;

    switch (value.type()) {
    case OpcUaType_ExtensionObject: {
        UaExtensionObject extensionObject;
        value.toExtensionObject(extensionObject);
        setIncomingData(extensionObject, reason, timefrom, typeId);
        break;
    }
    case OpcUaType_LocalizedText: {
        UaLocalizedText localizedText;
        value.toLocalizedText(localizedText);
        if (!mapped)
            createMap(localizedText);

        for (const auto &it : elementMap) {
            auto pelem = it.second.lock();
            UaVariant memberValue;
            switch (it.first) {
            case 0:
                memberValue.setString(localizedText.locale());
                break;
            case 1:
                memberValue.setString(localizedText.text());
                break;
            }
            pelem->setIncomingData(memberValue, reason);
        }
        break;
    }
    case OpcUaType_QualifiedName: {
        UaQualifiedName qualifiedName;
        value.toQualifiedName(qualifiedName);
        if (!mapped)
            createMap(qualifiedName);

        for (const auto &it : elementMap) {
            auto pelem = it.second.lock();
            UaVariant memberValue;
            switch (it.first) {
            case 0:
                memberValue.setUInt16(qualifiedName.namespaceIndex());
                break;
            case 1:
                memberValue.setString(qualifiedName.name());
                break;
            }
            pelem->setIncomingData(memberValue, reason);
        }
        break;
    }
    default:
        errlogPrintf("%s: %s is no structured data but a %s\n",
                     pitem->recConnector->getRecordName(),
                     name.c_str(),
                     variantTypeString(value.type()));
    }
}

void
DataElementUaSdkNode::setIncomingData (const UaExtensionObject &extensionObject,
                                       ProcessReason reason,
                                       const std::string *timefrom,
                                       const UaNodeId *)
{
    if (debug() >= 5)
        std::cout << "Element " << name << " splitting structured data to " << elements.size() << " child elements"
                  << std::endl;

    UaExtensionObject &eo = const_cast<UaExtensionObject &>(extensionObject);
    if (eo.encoding() == UaExtensionObject::EncodeableObject)
        eo.changeEncoding(UaExtensionObject::Binary);
    static auto catalog_timer(StatsManager::getInstance().getExecutionStats(
        "catalog_query", std::vector<double>{100, 200, 500, 1000, 2000, 5000, 10000}));

    if (definition.isNull()) {
        StatsTimer t(catalog_timer);
        definition = pitem->structureDefinition(eo.encodingTypeId());
        if (definition.isNull()) {
            errlogPrintf("Cannot get a structure definition for item %s element %s (dataTypeId %s "
                         "encodingTypeId %s) - no access to type dictionary?\n",
                         pitem->nodeid->toString().toUtf8(),
                         name.c_str(),
                         eo.dataTypeId().toString().toUtf8(),
                         eo.encodingTypeId().toString().toUtf8());
            return;
        }
    }

    if (!mapped)
        createMap(timefrom);

    if (timefrom) {
        if (timesrc >= 0)
            pitem->tsData = epicsTimeFromUaVariant(UaGenericStructureValue(eo, definition).value(timesrc));
        else
            pitem->tsData = pitem->tsSource;
    }

    for (const auto &it : elementMap) {
        auto pelem = it.second.lock();
        OpcUa_StatusCode stat;
        if (definition.isUnion()) {
            UaGenericUnionValue genericValue(eo, definition);
            int index = genericValue.switchValue() - 1;
            if (it.first == index) {
                const UaVariant memberValue = genericValue.value();
                if (memberValue.type() == OpcUaType_ExtensionObject && !memberValue.isArray()) {
                    UaExtensionObject childEo;
                    memberValue.toExtensionObject(childEo);
                    pelem->setIncomingData(childEo, reason);
                } else {
                    pelem->setIncomingData(memberValue, reason);
                }
                stat = OpcUa_Good;
            } else {
                stat = OpcUa_BadNoData;
            }
        } else {
            const UaVariant memberValue = UaGenericStructureValue(eo, definition).value(it.first, &stat);
            if (stat == OpcUa_Good) {
                if (memberValue.type() == OpcUaType_ExtensionObject && !memberValue.isArray()) {
                    UaExtensionObject childEo;
                    memberValue.toExtensionObject(childEo);
                    pelem->setIncomingData(childEo, reason);
                } else {
                    pelem->setIncomingData(memberValue, reason);
                }
            }
        }
        if (stat == OpcUa_BadNoData) {
            const UaStructureField &field = definition.child(it.first);
            OpcUa_Variant fakeValue;
            OpcUa_Variant_Initialize(&fakeValue);
            fakeValue.Datatype = field.valueType();
            fakeValue.ArrayType = field.valueRank() != 0;
            if (debug())
                std::cerr << pitem->recConnector->getRecordName() << " element " << pelem->name
                          << (definition.isUnion() ? " not taken choice " : " absent optional ")
                          << variantTypeString((OpcUa_BuiltInType) fakeValue.Datatype)
                          << (fakeValue.ArrayType ? " array" : " scalar") << std::endl;
            pelem->setIncomingData(fakeValue, ProcessReason::readFailure);
        }
    }
}

void
DataElementUaSdkNode::setIncomingEvent (ProcessReason reason)
{
    for (const auto &it : elements) {
        auto pelem = it.lock();
        pelem->setIncomingEvent(reason);
    }
}

void
DataElementUaSdkNode::setState (const ConnectionStatus state)
{
    for (const auto &it : elements) {
        auto pelem = it.lock();
        pelem->setState(state);
    }
    if (state == ConnectionStatus::initialRead) {
        elementMap.clear();
        mapped = false;
        definition.clear();
        timesrc = -1;
    }
}

void
DataElementUaSdkNode::fillOutgoingData (const UaVariant &base, UaVariant &out)
{
    if (debug() >= 4)
        std::cout << "Element " << name << " updating structured data from " << elements.size() << " child elements"
                  << std::endl;

    out = base;
    bool isdirty = false;
    switch (out.type()) {
    case OpcUaType_ExtensionObject: {
        UaExtensionObject eoBase, eoOut;
        out.toExtensionObject(eoBase);
        fillOutgoingData(eoBase, eoOut);
        out.setExtensionObject(eoOut, OpcUa_True);
        isdirty = true; // Optimization: always mark dirty if it's a structure for now?
        // Actually, fillOutgoingData(eoBase, eoOut) should tell us if it's dirty.
        break;
    }
    case OpcUaType_LocalizedText: {
        UaLocalizedText localizedText;
        out.toLocalizedText(localizedText);
        if (!mapped)
            createMap(localizedText);

        for (const auto &it : elementMap) {
            auto pelem = it.second.lock();
            bool updated = false;
            {
                Guard G(pelem->outgoingLock);
                UaVariant memberOut;
                UaVariant memberBase;
                switch (it.first) {
                case 0:
                    memberBase.setString(localizedText.locale());
                    break;
                case 1:
                    memberBase.setString(localizedText.text());
                    break;
                }
                pelem->fillOutgoingData(memberBase, memberOut);
                if (pelem->isdirty) {
                    switch (it.first) {
                    case 0:
                        localizedText.setLocale(memberOut.toString());
                        break;
                    case 1:
                        localizedText.setText(memberOut.toString());
                        break;
                    }
                    pelem->isdirty = false;
                    isdirty = true;
                    updated = true;
                }
                if (debug() >= 4)
                    std::cout << "Data from child element " << pelem->name
                              << (updated ? " inserted into LocalizedText" : " ignored (not dirty)") << std::endl;
            }
        }
        if (isdirty)
            out.setLocalizedText(localizedText);
        break;
    }
    case OpcUaType_QualifiedName: {
        UaQualifiedName qualifiedName;
        out.toQualifiedName(qualifiedName);
        if (!mapped)
            createMap(qualifiedName);

        for (const auto &it : elementMap) {
            auto pelem = it.second.lock();
            bool updated = false;
            {
                Guard G(pelem->outgoingLock);
                UaVariant memberOut;
                UaVariant memberBase;
                switch (it.first) {
                case 0:
                    memberBase.setUInt16(qualifiedName.namespaceIndex());
                    break;
                case 1:
                    memberBase.setString(qualifiedName.name());
                    break;
                }
                pelem->fillOutgoingData(memberBase, memberOut);
                if (pelem->isdirty) {
                    OpcUa_UInt16 ns;
                    switch (it.first) {
                    case 0:
                        if (memberOut.toUInt16(ns) == OpcUa_Good)
                            qualifiedName.setNamespaceIndex(ns);
                        break;
                    case 1:
                        qualifiedName.setQualifiedName(memberOut.toString(), qualifiedName.namespaceIndex());
                        break;
                    }
                    pelem->isdirty = false;
                    isdirty = true;
                    updated = true;
                }
                if (debug() >= 4)
                    std::cout << "Data from child element " << pelem->name
                              << (updated ? " inserted into QualifiedName" : " ignored (not dirty)") << std::endl;
            }
        }
        if (isdirty)
            out.setQualifiedName(qualifiedName);
        break;
    }
    default:
        errlogPrintf("%s: %s is no structured data but a %s\n",
                     pitem->recConnector->getRecordName(),
                     name.c_str(),
                     variantTypeString(out.type()));
    }
    this->isdirty = isdirty;
}

void
DataElementUaSdkNode::fillOutgoingData (const UaExtensionObject &base, UaExtensionObject &out)
{
    if (debug() >= 4)
        std::cout << "Element " << name << " updating structured data from " << elements.size() << " child elements"
                  << std::endl;

    out = base;
    if (out.encoding() == UaExtensionObject::EncodeableObject)
        out.changeEncoding(UaExtensionObject::Binary);

    UaStructureDefinition def = pitem->structureDefinition(out.encodingTypeId());
    if (def.isNull()) {
        errlogPrintf("Cannot get a structure definition for extensionObject with dataTypeID %s "
                     "/ encodingTypeID %s - "
                     "check access to type "
                     "dictionary\n",
                     out.dataTypeId().toString().toUtf8(),
                     out.encodingTypeId().toString().toUtf8());
        return;
    }

    if (!mapped)
        createMap();

    bool isdirty = false;
    if (def.isUnion()) {
        UaGenericUnionValue genericUnion(out, def);
        for (const auto &it : elementMap) {
            auto pelem = it.second.lock();
            bool updated = false;
            {
                Guard G(pelem->outgoingLock);
                if (pelem->isDirty()) {
                    UaVariant memberOut;
                    UaVariant memberBase = genericUnion.value();
                    pelem->fillOutgoingData(memberBase, memberOut);
                    if (pelem->isDirty()) {
                        genericUnion.setValue(it.first + 1, memberOut);
                        isdirty = true;
                        updated = true;
                    }
                }
            }
            if (debug() >= 4)
                std::cout << "Data from child element " << pelem->name
                          << (updated ? " inserted into union" : " ignored (not dirty)") << std::endl;
        }
        if (isdirty)
            genericUnion.toExtensionObject(out);

    } else {
        UaGenericStructureValue genericStruct(out, def);
        for (const auto &it : elementMap) {
            auto pelem = it.second.lock();
            bool updated = false;
            {
                Guard G(pelem->outgoingLock);
                if (pelem->isDirty()) {
                    UaVariant memberOut;
                    OpcUa_StatusCode stat;
                    UaVariant memberBase = genericStruct.value(it.first, &stat);
                    pelem->fillOutgoingData(memberBase, memberOut);
                    if (pelem->isDirty()) {
                        genericStruct.setField(it.first, memberOut);
                        isdirty = true;
                        updated = true;
                    }
                }
            }
            if (debug() >= 4)
                std::cout << "Data from child element " << pelem->name
                          << (updated ? " inserted into structure" : " ignored (not dirty)") << std::endl;
        }
        if (isdirty)
            genericStruct.toExtensionObject(out);
    }
    this->isdirty = isdirty;
}

void
DataElementUaSdkNode::clearOutgoingData ()
{
    for (const auto &it : elements)
        if (auto pelem = it.lock())
            pelem->clearOutgoingData();
    isdirty = false;
}

void
DataElementUaSdkNode::requestRecordProcessing (const ProcessReason reason) const
{
    for (const auto &it : elementMap) {
        auto pelem = it.second.lock();
        pelem->requestRecordProcessing(reason);
    }
}

epicsTime
DataElementUaSdkNode::epicsTimeFromUaVariant (const UaVariant &data) const
{
    UaDateTime dt;
    UaStatus s = data.toDateTime(dt);
    if (s.isGood()) {
        return ItemUaSdk::uaToEpicsTime(dt, 0);
    }
    return pitem->tsSource;
}

#define LOG_ERROR_AND_RETURN(method) \
    errlogPrintf("DataElementUaSdkNode::%s called on a node element. This should not happen.\n", #method); \
    return 1;
} // namespace DevOpcua
