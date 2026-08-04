#pragma once

#include "core/command_queue.h"
#include "core/config_store.h"
#include "core/device_driver.h"
#include "core/driver_catalog.h"

namespace studio {

class DeviceManager {
 public:
  static constexpr size_t kMaxCompiledDrivers = 8;
  static constexpr size_t kMaxActiveLinks = CONFIG_MAX_ACTIVE_LINKS;

  DeviceManager(IConfigBackend& backend, ILegacySharkBackend& legacyBackend,
                DeviceDriver* const* drivers, size_t driverCount);

  bool begin();
  void loop();

  size_t count() const { return registry_.count(); }
  const DeviceRecord* at(size_t index) const { return registry_.at(index); }
  const DeviceRecord* find(InstanceId instanceId) const {
    return registry_.find(instanceId);
  }
  // Primary/exclusive active instance for single-device UI compatibility.
  InstanceId activeInstance() const;
  size_t activeCount() const { return activeCount_; }
  bool isActive(InstanceId instanceId) const;
  bool linksHeld() const { return linksHeld_; }
  void setLinksHeld(bool held) { linksHeld_ = held; }

  RegistryStatus add(DriverId driverId, const char* displayName, InstanceId& outId);
  RegistryStatus remove(InstanceId instanceId);
  RegistryStatus rename(InstanceId instanceId, const char* displayName);
  RegistryStatus setEnabled(InstanceId instanceId, bool enabled);
  RegistryStatus clearPairing(InstanceId instanceId);

  // Exclusive activation for device screens: tears down every other link.
  bool activate(InstanceId instanceId);
  // Concurrent activation for sequence runs: keeps other drivers active.
  bool activateHeld(InstanceId instanceId);
  void deactivate(InstanceId instanceId);
  void deactivateAll();
  // Compatibility: deactivate the primary/exclusive active instance, or all
  // when only one is active.
  void deactivate();
  bool enqueue(DeviceCommand command);
  bool popResult(CommandResult& result) { return results_.pop(result); }

  DeviceRuntimeState runtimeState(InstanceId instanceId) const;
  const void* specializedState(InstanceId instanceId) const;

 private:
  DeviceDriver* driverFor(DriverId driverId) const;
  InstanceId activeForDriver(DriverId driverId) const;
  bool addActive(InstanceId instanceId);
  void removeActive(InstanceId instanceId);
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
  InstanceId activeInstances_[kMaxActiveLinks] = {};
  size_t activeCount_ = 0;
  uint32_t nextRequestId_ = 1;
  bool begun_ = false;
  bool linksHeld_ = false;
};

DeviceManager& devices();

}  // namespace studio
