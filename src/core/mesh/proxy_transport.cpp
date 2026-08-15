#include "core/mesh/proxy_transport.h"

namespace studio::mesh {

bool ProxyTransport::acquire(InstanceId instanceId) {
  if (instanceId == kInvalidInstanceId) return false;
  if (contains(instanceId)) return true;
  for (InstanceId& user : users_) {
    if (user != kInvalidInstanceId) continue;
    user = instanceId;
    return true;
  }
  return false;
}

void ProxyTransport::release(InstanceId instanceId) {
  for (InstanceId& user : users_) {
    if (user == instanceId) user = kInvalidInstanceId;
  }
}

bool ProxyTransport::contains(InstanceId instanceId) const {
  for (InstanceId user : users_) {
    if (user == instanceId) return true;
  }
  return false;
}

bool ProxyTransport::empty() const {
  for (InstanceId user : users_) {
    if (user != kInvalidInstanceId) return false;
  }
  return true;
}

InstanceId ProxyTransport::preferred(const StoreData& store) const {
  for (InstanceId user : users_) {
    const NodeRecord* node = findNode(store, user);
    if (node != nullptr && node->model == DriverId::ZhiyunLight) return user;
  }
  for (InstanceId user : users_) {
    if (user != kInvalidInstanceId) return user;
  }
  return kInvalidInstanceId;
}

bool ProxyTransport::requiresZhiyunService(const StoreData& store) const {
  const NodeRecord* node = findNode(store, preferred(store));
  return node != nullptr && node->model == DriverId::ZhiyunLight;
}

}  // namespace studio::mesh
