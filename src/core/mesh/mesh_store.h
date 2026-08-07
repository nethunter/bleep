#pragma once

// The persisted v1 blob keeps its original AMSH magic and implementation so
// installed Amaran meshes migrate without a destructive rewrite. New mesh
// drivers use this neutral facade rather than depending on Amaran semantics.
#include "devices/amaran_light/store.h"

namespace studio::mesh {

using NetworkRecord = amaran_light::MeshNetworkRecord;
using NodeRecord = amaran_light::MeshNodeRecord;
using StoreData = amaran_light::MeshStoreData;
using Store = amaran_light::MeshStore;
using SequenceAllocator = amaran_light::SequenceAllocator;

inline NodeRecord* findNode(StoreData& data, InstanceId instanceId) {
  return amaran_light::findNode(data, instanceId);
}

inline const NodeRecord* findNode(const StoreData& data,
                                  InstanceId instanceId) {
  return amaran_light::findNode(data, instanceId);
}

inline bool upsertNode(StoreData& data, const NodeRecord& node) {
  return amaran_light::upsertNode(data, node);
}

inline bool removeNode(StoreData& data, InstanceId instanceId) {
  return amaran_light::removeNode(data, instanceId);
}

}  // namespace studio::mesh
