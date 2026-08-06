#pragma once

#include "core/device_types.h"

namespace studio {

enum class RegistryStatus : uint8_t {
  Ok,
  Full,
  Invalid,
  DuplicateDriver,
  NotFound,
};

class DeviceRegistry {
 public:
  size_t capacity() const { return CONFIG_MAX_DEVICE_INSTANCES; }
  size_t count() const { return count_; }
  bool initialized() const { return initialized_; }
  InstanceId nextInstanceId() const { return nextInstanceId_; }

  const DeviceRecord* at(size_t index) const;
  DeviceRecord* at(size_t index);
  const DeviceRecord* find(InstanceId instanceId) const;
  DeviceRecord* find(InstanceId instanceId);
  size_t countByDriver(DriverId driverId) const;

  RegistryStatus add(DriverId driverId, const char* displayName, uint8_t maxForDriver,
                     InstanceId& outId);
  RegistryStatus commitPrepared(const DeviceRecord& record,
                                uint8_t maxForDriver);
  RegistryStatus remove(InstanceId instanceId);
  RegistryStatus rename(InstanceId instanceId, const char* displayName);
  RegistryStatus setEnabled(InstanceId instanceId, bool enabled);
  RegistryStatus updatePairing(InstanceId instanceId, const char* address, uint8_t addressType,
                               const char* advertisedName);
  RegistryStatus clearPairing(InstanceId instanceId);
  RegistryStatus configureHomeAssistant(InstanceId instanceId,
                                        HomeAssistantDomain domain,
                                        const char* entityId);

  void clear(bool initialized = true);
  bool restore(const DeviceRecord* records, size_t count, InstanceId nextInstanceId,
               bool initialized);

 private:
  DeviceRecord records_[CONFIG_MAX_DEVICE_INSTANCES] = {};
  size_t count_ = 0;
  InstanceId nextInstanceId_ = 1;
  bool initialized_ = false;
};

}  // namespace studio
