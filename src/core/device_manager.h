#pragma once

#include "core/command_queue.h"
#include "core/config_store.h"
#include "core/device_driver.h"
#include "core/driver_catalog.h"

namespace studio {

class DeviceManager {
 public:
  DeviceManager(IConfigBackend& backend, ILegacySharkBackend& legacyBackend,
                DeviceDriver& sharkDriver);

  bool begin();
  void loop();

  size_t count() const { return registry_.count(); }
  const DeviceRecord* at(size_t index) const { return registry_.at(index); }
  const DeviceRecord* find(InstanceId instanceId) const { return registry_.find(instanceId); }
  InstanceId activeInstance() const { return activeInstance_; }

  RegistryStatus add(DriverId driverId, const char* displayName, InstanceId& outId);
  RegistryStatus remove(InstanceId instanceId);
  RegistryStatus rename(InstanceId instanceId, const char* displayName);
  RegistryStatus setEnabled(InstanceId instanceId, bool enabled);
  RegistryStatus clearPairing(InstanceId instanceId);

  bool activate(InstanceId instanceId);
  void deactivate();
  bool enqueue(DeviceCommand command);
  bool popResult(CommandResult& result) { return results_.pop(result); }

  DeviceRuntimeState runtimeState(InstanceId instanceId) const;
  const void* specializedState(InstanceId instanceId) const;

 private:
  DeviceDriver* driverFor(DriverId driverId) const;
  bool seedInitialRegistry();
  bool save();
  CommandStatus dispatch(const DeviceCommand& command);

  ILegacySharkBackend& legacyBackend_;
  DeviceDriver& sharkDriver_;
  ConfigStore store_;
  DeviceRegistry registry_;
  DeviceCommandQueue commands_;
  DeviceResultQueue results_;
  InstanceId activeInstance_ = kInvalidInstanceId;
  uint32_t nextRequestId_ = 1;
  bool begun_ = false;
};

DeviceManager& devices();

}  // namespace studio

