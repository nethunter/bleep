#pragma once

// Keep the shared wire format behind a transport-neutral facade so Aputure
// Light and Zhiyun drivers do not depend on each other's product semantics.
#include "devices/aputure_light/store.h"

namespace studio::mesh {

using NetworkRecord = aputure_light::MeshNetworkRecord;
using NodeRecord = aputure_light::MeshNodeRecord;
using StoreData = aputure_light::MeshStoreData;
using Store = aputure_light::MeshStore;
using SequenceAllocator = aputure_light::SequenceAllocator;

inline NodeRecord* findNode(StoreData& data, InstanceId instanceId) {
  return aputure_light::findNode(data, instanceId);
}

inline const NodeRecord* findNode(const StoreData& data,
                                  InstanceId instanceId) {
  return aputure_light::findNode(data, instanceId);
}

inline bool upsertNode(StoreData& data, const NodeRecord& node) {
  return aputure_light::upsertNode(data, node);
}

inline bool removeNode(StoreData& data, InstanceId instanceId) {
  return aputure_light::removeNode(data, instanceId);
}

inline uint16_t defaultControlGroupAddress(const StoreData& data,
                                           const NodeRecord& node) {
  return aputure_light::defaultControlGroupAddress(data, node);
}

inline uint8_t nextZhiyunRoutingSelector(const StoreData& data) {
  return aputure_light::nextZhiyunRoutingSelector(data);
}

}  // namespace studio::mesh
