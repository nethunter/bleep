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

InstanceId DeviceManager::activeInstance() const {
  return activeCount_ > 0 ? activeInstances_[0] : kInvalidInstanceId;
}

bool DeviceManager::isActive(InstanceId instanceId) const {
  if (instanceId == kInvalidInstanceId) {
    return false;
  }
  for (size_t i = 0; i < activeCount_; ++i) {
    if (activeInstances_[i] == instanceId) {
      return true;
    }
  }
  return false;
}

InstanceId DeviceManager::activeForDriver(DriverId driverId) const {
  for (size_t i = 0; i < activeCount_; ++i) {
    const DeviceRecord* record = registry_.find(activeInstances_[i]);
    if (record != nullptr && record->driverId == driverId) {
      return record->instanceId;
    }
  }
  return kInvalidInstanceId;
}

bool DeviceManager::addActive(InstanceId instanceId) {
  if (isActive(instanceId)) {
    return true;
  }
  if (activeCount_ >= kMaxActiveLinks) {
    return false;
  }
  activeInstances_[activeCount_++] = instanceId;
  return true;
}

void DeviceManager::removeActive(InstanceId instanceId) {
  for (size_t i = 0; i < activeCount_; ++i) {
    if (activeInstances_[i] != instanceId) {
      continue;
    }
    for (size_t j = i + 1; j < activeCount_; ++j) {
      activeInstances_[j - 1] = activeInstances_[j];
    }
    --activeCount_;
    activeInstances_[activeCount_] = kInvalidInstanceId;
    return;
  }
}

void DeviceManager::applySkipPeers(DeviceDriver& driver, const DeviceRecord& record) {
  for (size_t i = 0; i < registry_.count(); ++i) {
    const DeviceRecord* other = registry_.at(i);
    if (other == nullptr || other->instanceId == record.instanceId ||
        other->driverId != record.driverId || !other->paired ||
        other->bleAddress[0] == '\0') {
      continue;
    }
    driver.preferSkipPeer(other->bleAddress);
  }
}

void DeviceManager::loop() {
  if (!begun_) {
    return;
  }

  for (size_t i = 0; i < activeCount_; ++i) {
    DeviceRecord* activeRecord = registry_.find(activeInstances_[i]);
    if (activeRecord == nullptr) {
      continue;
    }
    DeviceDriver* activeDriver = driverFor(activeRecord->driverId);
    if (activeDriver == nullptr) {
      continue;
    }
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
  if (isActive(instanceId)) {
    deactivate(instanceId);
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
  if (!enabled && isActive(instanceId)) {
    deactivate(instanceId);
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
  if (isActive(instanceId)) {
    deactivate(instanceId);
  }
  const RegistryStatus status = registry_.clearPairing(instanceId);
  if (status == RegistryStatus::Ok) {
    save();
  }
  return status;
}

bool DeviceManager::activate(InstanceId instanceId) {
  if (linksHeld_) {
    return false;
  }
  DeviceRecord* record = registry_.find(instanceId);
  if (record == nullptr || !record->enabled) {
    return false;
  }
  DeviceDriver* driver = driverFor(record->driverId);
  if (driver == nullptr) {
    return false;
  }
  deactivateAll();
  activeInstances_[0] = instanceId;
  activeCount_ = 1;
  driver->activate(*record);
  applySkipPeers(*driver, *record);
  return true;
}

bool DeviceManager::activateHeld(InstanceId instanceId) {
  DeviceRecord* record = registry_.find(instanceId);
  if (record == nullptr || !record->enabled) {
    return false;
  }
  DeviceDriver* driver = driverFor(record->driverId);
  if (driver == nullptr) {
    return false;
  }
  if (isActive(instanceId)) {
    return true;
  }
  const InstanceId sameDriver = activeForDriver(record->driverId);
  if (sameDriver != kInvalidInstanceId) {
    deactivate(sameDriver);
  }
  if (activeCount_ >= kMaxActiveLinks) {
    return false;
  }
  if (!addActive(instanceId)) {
    return false;
  }
  driver->activate(*record);
  applySkipPeers(*driver, *record);
  return true;
}

void DeviceManager::deactivate(InstanceId instanceId) {
  if (!isActive(instanceId)) {
    return;
  }
  DeviceRecord* record = registry_.find(instanceId);
  if (record != nullptr) {
    DeviceDriver* driver = driverFor(record->driverId);
    if (driver != nullptr) {
      driver->deactivate();
      if (driver->consumePairingUpdate(*record)) {
        save();
      }
    }
  }
  removeActive(instanceId);
  if (activeCount_ == 0) {
    linksHeld_ = false;
  }
}

void DeviceManager::deactivateAll() {
  while (activeCount_ > 0) {
    deactivate(activeInstances_[0]);
  }
  linksHeld_ = false;
}

void DeviceManager::deactivate() {
  if (activeCount_ == 1) {
    deactivate(activeInstances_[0]);
    return;
  }
  if (!linksHeld_ && activeCount_ > 0) {
    deactivate(activeInstances_[0]);
  }
}

bool DeviceManager::enqueue(DeviceCommand command) {
  if (command.requestId == 0) {
    command.requestId = nextRequestId_++;
  }
  return commands_.push(command);
}

DeviceRuntimeState DeviceManager::runtimeState(InstanceId instanceId) const {
  const DeviceRecord* record = registry_.find(instanceId);
  if (record == nullptr || !isActive(instanceId)) {
    return DeviceRuntimeState{};
  }
  DeviceDriver* driver = driverFor(record->driverId);
  return driver != nullptr ? driver->runtimeState() : DeviceRuntimeState{};
}

const void* DeviceManager::specializedState(InstanceId instanceId) const {
  const DeviceRecord* record = registry_.find(instanceId);
  if (record == nullptr || !isActive(instanceId)) {
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
    if (isActive(command.instanceId)) {
      deactivate(command.instanceId);
      return CommandStatus::Succeeded;
    }
    return CommandStatus::Unavailable;
  }
  if (!isActive(command.instanceId)) {
    return CommandStatus::Unavailable;
  }
  DeviceDriver* driver = driverFor(record->driverId);
  return driver != nullptr ? driver->dispatch(command) : CommandStatus::Unsupported;
}

}  // namespace studio
