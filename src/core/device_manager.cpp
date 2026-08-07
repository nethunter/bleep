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

uint8_t DeviceManager::ownerBit(ConnectionOwner owner) {
  return static_cast<uint8_t>(owner);
}

DeviceManager::ActiveSlot* DeviceManager::slotFor(InstanceId instanceId) {
  for (size_t i = 0; i < kMaxActiveInstances; ++i) {
    if (activeSlots_[i].instanceId == instanceId) {
      return &activeSlots_[i];
    }
  }
  return nullptr;
}

const DeviceManager::ActiveSlot* DeviceManager::slotFor(
    InstanceId instanceId) const {
  for (size_t i = 0; i < kMaxActiveInstances; ++i) {
    if (activeSlots_[i].instanceId == instanceId) {
      return &activeSlots_[i];
    }
  }
  return nullptr;
}

InstanceId DeviceManager::foregroundInstance() const {
  for (size_t i = 0; i < kMaxActiveInstances; ++i) {
    if ((activeSlots_[i].owners & ownerBit(ConnectionOwner::Foreground)) != 0) {
      return activeSlots_[i].instanceId;
    }
  }
  return kInvalidInstanceId;
}

bool DeviceManager::isActive(InstanceId instanceId) const {
  return instanceId != kInvalidInstanceId && slotFor(instanceId) != nullptr;
}

bool DeviceManager::ownedBy(InstanceId instanceId, ConnectionOwner owner) const {
  const ActiveSlot* slot = slotFor(instanceId);
  return slot != nullptr && (slot->owners & ownerBit(owner)) != 0;
}

bool DeviceManager::isRetained(InstanceId instanceId) const {
  const ActiveSlot* slot = slotFor(instanceId);
  return slot != nullptr && slot->retained;
}

BleSlotKey DeviceManager::bleSlotKey(InstanceId instanceId) const {
  const DeviceRecord* record = find(instanceId);
  if (record == nullptr) return {};
  DeviceDriver* driver = driverFor(record->driverId);
  return driver != nullptr ? driver->bleSlotKey(*record) : BleSlotKey{};
}

size_t DeviceManager::bleSlotCount() const {
  BleSlotKey keys[CONFIG_MAX_ACTIVE_LINKS] = {};
  size_t count = 0;
  for (size_t i = 0; i < kMaxActiveInstances; ++i) {
    if (activeSlots_[i].instanceId == kInvalidInstanceId) continue;
    const BleSlotKey key = bleSlotKey(activeSlots_[i].instanceId);
    if (!key.valid()) continue;
    bool known = false;
    for (size_t j = 0; j < count; ++j) known = known || keys[j] == key;
    if (!known && count < CONFIG_MAX_ACTIVE_LINKS) keys[count++] = key;
  }
  return count;
}

void DeviceManager::touch(ActiveSlot& slot) {
  ++useCounter_;
  if (useCounter_ == 0) {
    useCounter_ = 1;
  }
  slot.lastUsed = useCounter_;
}

bool DeviceManager::addActive(InstanceId instanceId, ConnectionOwner owner) {
  ActiveSlot* existing = slotFor(instanceId);
  if (existing != nullptr) {
    existing->owners |= ownerBit(owner);
    touch(*existing);
    return true;
  }
  for (size_t i = 0; i < kMaxActiveInstances; ++i) {
    if (activeSlots_[i].instanceId != kInvalidInstanceId) {
      continue;
    }
    activeSlots_[i].instanceId = instanceId;
    activeSlots_[i].owners = ownerBit(owner);
    activeSlots_[i].retained = false;
    touch(activeSlots_[i]);
    ++activeCount_;
    return true;
  }
  return false;
}

void DeviceManager::removeActive(InstanceId instanceId) {
  ActiveSlot* slot = slotFor(instanceId);
  if (slot != nullptr) {
    *slot = ActiveSlot{};
    --activeCount_;
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
    driver.preferSkipPeer(record.instanceId, other->bleAddress);
  }
}

