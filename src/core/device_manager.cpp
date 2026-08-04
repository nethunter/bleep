#include "core/device_manager.h"

#include <cstring>

namespace studio {

DeviceManager::DeviceManager(IConfigBackend& backend, ILegacySharkBackend& legacyBackend,
                             DeviceDriver* const* drivers, size_t driverCount)
    : legacyBackend_(legacyBackend), store_(backend) {
  driverCount_ =
      driverCount < kMaxCompiledDrivers ? driverCount : kMaxCompiledDrivers;
  for (size_t i = 0; i < driverCount_; ++i) {
    drivers_[i] = drivers[i];
  }
}

bool DeviceManager::begin() {
  const ConfigLoadStatus status = store_.load(registry_);
  if (status == ConfigLoadStatus::Corrupt) {
    registry_.clear(true);
    begun_ = true;
    return false;
  }
  if (status == ConfigLoadStatus::Missing && !seedInitialRegistry()) {
    begun_ = true;
    return false;
  }
  begun_ = true;
  return true;
}

bool DeviceManager::seedInitialRegistry() {
  registry_.clear(false);
#if CONFIG_DRIVER_SHARK_NANO_II
  const DriverDescriptor* descriptor = DriverCatalog::find(DriverId::SharkNanoII);
  if (descriptor != nullptr) {
    InstanceId instanceId = kInvalidInstanceId;
    if (registry_.add(descriptor->id, descriptor->model, descriptor->maxInstances,
                      instanceId) != RegistryStatus::Ok) {
      return false;
    }

    LegacySharkConfig legacy;
    if (legacyBackend_.readLegacyShark(legacy) && legacy.paired) {
      registry_.updatePairing(instanceId, legacy.address, legacy.addressType,
                              legacy.advertisedName);
    }
  }
#endif
  return save();
}

void DeviceManager::loop() {
  if (!begun_) {
    return;
  }

  DeviceDriver* activeDriver = nullptr;
  DeviceRecord* activeRecord = registry_.find(activeInstance_);
  if (activeRecord != nullptr) {
    activeDriver = driverFor(activeRecord->driverId);
  }
  if (activeDriver != nullptr) {
    activeDriver->loop();
    if (activeDriver->consumePairingUpdate(*activeRecord)) {
      save();
    }
  }

  DeviceCommand command;
  if (!commands_.pop(command)) {
    return;
  }
  CommandResult result;
  result.requestId = command.requestId;
  result.instanceId = command.instanceId;
  result.status = dispatch(command);
  results_.push(result);
}

RegistryStatus DeviceManager::add(DriverId driverId, const char* displayName,
                                  InstanceId& outId) {
  const DriverDescriptor* descriptor = DriverCatalog::find(driverId);
  if (descriptor == nullptr) {
    outId = kInvalidInstanceId;
    return RegistryStatus::Invalid;
  }
  const RegistryStatus status =
      registry_.add(driverId, displayName, descriptor->maxInstances, outId);
  if (status == RegistryStatus::Ok && !save()) {
    registry_.remove(outId);
    outId = kInvalidInstanceId;
    return RegistryStatus::Invalid;
  }
  return status;
}

RegistryStatus DeviceManager::remove(InstanceId instanceId) {
  DeviceRecord* record = registry_.find(instanceId);
  if (record != nullptr) {
    DeviceDriver* driver = driverFor(record->driverId);
    if (driver != nullptr) {
      // Drop controller-side bonds before the registry forgets the address.
      driver->forgetPairing(*record);
    }
  }
  if (instanceId == activeInstance_) {
    deactivate();
  }
  const RegistryStatus status = registry_.remove(instanceId);
  if (status == RegistryStatus::Ok) {
    save();
  }
  return status;
}

RegistryStatus DeviceManager::rename(InstanceId instanceId, const char* displayName) {
  const RegistryStatus status = registry_.rename(instanceId, displayName);
  if (status == RegistryStatus::Ok) {
    save();
  }
  return status;
}

RegistryStatus DeviceManager::setEnabled(InstanceId instanceId, bool enabled) {
  if (!enabled && instanceId == activeInstance_) {
    deactivate();
  }
  const RegistryStatus status = registry_.setEnabled(instanceId, enabled);
  if (status == RegistryStatus::Ok) {
    save();
  }
  return status;
}

RegistryStatus DeviceManager::clearPairing(InstanceId instanceId) {
  DeviceRecord* record = registry_.find(instanceId);
  if (record == nullptr) {
    return RegistryStatus::NotFound;
  }
  DeviceDriver* driver = driverFor(record->driverId);
  if (driver != nullptr) {
    driver->forgetPairing(*record);
  }
  if (instanceId == activeInstance_) {
    deactivate();
  }
  const RegistryStatus status = registry_.clearPairing(instanceId);
  if (status == RegistryStatus::Ok) {
    save();
  }
  return status;
}

bool DeviceManager::activate(InstanceId instanceId) {
  DeviceRecord* record = registry_.find(instanceId);
  if (record == nullptr || !record->enabled) {
    return false;
  }
  DeviceDriver* driver = driverFor(record->driverId);
  if (driver == nullptr) {
    return false;
  }
  if (activeInstance_ != kInvalidInstanceId) {
    deactivate();
  }
  activeInstance_ = instanceId;
  driver->activate(*record);
  // Never latch onto a body already claimed by another record of this driver.
  for (size_t i = 0; i < registry_.count(); ++i) {
    const DeviceRecord* other = registry_.at(i);
    if (other == nullptr || other->instanceId == instanceId ||
        other->driverId != record->driverId || !other->paired ||
        other->bleAddress[0] == '\0') {
      continue;
    }
    driver->preferSkipPeer(other->bleAddress);
  }
  return true;
}

void DeviceManager::deactivate() {
  DeviceRecord* record = registry_.find(activeInstance_);
  if (record != nullptr) {
    DeviceDriver* driver = driverFor(record->driverId);
    if (driver != nullptr) {
      driver->deactivate();
      if (driver->consumePairingUpdate(*record)) {
        save();
      }
    }
  }
  activeInstance_ = kInvalidInstanceId;
}

bool DeviceManager::enqueue(DeviceCommand command) {
  if (command.requestId == 0) {
    command.requestId = nextRequestId_++;
  }
  return commands_.push(command);
}

DeviceRuntimeState DeviceManager::runtimeState(InstanceId instanceId) const {
  const DeviceRecord* record = registry_.find(instanceId);
  if (record == nullptr || instanceId != activeInstance_) {
    return DeviceRuntimeState{};
  }
  DeviceDriver* driver = driverFor(record->driverId);
  return driver != nullptr ? driver->runtimeState() : DeviceRuntimeState{};
}

const void* DeviceManager::specializedState(InstanceId instanceId) const {
  const DeviceRecord* record = registry_.find(instanceId);
  if (record == nullptr || instanceId != activeInstance_) {
    return nullptr;
  }
  DeviceDriver* driver = driverFor(record->driverId);
  return driver != nullptr ? driver->specializedState() : nullptr;
}

DeviceDriver* DeviceManager::driverFor(DriverId driverId) const {
  for (size_t i = 0; i < driverCount_; ++i) {
    if (drivers_[i] != nullptr && drivers_[i]->driverId() == driverId) {
      return drivers_[i];
    }
  }
  return nullptr;
}

bool DeviceManager::save() { return store_.save(registry_); }

CommandStatus DeviceManager::dispatch(const DeviceCommand& command) {
  DeviceRecord* record = registry_.find(command.instanceId);
  if (record == nullptr) {
    return CommandStatus::InvalidInstance;
  }
  if (!record->enabled) {
    return CommandStatus::Disabled;
  }
  if (command.type == CommandType::Disconnect) {
    if (command.instanceId == activeInstance_) {
      deactivate();
      return CommandStatus::Succeeded;
    }
    return CommandStatus::Unavailable;
  }
  if (command.instanceId != activeInstance_) {
    return CommandStatus::Unavailable;
  }
  DeviceDriver* driver = driverFor(record->driverId);
  return driver != nullptr ? driver->dispatch(command) : CommandStatus::Unsupported;
}

}  // namespace studio

