/*************************************************************************\
* Copyright (c) 2018-2026 ITER Organization.
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
#include <uagenericoptionsetvalue.h>
#include <uagenericunionvalue.h>
#include <uaoptionsetdefinition.h>
#include <uastructurefielddata.h>

#include <alarm.h>
#include <epicsTime.h>
#include <errlog.h>

#include "ItemUaSdk.h"
#include "RecordConnector.h"

namespace DevOpcua {

namespace {

// Gives access to the SDK's decoding of a single structure field
// (UaAbstractGenericValue::readField() is protected and does not depend on the object)
class FieldReader : public UaGenericStructureValue
{
public:
    using UaAbstractGenericValue::readField;
};

const FieldReader &
fieldReader ()
{
    static const FieldReader reader;
    return reader;
}

// Compatibility with older UA SDK versions (seen with <= 1.7.2), detected at compile time
// (the 'int' overload is preferred if it compiles, else the 'long' one is used):
// - UaBinaryDecoder has no nesting depth check
// - UaAbstractGenericValue::readField() has no status argument
// - UaAbstractGenericValue::toVariant() returns void

template<typename D>
auto
checkNestingDepth (D &decoder, int) -> decltype(decoder.checkNestingDepth())
{
    return decoder.checkNestingDepth();
}

template<typename D>
OpcUa_StatusCode
checkNestingDepth (D &, long)
{
    return OpcUa_Good;
}

template<typename D>
auto
decrementNestingDepth (D &decoder, int) -> decltype(decoder.decrementNestingDepth())
{
    decoder.decrementNestingDepth();
}

template<typename D>
void
decrementNestingDepth (D &, long)
{}

template<typename R>
auto
readField (const R &reader, UaAbstractDecoder *decoder, const UaStructureField &field, OpcUa_StatusCode &status, int)
    -> decltype(reader.readField(decoder, field, status))
{
    return reader.readField(decoder, field, status);
}

template<typename R>
UaStructureFieldData
readField (const R &reader, UaAbstractDecoder *decoder, const UaStructureField &field, OpcUa_StatusCode &status, long)
{
    status = OpcUa_Good; // errors are not reported
    return reader.readField(decoder, field);
}

template<typename T>
auto
toVariant (const T &value, UaVariant &variant, int) -> decltype(OpcUa_StatusCode(value.toVariant(variant)))
{
    return value.toVariant(variant);
}

template<typename T>
OpcUa_StatusCode
toVariant (const T &value, UaVariant &variant, long)
{
    value.toVariant(variant);
    return OpcUa_Good;
}

// Member is a scalar structure (not a union) - decoded by the SDK into a UaGenericStructureValue
// (see UaAbstractGenericValue::readField())
bool
isNestedStructure (const UaStructureField &field)
{
    if (field.arrayType() != UaStructureField::ArrayType_Scalar || field.valueType() != OpcUaType_ExtensionObject
        || field.typeId() == OpcUaId_Structure)
        return false;
    const UaStructureDefinition def = field.structureDefinition();
    return !def.isNull() && !def.isUnion();
}

// Member of a simple type (scalar, no option set) that can be read directly
// (decoded and stored in the variant as UaAbstractGenericValue::readField() does it)
OpcUa_BuiltInType
simpleFieldType (const UaStructureField &field)
{
    if (field.arrayType() != UaStructureField::ArrayType_Scalar || !field.optionSetDefinition().isNull())
        return OpcUaType_Null;
    switch (field.valueType()) {
    case OpcUaType_Boolean:
    case OpcUaType_SByte:
    case OpcUaType_Byte:
    case OpcUaType_Int16:
    case OpcUaType_UInt16:
    case OpcUaType_Int32:
    case OpcUaType_UInt32:
    case OpcUaType_Int64:
    case OpcUaType_UInt64:
    case OpcUaType_Float:
    case OpcUaType_Double:
    case OpcUaType_DateTime:
        return field.valueType();
    default:
        return OpcUaType_Null;
    }
}

// Read a member of a simple type (see simpleFieldType()),
// avoiding the UaStructureFieldData object and copy of readField()
OpcUa_StatusCode
readSimpleField (UaBinaryDecoder &decoder, OpcUa_BuiltInType type, UaVariant &value)
{
    static const UaString noName; // field names are not used by the binary decoder
    OpcUa_StatusCode status = OpcUa_BadInternalError;

#define READ_SIMPLE_FIELD(T)                    \
    case OpcUaType_##T: {                       \
        OpcUa_##T v;                            \
        status = decoder.read##T(noName, v);    \
        if (OpcUa_IsGood(status))               \
            value.set##T(v);                    \
        break;                                  \
    }

    switch (type) {
    case OpcUaType_Boolean: {
        OpcUa_Boolean v;
        status = decoder.readBoolean(noName, v);
        if (OpcUa_IsGood(status))
            value.setBool(v);
        break;
    }
    READ_SIMPLE_FIELD(SByte)
    READ_SIMPLE_FIELD(Byte)
    READ_SIMPLE_FIELD(Int16)
    READ_SIMPLE_FIELD(UInt16)
    READ_SIMPLE_FIELD(Int32)
    READ_SIMPLE_FIELD(UInt32)
    READ_SIMPLE_FIELD(Int64)
    READ_SIMPLE_FIELD(UInt64)
    READ_SIMPLE_FIELD(Float)
    READ_SIMPLE_FIELD(Double)
    case OpcUaType_DateTime: {
        UaDateTime v;
        status = decoder.readDateTime(noName, v);
        if (OpcUa_IsGood(status))
            value.setDateTime(v);
        break;
    }
    default:
        break;
    }
#undef READ_SIMPLE_FIELD
    return status;
}

// Field data as variant (as UaGenericStructureValue::value() does it)
OpcUa_StatusCode
fieldDataToVariant (const UaStructureFieldData &data, UaVariant &value)
{
    switch (data.type()) {
    case DataVariant:
        value = data.variant();
        return OpcUa_Good;
    case DataGenericStructure:
        return toVariant(data.value(), value, 0);
    case DataGenericStructureArray:
        return toVariant(data.valueArray(), value, 0);
    case DataGenericUnion:
        return toVariant(data.genericUnion(), value, 0);
    case DataGenericUnionArray:
        return toVariant(data.genericUnionArray(), value, 0);
    case DataGenericOptionSet:
        return toVariant(data.genericOptionSet(), value, 0);
    case DataGenericOptionSetArray:
        return toVariant(data.genericOptionSetArray(), value, 0);
    default:
        return OpcUa_BadInternalError;
    }
}

} // namespace

DataElementUaSdkNode::DataElementUaSdkNode (const std::string &name, class ItemUaSdk *pitem)
    : DataElementUaSdk(name, pitem)
    , timesrc(-1)
    , mapped(false)
{
    pitem->dataTreeNoOfNodes++;
}

DataElementUaSdkNode::~DataElementUaSdkNode()
{
    pitem->dataTreeNoOfNodes--;
}

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

    // Index the structure members by name once
    // (avoids comparing every child element with every member: O(n^2) for wide structures)
    const int noOfMembers = definition.childrenCount();
    std::unordered_map<std::string, int> memberIndex;
    memberIndex.reserve(noOfMembers);
    for (int i = 0; i < noOfMembers; i++)
        memberIndex.emplace(definition.child(i).name().toUtf8(), i);

    if (timefrom) {
        auto found = memberIndex.find(*timefrom);
        timesrc = (found != memberIndex.end()) ? found->second : -1;
        if (timesrc == -1) {
            errlogPrintf("%s: timestamp element %s not found - using source timestamp\n",
                         pitem->recConnector->getRecordName(),
                         timefrom->c_str());
        } else {
            OpcUa_BuiltInType t = definition.child(timesrc).valueType();
            if (t != OpcUaType_DateTime) {
                errlogPrintf("%s: timestamp element %s has invalid type %s - using "
                             "source timestamp\n",
                             pitem->recConnector->getRecordName(),
                             timefrom->c_str(),
                             variantTypeString(t));
                timesrc = -1;
            }
        }
    }
    fields.resize(noOfMembers);
    nestedStructure.resize(noOfMembers);
    simpleType.resize(noOfMembers);
    memberElements.assign(noOfMembers, std::weak_ptr<DataElementUaSdk>());
    for (int i = 0; i < noOfMembers; i++) {
        fields[i] = definition.child(i);
        nestedStructure[i] = isNestedStructure(fields[i]);
        simpleType[i] = simpleFieldType(fields[i]);
    }
    for (const auto &it : elements) {
        auto pelem = it.lock();
        auto found = memberIndex.find(pelem->name);
        if (found != memberIndex.end()) {
            elementMap.insert({found->second, it});
            memberElements[found->second] = it;
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
    incomingData = value;

    if (debug() >= 5)
        std::cout << "Element " << name << " splitting structured data to " << elements.size() << " child elements"
                  << std::endl;

    switch (value.type()) {
    case OpcUaType_ExtensionObject: {
        UaExtensionObject extensionObject;
        value.toExtensionObject(extensionObject);
        if (extensionObject.encoding() == UaExtensionObject::EncodeableObject)
            extensionObject.changeEncoding(UaExtensionObject::Binary);

        if (definition.isNull()) {
            definition = pitem->structureDefinition(extensionObject.encodingTypeId());
            if (definition.isNull()) {
                errlogPrintf("Cannot get a structure definition for item %s element %s (dataTypeId %s "
                             "encodingTypeId %s) - no access to type dictionary?\n",
                             pitem->nodeid->toString().toUtf8(),
                             name.c_str(),
                             extensionObject.dataTypeId().toString().toUtf8(),
                             extensionObject.encodingTypeId().toString().toUtf8());
                return;
            }
        }

        if (!mapped)
            createMap(timefrom);

        if (timefrom)
            pitem->tsData = pitem->tsSource; // unless set from the structure member

        if (definition.isUnion()) {
            UaGenericUnionValue genericUnion(extensionObject, definition);
            const int unionIndex = genericUnion.switchValue() - 1;
            for (const auto &it : elementMap) {
                auto pelem = it.second.lock();
                if (it.first == unionIndex)
                    pelem->setIncomingData(genericUnion.value(), reason);
                else
                    setMissingMember(pelem, it.first);
            }
        } else if (extensionObject.encoding() == UaExtensionObject::Binary) {
            // Decode the body directly into the child elements
            // (as UaGenericStructureValue::setExtensionObject() sets up the decoder)
            const OpcUa_ExtensionObject *pext = static_cast<const OpcUa_ExtensionObject *>(extensionObject);
            UaByteString body;
            body.attach(&pext->Body.Binary);
            UaBinaryDecoder decoder;
            OpcUa_StatusCode status = decoder.create(body).statusCode();
            if (OpcUa_IsGood(status)) {
                // Leaf updates are held back until the data timestamp (member) has been decoded
                UpdateSink pending;
                const bool deferUpdates = (timefrom && timesrc >= 0);
                status = decodeStructure(decoder, reason, deferUpdates ? &pending : nullptr);
                if (OpcUa_IsGood(status)) {
                    for (auto &u : pending) {
                        if (u.missingIndex >= 0)
                            u.owner->setMissingMember(u.pelem, u.missingIndex);
                        else
                            u.pelem->takeIncomingData(u.value, u.reason);
                    }
                }
                decoder.clear();
            }
            body.detach();
            if (OpcUa_IsBad(status))
                errlogPrintf("%s: decoding structured data of element %s (encodingTypeId %s) failed: %s\n",
                             pitem->recConnector->getRecordName(),
                             name.c_str(),
                             extensionObject.encodingTypeId().toString().toUtf8(),
                             UaStatus(status).toString().toUtf8());
        } else {
            UaGenericStructureValue genericStruct(extensionObject, definition);
            if (timefrom && timesrc >= 0)
                pitem->tsData = epicsTimeFromUaVariant(genericStruct.value(timesrc));
            dissectStructure(genericStruct, reason);
        }
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

OpcUa_StatusCode
DataElementUaSdkNode::decodeStructure (UaBinaryDecoder &decoder, ProcessReason reason, UpdateSink *deferred)
{
    // Same decoding as UaGenericStructureValue::decode()
    OpcUa_StatusCode status = checkNestingDepth(decoder, 0);
    if (OpcUa_IsBad(status))
        return status;

    const bool hasOptionalFields = definition.hasOptionalFields();
    OpcUa_UInt32 mask = 0;
    OpcUa_UInt32 bit = 1;
    if (hasOptionalFields)
        status = decoder.readUInt32("EncodingMask", mask);

    const int noOfMembers = static_cast<int>(fields.size());
    for (int i = 0; i < noOfMembers && !OpcUa_IsBad(status); i++) {
        const UaStructureField &field = fields[i];
        const std::shared_ptr<DataElementUaSdk> pelem = memberElements[i].lock();

        if (hasOptionalFields && field.isOptional()) {
            const bool present = (mask & bit) != 0;
            bit <<= 1;
            if (!present) {
                if (pelem)
                    deliverMissing(pelem, i, deferred);
                continue;
            }
        }

        if (nestedStructure[i]) {
            if (!pelem) {
                status = skipStructure(decoder, field.structureDefinition());
                continue;
            }
            if (!pelem->isLeaf()) {
                status = std::static_pointer_cast<DataElementUaSdkNode>(pelem)
                             ->decodeNestedStructure(decoder, field.structureDefinition(), reason, deferred);
                continue;
            }
            // A leaf connected to a structure member gets the structure (as ExtensionObject)
        }

        UaVariant memberValue;
        OpcUa_StatusCode valueStatus;
        if (simpleType[i] != OpcUaType_Null) {
            status = readSimpleField(decoder, simpleType[i], memberValue);
            if (OpcUa_IsBad(status))
                break;
            valueStatus = OpcUa_IsGood(status) ? OpcUa_Good : OpcUa_BadNoData;
        } else {
            const UaStructureFieldData data = readField(fieldReader(), &decoder, field, status, 0);
            if (OpcUa_IsBad(status))
                break;
            if (!pelem && i != timesrc)
                continue;
            valueStatus = data.isEmpty() ? OpcUa_BadNoData : fieldDataToVariant(data, memberValue);
        }
        if (!pelem && i != timesrc)
            continue;
        if (valueStatus == OpcUa_BadNoData) {
            if (pelem && field.isOptional())
                deliverMissing(pelem, i, deferred);
            continue;
        }
        if (valueStatus != OpcUa_Good)
            continue;
        if (i == timesrc)
            pitem->tsData = epicsTimeFromUaVariant(memberValue);
        if (pelem)
            deliver(pelem, memberValue, reason, deferred);
    }

    decrementNestingDepth(decoder, 0);
    return status;
}

OpcUa_StatusCode
DataElementUaSdkNode::decodeNestedStructure (UaBinaryDecoder &decoder,
                                             const UaStructureDefinition &memberDefinition,
                                             ProcessReason reason,
                                             UpdateSink *deferred)
{
    // The current value is provided by the parent when needed (see buildOutgoingData())
    incomingData.clear();

    if (debug() >= 5)
        std::cout << "Element " << name << " decoding nested structure to " << elements.size() << " child elements"
                  << std::endl;

    // The parent's definition includes the nested one - no need to query the session
    if (definition.isNull())
        definition = memberDefinition;
    if (!mapped)
        createMap();

    return decodeStructure(decoder, reason, deferred);
}

OpcUa_StatusCode
DataElementUaSdkNode::skipStructure (UaBinaryDecoder &decoder, const UaStructureDefinition &def)
{
    OpcUa_StatusCode status = checkNestingDepth(decoder, 0);
    if (OpcUa_IsBad(status))
        return status;

    const bool hasOptionalFields = def.hasOptionalFields();
    OpcUa_UInt32 mask = 0;
    OpcUa_UInt32 bit = 1;
    if (hasOptionalFields)
        status = decoder.readUInt32("EncodingMask", mask);

    const int noOfMembers = def.childrenCount();
    for (int i = 0; i < noOfMembers && !OpcUa_IsBad(status); i++) {
        const UaStructureField field = def.child(i);
        if (hasOptionalFields && field.isOptional()) {
            const bool present = (mask & bit) != 0;
            bit <<= 1;
            if (!present)
                continue;
        }
        const OpcUa_BuiltInType type = simpleFieldType(field);
        if (type != OpcUaType_Null) {
            UaVariant discard;
            status = readSimpleField(decoder, type, discard);
        } else if (isNestedStructure(field)) {
            status = skipStructure(decoder, field.structureDefinition());
        } else {
            readField(fieldReader(), &decoder, field, status, 0);
        }
    }

    decrementNestingDepth(decoder, 0);
    return status;
}

void
DataElementUaSdkNode::deliver (const std::shared_ptr<DataElementUaSdk> &pelem,
                               UaVariant &value,
                               ProcessReason reason,
                               UpdateSink *deferred)
{
    if (deferred) {
        deferred->push_back({pelem, UaVariant(), reason, this, -1});
        // Move the data into the pending update (no deep copy)
        deferred->back().value.attach(value);
        value.detach();
    } else {
        pelem->takeIncomingData(value, reason);
    }
}

void
DataElementUaSdkNode::deliverMissing (const std::shared_ptr<DataElementUaSdk> &pelem, int index, UpdateSink *deferred)
{
    if (deferred)
        deferred->push_back({pelem, UaVariant(), ProcessReason::readFailure, this, index});
    else
        setMissingMember(pelem, index);
}

void
DataElementUaSdkNode::dissectStructure (const UaGenericStructureValue &genericStruct, ProcessReason reason)
{
    for (const auto &it : elementMap) {
        auto pelem = it.second.lock();
        OpcUa_StatusCode stat = OpcUa_Good;
        const UaVariant memberValue = genericStruct.value(it.first, &stat);
        if (stat == OpcUa_Good)
            pelem->setIncomingData(memberValue, reason);
        else if (stat == OpcUa_BadNoData)
            setMissingMember(pelem, it.first);
    }
}

void
DataElementUaSdkNode::setMissingMember (const std::shared_ptr<DataElementUaSdk> &pelem, int index)
{
    const UaStructureField &field = definition.child(index);
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
        fields.clear();
        nestedStructure.clear();
        simpleType.clear();
        memberElements.clear();
    }
}

const UaVariant &
DataElementUaSdkNode::getOutgoingData ()
{
    return buildOutgoingData(incomingData);
}

const UaVariant &
DataElementUaSdkNode::buildOutgoingData (const UaVariant &current)
{
    if (debug() >= 4)
        std::cout << "Element " << name << " updating structured data from " << elements.size() << " child elements"
                  << std::endl;

    outgoingData = current;
    bool isdirty = false;
    switch (outgoingData.type()) {
    case OpcUaType_ExtensionObject: {
        UaExtensionObject extensionObject;
        outgoingData.toExtensionObject(extensionObject);
        if (extensionObject.encoding() == UaExtensionObject::EncodeableObject)
            extensionObject.changeEncoding(UaExtensionObject::Binary);

        // Use the definition cached by the incoming data path (no type catalog query for every write);
        // the cache is only read here, it is set and cleared by the client thread
        UaStructureDefinition definition = this->definition;
        if (definition.isNull())
            definition = pitem->structureDefinition(extensionObject.encodingTypeId());
        if (definition.isNull()) {
            errlogPrintf("Cannot get a structure definition for item %s element %s (dataTypeId %s "
                         "encodingTypeId %s) - no access to type dictionary?\n",
                         pitem->nodeid->toString().toUtf8(),
                         name.c_str(),
                         extensionObject.dataTypeId().toString().toUtf8(),
                         extensionObject.encodingTypeId().toString().toUtf8());
            return outgoingData;
        }

        if (!mapped)
            createMap();

        if (definition.isUnion()) {
            UaGenericUnionValue genericUnion(extensionObject, definition);
            for (const auto &it : elementMap) {
                auto pelem = it.second.lock();
                bool updated = false;
                {
                    Guard G(pelem->outgoingLock);
                    if (pelem->isDirty()) {
                        genericUnion.setValue(it.first + 1, pelem->getOutgoingData());
                        pelem->isdirty = false;
                        isdirty = true;
                        updated = true;
                    }
                }
                if (debug() >= 4)
                    std::cout << "Data from child element " << pelem->name
                              << (updated ? " inserted into union" : " ignored (not dirty)") << std::endl;
            }
            if (isdirty)
                genericUnion.toExtensionObject(extensionObject);

        } else {
            UaGenericStructureValue genericStruct(extensionObject, definition);
            for (const auto &it : elementMap) {
                auto pelem = it.second.lock();
                bool updated = false;
                {
                    Guard G(pelem->outgoingLock);
                    if (pelem->isDirty()) {
                        if (pelem->isLeaf())
                            genericStruct.setField(it.first, pelem->getOutgoingData());
                        else
                            // Nested nodes do not keep their incoming value (see decodeNestedStructure())
                            genericStruct.setField(it.first,
                                                   std::static_pointer_cast<DataElementUaSdkNode>(pelem)
                                                       ->buildOutgoingData(genericStruct.value(it.first)));
                        pelem->isdirty = false;
                        isdirty = true;
                        updated = true;
                    }
                }
                if (debug() >= 4)
                    std::cout << "Data from child element " << pelem->name
                              << (updated ? " inserted into structure" : " ignored (not dirty)") << std::endl;
            }
            if (isdirty)
                genericStruct.toExtensionObject(extensionObject);
        }

        if (isdirty)
            outgoingData.setExtensionObject(extensionObject, OpcUa_True);
        break;
    }
    case OpcUaType_LocalizedText: {
        UaLocalizedText localizedText;
        outgoingData.toLocalizedText(localizedText);
        if (!mapped)
            createMap(localizedText);

        for (const auto &it : elementMap) {
            auto pelem = it.second.lock();
            bool updated = false;
            {
                Guard G(pelem->outgoingLock);
                if (pelem->isDirty()) {
                    switch (it.first) {
                    case 0:
                        localizedText.setLocale(pelem->getOutgoingData().toString());
                        break;
                    case 1:
                        localizedText.setText(pelem->getOutgoingData().toString());
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
            outgoingData.setLocalizedText(localizedText);
        break;
    }
    case OpcUaType_QualifiedName: {
        UaQualifiedName qualifiedName;
        outgoingData.toQualifiedName(qualifiedName);
        if (!mapped)
            createMap(qualifiedName);

        for (const auto &it : elementMap) {
            auto pelem = it.second.lock();
            bool updated = false;
            {
                Guard G(pelem->outgoingLock);
                OpcUa_UInt16 ns;
                if (pelem->isDirty()) {
                    switch (it.first) {
                    case 0:
                        if (pelem->getOutgoingData().toUInt16(ns) == OpcUa_Good)
                            qualifiedName.setNamespaceIndex(ns);
                        break;
                    case 1:
                        qualifiedName.setQualifiedName(pelem->getOutgoingData().toString(),
                                                       qualifiedName.namespaceIndex());
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
            outgoingData.setQualifiedName(qualifiedName);
        break;
    }
    default:
        errlogPrintf("%s: %s is no structured data but a %s\n",
                     pitem->recConnector->getRecordName(),
                     name.c_str(),
                     variantTypeString(outgoingData.type()));
        return outgoingData;
    }
    if (debug() >= 4) {
        if (isdirty)
            std::cout << "Encoding changed data structure to outgoingData of element " << name << std::endl;
        else
            std::cout << "Returning unchanged outgoingData of element " << name << std::endl;
    }
    return outgoingData;
}

void
DataElementUaSdkNode::clearOutgoingData ()
{
    outgoingData.clear();
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