void DeviceManager::loop() {
  if (!begun_) {
    return;
  }

  for (size_t i = 0; i < driverCount_; ++i) {
    if (drivers_[i] != nullptr) {
      drivers_[i]->loop();
    }
  }

  for (size_t i = 0; i < kMaxActiveInstances; ++i) {
    ActiveSlot& slot = activeSlots_[i];
    DeviceRecord* activeRecord = mutableRecord(slot.instanceId);
    if (activeRecord == nullptr) {
      continue;
    }
    DeviceDriver* activeDriver = driverFor(activeRecord->driverId);
    if (activeDriver == nullptr) {
      continue;
    }
    const DeviceRuntimeState runtime =
        activeDriver->runtimeState(activeRecord->instanceId);
    if (runtime.protocolReady) {
      slot.retained = true;
    }
    const bool pairingChanged = activeDriver->consumePairingUpdate(
        activeRecord->instanceId, *activeRecord);
    if (pairingChanged && !isPendingAdd(activeRecord->instanceId)) {
      save();
    }
    if (isPendingAdd(activeRecord->instanceId) && activeRecord->paired &&
        runtime.protocolReady && !pendingCommitFailed_) {
      commitPendingAdd();
    }
    if (slot.owners == 0 && slot.retained &&
        runtime.link == LinkState::Disconnected &&
        !runtime.commandPending &&
        !(runtime.recordingConfirmed && runtime.recording) &&
        !activeDriver->retainWhileDisconnected(activeRecord->instanceId)) {
      deactivate(activeRecord->instanceId);
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
  if (pendingAdd() != kInvalidInstanceId) {
    outId = kInvalidInstanceId;
    return RegistryStatus::Invalid;
  }
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

RegistryStatus DeviceManager::beginAdd(DriverId driverId,
                                       const char* displayName,
                                       InstanceId& outId) {
  outId = kInvalidInstanceId;
  if (pendingAdd() != kInvalidInstanceId || displayName == nullptr ||
      displayName[0] == '\0') {
    return RegistryStatus::Invalid;
  }
  const DriverDescriptor* descriptor = DriverCatalog::find(driverId);
  if (descriptor == nullptr || !descriptor->discoverable ||
      driverId == DriverId::HomeAssistant) {
    return RegistryStatus::Invalid;
  }
  if (registry_.count() >= registry_.capacity()) {
    return RegistryStatus::Full;
  }
  if (registry_.countByDriver(driverId) >= descriptor->maxInstances) {
    return RegistryStatus::DuplicateDriver;
  }
  pendingRecord_ = DeviceRecord{};
  pendingRecord_.instanceId = registry_.nextInstanceId();
  pendingRecord_.driverId = driverId;
  pendingRecord_.enabled = true;
  std::strncpy(pendingRecord_.displayName, displayName,
               sizeof(pendingRecord_.displayName) - 1);
  outId = pendingRecord_.instanceId;
  pendingCommitFailed_ = false;
  return RegistryStatus::Ok;
}

bool DeviceManager::retryPendingAdd(InstanceId instanceId) {
  if (!isPendingAdd(instanceId) || !pendingCommitFailed_) {
    return false;
  }
  pendingCommitFailed_ = false;
  return commitPendingAdd();
}

RegistryStatus DeviceManager::cancelPendingAdd(InstanceId instanceId) {
  if (!isPendingAdd(instanceId)) {
    return RegistryStatus::NotFound;
  }
  DeviceDriver* driver = driverFor(pendingRecord_.driverId);
  if (driver != nullptr) {
    driver->cancelOnboarding(pendingRecord_);
  }
  if (isActive(instanceId)) {
    deactivate(instanceId);
  }
  pendingRecord_ = DeviceRecord{};
  pendingCommitFailed_ = false;
  return RegistryStatus::Ok;
}

bool DeviceManager::commitPendingAdd() {
  if (pendingAdd() == kInvalidInstanceId || !pendingRecord_.paired ||
      !runtimeState(pendingRecord_.instanceId).protocolReady) {
    return false;
  }
  const DriverDescriptor* descriptor =
      DriverCatalog::find(pendingRecord_.driverId);
  if (descriptor == nullptr) {
    return false;
  }
  const DeviceRegistry previous = registry_;
  if (registry_.commitPrepared(pendingRecord_, descriptor->maxInstances) !=
      RegistryStatus::Ok) {
    return false;
  }
  if (!save()) {
    registry_ = previous;
    pendingCommitFailed_ = true;
    return false;
  }
  pendingRecord_ = DeviceRecord{};
  pendingCommitFailed_ = false;
  return true;
}

RegistryStatus DeviceManager::remove(InstanceId instanceId) {
  DeviceRecord* record = registry_.find(instanceId);
  if (record == nullptr) return RegistryStatus::NotFound;
  const DeviceRecord removed = *record;
  const DeviceRegistry previous = registry_;
  const RegistryStatus status = registry_.remove(instanceId);
  if (status != RegistryStatus::Ok) return status;
  if (!save()) {
    registry_ = previous;
    return RegistryStatus::Invalid;
  }
  DeviceDriver* driver = driverFor(removed.driverId);
  if (isActive(instanceId)) {
    if (driver != nullptr) driver->deactivate(instanceId);
    removeActive(instanceId);
  }
  if (driver != nullptr) {
    // Forget transport identity only after the updated registry is durable.
    driver->forgetPairing(removed);
  }
  return RegistryStatus::Ok;
}

RegistryStatus DeviceManager::rename(InstanceId instanceId, const char* displayName) {
  const DeviceRecord* record = registry_.find(instanceId);
  return record == nullptr ? RegistryStatus::NotFound
                           : update(instanceId, displayName, record->enabled);
}

RegistryStatus DeviceManager::update(InstanceId instanceId,
                                     const char* displayName, bool enabled) {
  char safeName[kDeviceNameCapacity] = "";
  if (displayName != nullptr) {
    std::strncpy(safeName, displayName, sizeof(safeName) - 1);
  }
  const DeviceRegistry previous = registry_;
  RegistryStatus status = registry_.rename(instanceId, safeName);
  if (status != RegistryStatus::Ok) return status;
  status = registry_.setEnabled(instanceId, enabled);
  if (status != RegistryStatus::Ok || !save()) {
    registry_ = previous;
    return RegistryStatus::Invalid;
  }
  if (!enabled && isActive(instanceId)) deactivate(instanceId);
  return RegistryStatus::Ok;
}

RegistryStatus DeviceManager::setEnabled(InstanceId instanceId, bool enabled) {
  const DeviceRecord* record = registry_.find(instanceId);
  return record == nullptr ? RegistryStatus::NotFound
                           : update(instanceId, record->displayName, enabled);
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

RegistryStatus DeviceManager::addHomeAssistantEntity(
    HomeAssistantDomain domain, const char* entityId, const char* displayName,
    InstanceId& outId) {
  const RegistryStatus added =
      add(DriverId::HomeAssistant, displayName, outId);
  if (added != RegistryStatus::Ok) {
    return added;
  }
  const RegistryStatus configured =
      registry_.configureHomeAssistant(outId, domain, entityId);
  if (configured != RegistryStatus::Ok || !save()) {
    registry_.remove(outId);
    outId = kInvalidInstanceId;
    save();
    return configured == RegistryStatus::Ok ? RegistryStatus::Invalid
                                             : configured;
  }
  return RegistryStatus::Ok;
}

RegistryStatus DeviceManager::rebindHomeAssistantEntity(
    InstanceId instanceId, HomeAssistantDomain domain, const char* entityId) {
  if (isActive(instanceId)) {
    deactivate(instanceId);
  }
  const RegistryStatus status =
      registry_.configureHomeAssistant(instanceId, domain, entityId);
  return status == RegistryStatus::Ok && !save() ? RegistryStatus::Invalid
                                                  : status;
}

RegistryStatus DeviceManager::replaceHomeAssistantEntities(
    const HomeAssistantEntitySelection* selections, size_t count) {
  if ((count > 0 && selections == nullptr) || count > 4) {
    return RegistryStatus::Invalid;
  }
  for (size_t i = 0; i < count; ++i) {
    if (selections[i].domain == HomeAssistantDomain::None ||
        selections[i].entityId[0] == '\0' || selections[i].displayName[0] == '\0') {
      return RegistryStatus::Invalid;
    }
    for (size_t j = i + 1; j < count; ++j) {
      if (std::strncmp(selections[i].entityId, selections[j].entityId,
                       kHomeAssistantEntityIdCapacity) == 0) {
        return RegistryStatus::DuplicateDriver;
      }
    }
  }

  DeviceRegistry previous = registry_;
  for (size_t i = 0; i < registry_.count(); ++i) {
    const DeviceRecord* record = registry_.at(i);
    if (record != nullptr && record->driverId == DriverId::HomeAssistant &&
        isActive(record->instanceId)) {
      deactivate(record->instanceId);
    }
  }
  for (size_t i = registry_.count(); i > 0; --i) {
    const DeviceRecord* record = registry_.at(i - 1);
    if (record == nullptr || record->driverId != DriverId::HomeAssistant) continue;
    bool keep = false;
    for (size_t s = 0; s < count; ++s) {
      keep = keep || selections[s].instanceId == record->instanceId;
    }
    if (!keep) registry_.remove(record->instanceId);
  }
  for (size_t s = 0; s < count; ++s) {
    InstanceId id = selections[s].instanceId;
    DeviceRecord* existing = registry_.find(id);
    if (existing == nullptr) {
      const DriverDescriptor* descriptor = DriverCatalog::find(DriverId::HomeAssistant);
      if (descriptor == nullptr ||
          registry_.add(DriverId::HomeAssistant, selections[s].displayName,
                        descriptor->maxInstances, id) != RegistryStatus::Ok) {
        registry_ = previous;
        return RegistryStatus::Full;
      }
    } else if (existing->driverId != DriverId::HomeAssistant) {
      registry_ = previous;
      return RegistryStatus::Invalid;
    } else {
      registry_.rename(id, selections[s].displayName);
    }
    const RegistryStatus configured = registry_.configureHomeAssistant(
        id, selections[s].domain, selections[s].entityId);
    if (configured != RegistryStatus::Ok) {
      registry_ = previous;
      return configured;
    }
  }
  if (!save()) {
    registry_ = previous;
    return RegistryStatus::Invalid;
  }
  return RegistryStatus::Ok;
}

bool DeviceManager::acquire(InstanceId instanceId, ConnectionOwner owner) {
  DeviceRecord* record = mutableRecord(instanceId);
  if (record == nullptr || !record->enabled) {
    return false;
  }
  DeviceDriver* driver = driverFor(record->driverId);
  if (driver == nullptr) {
    return false;
  }
  if (owner == ConnectionOwner::Foreground) {
    const InstanceId current = foregroundInstance();
    if (current != kInvalidInstanceId && current != instanceId) {
      release(current, ConnectionOwner::Foreground);
    }
  }
  if (isActive(instanceId)) {
    // Retained sessions may need to resume device-specific work when a new
    // owner arrives (for example, waking a Canon camera that this session
    // previously powered off).
    if (!driver->resume(*record)) {
      return false;
    }
    return addActive(instanceId, owner);
  }
  if (!ensureBleSlotAvailable(*record, *driver)) {
    return false;
  }
  if (activeCount_ >= kMaxActiveInstances && !evictOldestIdleInstance()) {
    return false;
  }
  if (!driver->activate(*record)) {
    return false;
  }
  if (!addActive(instanceId, owner)) {
    driver->deactivate(instanceId);
    return false;
  }
  applySkipPeers(*driver, *record);
  return true;
}

void DeviceManager::release(InstanceId instanceId, ConnectionOwner owner) {
  ActiveSlot* slot = slotFor(instanceId);
  if (slot == nullptr) {
    return;
  }
  if (runtimeState(instanceId).protocolReady) {
    slot->retained = true;
  }
  slot->owners &= ~ownerBit(owner);
  touch(*slot);
  if (slot->owners == 0 && !slot->retained) {
    deactivate(instanceId);
  }
}

bool DeviceManager::ensureBleSlotAvailable(const DeviceRecord& record,
                                           const DeviceDriver& driver) {
  const BleSlotKey requested = driver.bleSlotKey(record);
  if (!requested.valid()) return true;
  for (size_t i = 0; i < kMaxActiveInstances; ++i) {
    if (activeSlots_[i].instanceId != kInvalidInstanceId &&
        bleSlotKey(activeSlots_[i].instanceId) == requested) {
      return true;
    }
  }
  return bleSlotCount() < CONFIG_MAX_ACTIVE_LINKS || evictOldestIdleBleGroup();
}

bool DeviceManager::evictOldestIdleInstance() {
  ActiveSlot* oldest = nullptr;
  for (size_t i = 0; i < kMaxActiveInstances; ++i) {
    ActiveSlot& slot = activeSlots_[i];
    if (slot.instanceId == kInvalidInstanceId || slot.owners != 0) continue;
    const DeviceRuntimeState runtime = runtimeState(slot.instanceId);
    if (runtime.commandPending ||
        (runtime.recordingConfirmed && runtime.recording)) {
      continue;
    }
    if (oldest == nullptr || slot.lastUsed < oldest->lastUsed) oldest = &slot;
  }
  if (oldest == nullptr) return false;
  deactivate(oldest->instanceId);
  return true;
}

bool DeviceManager::evictOldestIdleBleGroup() {
  BleSlotKey oldest;
  uint32_t oldestUse = 0;
  for (size_t i = 0; i < kMaxActiveInstances; ++i) {
    const ActiveSlot& candidate = activeSlots_[i];
    if (candidate.instanceId == kInvalidInstanceId) continue;
    const BleSlotKey key = bleSlotKey(candidate.instanceId);
    if (!key.valid()) continue;
    bool safe = true;
    uint32_t groupUse = 0;
    for (size_t j = 0; j < kMaxActiveInstances; ++j) {
      const ActiveSlot& member = activeSlots_[j];
      if (member.instanceId == kInvalidInstanceId ||
          bleSlotKey(member.instanceId) != key) {
        continue;
      }
      const DeviceRuntimeState runtime = runtimeState(member.instanceId);
      if (member.owners != 0 || runtime.commandPending ||
          (runtime.recordingConfirmed && runtime.recording)) {
        safe = false;
        break;
      }
      if (member.lastUsed > groupUse) groupUse = member.lastUsed;
    }
    if (safe && (!oldest.valid() || groupUse < oldestUse)) {
      oldest = key;
      oldestUse = groupUse;
    }
  }
  if (!oldest.valid()) return false;
  InstanceId members[kMaxActiveInstances] = {};
  size_t memberCount = 0;
  for (size_t i = 0; i < kMaxActiveInstances; ++i) {
    if (activeSlots_[i].instanceId != kInvalidInstanceId &&
        bleSlotKey(activeSlots_[i].instanceId) == oldest) {
      members[memberCount++] = activeSlots_[i].instanceId;
    }
  }
  for (size_t i = 0; i < memberCount; ++i) deactivate(members[i]);
  return true;
}

CommandStatus DeviceManager::disconnect(InstanceId instanceId, bool confirmed) {
  ActiveSlot* slot = slotFor(instanceId);
  if (slot == nullptr) {
    return CommandStatus::Unavailable;
  }
  const DeviceRuntimeState runtime = runtimeState(instanceId);
  if (ownedBy(instanceId, ConnectionOwner::Sequence) || runtime.commandPending) {
    return CommandStatus::Busy;
  }
  if (runtime.recordingConfirmed && runtime.recording && !confirmed) {
    return CommandStatus::ConfirmationRequired;
  }
  deactivate(instanceId);
  return CommandStatus::Succeeded;
}

void DeviceManager::deactivate(InstanceId instanceId) {
  if (!isActive(instanceId)) {
    return;
  }
  DeviceRecord* record = mutableRecord(instanceId);
  if (record != nullptr) {
    DeviceDriver* driver = driverFor(record->driverId);
    if (driver != nullptr) {
      if (driver->consumePairingUpdate(instanceId, *record) &&
          !isPendingAdd(instanceId)) {
        save();
      }
      driver->deactivate(instanceId);
    }
  }
  removeActive(instanceId);
}

void DeviceManager::deactivateAll() {
  for (size_t i = 0; i < kMaxActiveInstances; ++i) {
    if (activeSlots_[i].instanceId != kInvalidInstanceId) {
      deactivate(activeSlots_[i].instanceId);
    }
  }
}

bool DeviceManager::enqueue(DeviceCommand command) {
  if (command.requestId == 0) {
    command.requestId = nextRequestId_++;
  }
  return commands_.push(command);
}

DeviceRuntimeState DeviceManager::runtimeState(InstanceId instanceId) const {
  const DeviceRecord* record = find(instanceId);
  if (record == nullptr || !isActive(instanceId)) {
    return DeviceRuntimeState{};
  }
  DeviceDriver* driver = driverFor(record->driverId);
  return driver != nullptr ? driver->runtimeState(instanceId)
                           : DeviceRuntimeState{};
}

InstanceProfile DeviceManager::profile(InstanceId instanceId) const {
  const DeviceRecord* record = find(instanceId);
  if (record == nullptr) {
    return {};
  }
  if (record->driverId == DriverId::HomeAssistant) {
    InstanceProfile profile;
    profile.capabilities = capabilityBit(Capability::Link);
    switch (record->homeAssistantDomain) {
      case HomeAssistantDomain::Light:
        profile.type = DeviceType::Light;
        profile.capabilities |= capabilityBit(Capability::TurnOn) |
                                capabilityBit(Capability::TurnOff);
        break;
      case HomeAssistantDomain::Switch:
        profile.type = DeviceType::Switch;
        profile.capabilities |= capabilityBit(Capability::TurnOn) |
                                capabilityBit(Capability::TurnOff);
        break;
      case HomeAssistantDomain::InputBoolean:
        profile.type = DeviceType::Switch;
        profile.capabilities |= capabilityBit(Capability::TurnOn) |
                                capabilityBit(Capability::TurnOff);
        break;
      case HomeAssistantDomain::Button:
        profile.type = DeviceType::Action;
        profile.capabilities |= capabilityBit(Capability::Press);
        break;
      case HomeAssistantDomain::Scene:
      case HomeAssistantDomain::Script:
        profile.type = DeviceType::Action;
        profile.capabilities |= capabilityBit(Capability::Activate);
        break;
      case HomeAssistantDomain::None:
        break;
    }
    return profile;
  }
  const DriverDescriptor* descriptor = DriverCatalog::find(record->driverId);
  return descriptor != nullptr
             ? InstanceProfile{descriptor->type, descriptor->capabilities}
             : InstanceProfile{};
}

const void* DeviceManager::specializedState(InstanceId instanceId) const {
  const DeviceRecord* record = find(instanceId);
  if (record == nullptr || !isActive(instanceId)) {
    return nullptr;
  }
  DeviceDriver* driver = driverFor(record->driverId);
  return driver != nullptr ? driver->specializedState(instanceId) : nullptr;
}

DeviceDriver* DeviceManager::driverFor(DriverId driverId) const {
  for (size_t i = 0; i < driverCount_; ++i) {
    if (drivers_[i] != nullptr && drivers_[i]->driverId() == driverId) {
      return drivers_[i];
    }
  }
  return nullptr;
}

DeviceRecord* DeviceManager::mutableRecord(InstanceId instanceId) {
  return isPendingAdd(instanceId) ? &pendingRecord_
                                  : registry_.find(instanceId);
}

bool DeviceManager::save() { return store_.save(registry_); }

CommandStatus DeviceManager::dispatch(const DeviceCommand& command) {
  DeviceRecord* record = mutableRecord(command.instanceId);
  if (record == nullptr) {
    return CommandStatus::InvalidInstance;
  }
  if (!record->enabled) {
    return CommandStatus::Disabled;
  }
  if (command.type == CommandType::Disconnect) {
    return disconnect(command.instanceId, command.value0 != 0);
  }
  if (!isActive(command.instanceId)) {
    return CommandStatus::Unavailable;
  }
  DeviceDriver* driver = driverFor(record->driverId);
  ActiveSlot* slot = slotFor(command.instanceId);
  if (slot != nullptr) {
    touch(*slot);
  }
  return driver != nullptr ? driver->dispatch(command) : CommandStatus::Unsupported;
}

}  // namespace studio
