#pragma once

#include "core/device_types.h"
#include "core/mesh/mesh_store.h"

namespace studio::mesh {

// Reference-counted ownership and qualification policy for the one physical
// Mesh Proxy bearer shared by logical Aputure and Zhiyun light instances. The
// device runtime remains the BLE delegate; this object decides who owns the
// bearer and which saved node is qualified to proxy for the active users.
class ProxyTransport {
 public:
  bool acquire(InstanceId instanceId);
  void release(InstanceId instanceId);
  bool contains(InstanceId instanceId) const;
  bool empty() const;
  InstanceId preferred(const StoreData& store) const;
  bool requiresZhiyunService(const StoreData& store) const;

 private:
  InstanceId users_[CONFIG_MAX_ACTIVE_INSTANCES] = {};
};

}  // namespace studio::mesh
