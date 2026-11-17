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

#ifndef DEVOPCUA_DATAELEMENTUASDKNODE_H
#define DEVOPCUA_DATAELEMENTUASDKNODE_H

#include <vector>
#include <unordered_map>
#include <memory>

#include <uabinarydecoder.h>
#include <uagenericstructurevalue.h>
#include <uastructurefield.h>
#include <uavariant.h>

#include "DataElementUaSdk.h"

namespace DevOpcua {

class DataElementUaSdkNode : public DataElementUaSdk
{
public:
    DataElementUaSdkNode(const std::string &name, class ItemUaSdk *pitem);
    virtual ~DataElementUaSdkNode () override;

    /* ElementTree node interface methods */
    virtual bool isLeaf() const override { return false; }
    virtual void addChild(std::weak_ptr<DataElementUaSdk> elem) override;
    virtual std::shared_ptr<DataElementUaSdk> findChild(const std::string &name) const override;

    virtual void show(const int level, const unsigned int indent) const override;

    virtual void setIncomingData(const UaVariant &value,
                                 ProcessReason reason,
                                 const std::string *timefrom = nullptr,
                                 const UaNodeId *typeId = nullptr) override;
    virtual void setIncomingEvent(ProcessReason reason) override;
    virtual void setState(const ConnectionStatus state) override;
    virtual const UaVariant &getOutgoingData() override;
    virtual void clearOutgoingData() override;
    virtual void requestRecordProcessing(const ProcessReason reason) const override;

private:
    virtual bool isDirty() const override { return true; }
    virtual void markAsDirty() override {}

    /** @brief Update for a child element, held back until the data timestamp is known. */
    struct PendingUpdate
    {
        std::shared_ptr<DataElementUaSdk> pelem;
        UaVariant value;
        ProcessReason reason;
        DataElementUaSdkNode *owner; /**< node whose structure contains the member */
        int missingIndex;            /**< >= 0: absent member (index into owner's definition), value unused */
    };
    using UpdateSink = std::vector<PendingUpdate>;

    /**
     * @brief Decode the binary encoded body of this node's structure, pushing the members
     * directly into the child elements.
     *
     * Nested structures are decoded recursively into the corresponding child nodes,
     * without creating intermediate UaGenericStructureValue objects (whose construction
     * by the SDK creates default values for the complete subtree on every level).
     *
     * @param decoder  binary decoder, positioned at the start of the structure
     * @param reason  process reason for the child elements
     * @param deferred  if not null, collect the updates (instead of delivering them)
     * @return status of decoding (bad: data malformed)
     */
    OpcUa_StatusCode decodeStructure(UaBinaryDecoder &decoder, ProcessReason reason, UpdateSink *deferred);
    /** @brief Decode a nested structure (member of the parent) into this node. */
    OpcUa_StatusCode decodeNestedStructure(UaBinaryDecoder &decoder,
                                           const UaStructureDefinition &memberDefinition,
                                           ProcessReason reason,
                                           UpdateSink *deferred);
    /** @brief Decode a structure that has no element in the tree, discarding the data. */
    static OpcUa_StatusCode skipStructure(UaBinaryDecoder &decoder, const UaStructureDefinition &def);

    /** @brief Deliver (or collect) the update for a child element, taking over the value. */
    void deliver(const std::shared_ptr<DataElementUaSdk> &pelem,
                 UaVariant &value,
                 ProcessReason reason,
                 UpdateSink *deferred);
    void deliverMissing(const std::shared_ptr<DataElementUaSdk> &pelem, int index, UpdateSink *deferred);

    /** @brief Fallback dissection of an already decoded structure (non-binary encodings). */
    void dissectStructure(const UaGenericStructureValue &genericStruct, ProcessReason reason);
    void setMissingMember(const std::shared_ptr<DataElementUaSdk> &pelem, int index);

    /**
     * @brief Create the outgoing data from the current value and the changes of the child elements.
     *
     * @param current  current value of this node (for nested structures provided by the parent)
     */
    const UaVariant &buildOutgoingData(const UaVariant &current);

    void createMap(const std::string *timefrom = nullptr);
    void createMap(const UaLocalizedText&);
    void createMap(const UaQualifiedName&);

    epicsTime epicsTimeFromUaVariant(const UaVariant &data) const;

    std::vector<std::weak_ptr<DataElementUaSdk>> elements;
    std::unordered_map<int, std::weak_ptr<DataElementUaSdk>> elementMap;
    UaStructureDefinition definition; /**< cache of structure definition */
    std::vector<UaStructureField> fields;                         /**< cache of the definition's members */
    std::vector<bool> nestedStructure;                             /**< member is a (scalar, non-union) structure */
    std::vector<std::weak_ptr<DataElementUaSdk>> memberElements; /**< child element per member (if any) */
    int timesrc;
    bool mapped;
};

}

#endif // DEVOPCUA_DATAELEMENTUASDKNODE_H
