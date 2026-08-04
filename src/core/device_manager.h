#pragma once

#include "core/command_queue.h"
#include "core/config_store.h"
#include "core/device_driver.h"
#include "core/driver_catalog.h"

namespace studio {

class DeviceManager {
 public:
  static constexpr size_t kMaxCompiledDrivers = 8;
  static constexpr size_t kMaxActiveInstances = CONFIG_MAX_ACTIVE_INSTANCES;
  // Compatibility name for existing driver/test code; this is now an active
  // instance bound, not the physical BLE link bound.
  static constexpr size_t kMaxActiveLinks = kMaxActiveInstances;

  DeviceManager(IConfigBackend& backend, ILegacySharkBackend& legacyBackend,
                DeviceDriver* const* drivers, size_t driverCount);

  bool begin();
  void loop();

  size_t count() const { return registry_.count(); }
  const DeviceRecord* at(size_t index) const { return registry_.at(index); }
  const DeviceRecord* find(InstanceId instanceId) const {
    return registry_.find(instanceId);
  }
  InstanceProfile profile(InstanceId instanceId) const;
  InstanceId foregroundInstance() const;
  size_t activeCount() const { return activeCount_; }
  bool isActive(InstanceId instanceId) const;
  bool ownedBy(InstanceId instanceId, ConnectionOwner owner) const;
  bool isRetained(InstanceId instanceId) const;

  RegistryStatus add(DriverId driverId, const char* displayName, InstanceId& outId);
  RegistryStatus remove(InstanceId instanceId);
  RegistryStatus rename(InstanceId instanceId, const char* displayName);
  RegistryStatus setEnabled(InstanceId instanceId, bool enabled);
  RegistryStatus clearPairing(InstanceId instanceId);
  RegistryStatus addHomeAssistantEntity(HomeAssistantDomain domain,
                                        const char* entityId,
                                        const char* displayName,
                                        InstanceId& outId);
  RegistryStatus rebindHomeAssistantEntity(InstanceId instanceId,
                                           HomeAssistantDomain domain,
                                           const char* entityId);
  RegistryStatus replaceHomeAssistantEntities(
      const HomeAssistantEntitySelection* selections, size_t count);

  bool acquire(InstanceId instanceId, ConnectionOwner owner);
  void release(InstanceId instanceId, ConnectionOwner owner);
  CommandStatus disconnect(InstanceId instanceId, bool confirmed = false);
  void deactivateAll();
  bool enqueue(DeviceCommand command);
  bool popResult(CommandResult& result) { return results_.pop(result); }

  DeviceRuntimeState runtimeState(InstanceId instanceId) const;
  const void* specializedState(InstanceId instanceId) const;

 private:
  DeviceDriver* driverFor(DriverId driverId) const;
  struct ActiveSlot {
    InstanceId instanceId = kInvalidInstanceId;
    uint8_t owners = 0;
    bool retained = false;
    uint32_t lastUsed = 0;
  };

  ActiveSlot* slotFor(InstanceId instanceId);
  const ActiveSlot* slotFor(InstanceId instanceId) const;
  bool addActive(InstanceId instanceId, ConnectionOwner owner);
  void removeActive(InstanceId instanceId);
  bool evictOldestIdle();
  void deactivate(InstanceId instanceId);
  void touch(ActiveSlot& slot);
  static uint8_t ownerBit(ConnectionOwner owner);
  void applySkipPeers(DeviceDriver& driver, const DeviceRecord& record);
  bool seedInitialRegistry();
  bool save();
  CommandStatus dispatch(const DeviceCommand& command);

  ILegacySharkBackend& legacyBackend_;
  DeviceDriver* drivers_[kMaxCompiledDrivers] = {};
  size_t driverCount_ = 0;
  ConfigStore store_;
  DeviceRegistry registry_;
  DeviceCommandQueue commands_;
  DeviceResultQueue results_;
  ActiveSlot activeSlots_[kMaxActiveInstances] = {};
  size_t activeCount_ = 0;
  uint32_t useCounter_ = 0;
  uint32_t nextRequestId_ = 1;
  bool begun_ = false;
};

DeviceManager& devices();

}  // namespace studio
