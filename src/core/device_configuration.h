#pragma once

#include <cstring>

#include "core/config_store.h"

namespace studio {

class DeviceConfiguration {
 public:
  explicit DeviceConfiguration(IConfigBackend& backend) : store_(backend) {}

  ConfigLoadStatus load() { return store_.load(registry_); }
  bool initializeEmpty() {
    registry_.clear(false);
    return save();
  }
  void resetCorrupt() { registry_.clear(true); }
  bool save() { return store_.save(registry_); }

  DeviceRegistry& registry() { return registry_; }
  const DeviceRegistry& registry() const { return registry_; }
  DeviceRecord& pending() { return pending_; }
  const DeviceRecord& pending() const { return pending_; }
  bool pendingCommitFailed() const { return pendingCommitFailed_; }
  void setPendingCommitFailed(bool failed) { pendingCommitFailed_ = failed; }

  RegistryStatus prepare(DriverId driverId, const char* displayName,
                         uint8_t maxInstances, InstanceId& outId) {
    outId = kInvalidInstanceId;
    if (pending_.instanceId != kInvalidInstanceId || displayName == nullptr ||
        displayName[0] == '\0') {
      return RegistryStatus::Invalid;
    }
    if (registry_.count() >= registry_.capacity()) return RegistryStatus::Full;
    if (registry_.countByDriver(driverId) >= maxInstances) {
      return RegistryStatus::DuplicateDriver;
    }
    pending_ = {};
    pending_.instanceId = registry_.nextInstanceId();
    pending_.driverId = driverId;
    pending_.enabled = true;
    std::strncpy(pending_.displayName, displayName,
                 sizeof(pending_.displayName) - 1);
    outId = pending_.instanceId;
    pendingCommitFailed_ = false;
    return RegistryStatus::Ok;
  }

  void cancelPending() {
    pending_ = {};
    pendingCommitFailed_ = false;
  }

  bool commitPending(uint8_t maxInstances) {
    const DeviceRegistry previous = registry_;
    if (!previous.valid() ||
        registry_.commitPrepared(pending_, maxInstances) != RegistryStatus::Ok) {
      return false;
    }
    if (!save()) {
      registry_ = previous;
      pendingCommitFailed_ = true;
      return false;
    }
    cancelPending();
    return true;
  }

  template <typename Mutation>
  RegistryStatus transact(Mutation mutation) {
    const DeviceRegistry previous = registry_;
    if (!previous.valid()) return RegistryStatus::Full;
    const RegistryStatus status = mutation(registry_);
    if (status != RegistryStatus::Ok) return status;
    if (!save()) {
      registry_ = previous;
      return RegistryStatus::Invalid;
    }
    return RegistryStatus::Ok;
  }

 private:
  ConfigStore store_;
  DeviceRegistry registry_;
  DeviceRecord pending_;
  bool pendingCommitFailed_ = false;
};

}  // namespace studio
