#include "core/device_manager.h"

#include <cstring>

namespace studio {

DeviceManager::DeviceManager(IConfigBackend& backend,
                             DeviceDriver* const* drivers, size_t driverCount)
    : configuration_(backend) {
  driverCount_ =
      driverCount < kMaxCompiledDrivers ? driverCount : kMaxCompiledDrivers;
  for (size_t i = 0; i < driverCount_; ++i) {
    drivers_[i] = drivers[i];
  }
}

bool DeviceManager::begin() {
  const ConfigLoadStatus status = configuration_.load();
  if (status == ConfigLoadStatus::Corrupt) {
    configuration_.resetCorrupt();
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
  return configuration_.initializeEmpty();
}

uint8_t DeviceManager::ownerBit(ConnectionOwner owner) {
  return ActiveInstancePool::ownerBit(owner);
}

DeviceManager::ActiveSlot* DeviceManager::slotFor(InstanceId instanceId) {
  return activePool_.find(instanceId);
}

const DeviceManager::ActiveSlot* DeviceManager::slotFor(
    InstanceId instanceId) const {
  return activePool_.find(instanceId);
}

InstanceId DeviceManager::foregroundInstance() const {
  return activePool_.foregroundInstance();
}

bool DeviceManager::isActive(InstanceId instanceId) const {
  return activePool_.contains(instanceId);
}

bool DeviceManager::ownedBy(InstanceId instanceId, ConnectionOwner owner) const {
  return activePool_.ownedBy(instanceId, owner);
}

bool DeviceManager::isRetained(InstanceId instanceId) const {
  return activePool_.retained(instanceId);
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
    if (activePool_.slots()[i].instanceId == kInvalidInstanceId) continue;
    const BleSlotKey key = bleSlotKey(activePool_.slots()[i].instanceId);
    if (!key.valid()) continue;
    bool known = false;
    for (size_t j = 0; j < count; ++j) known = known || keys[j] == key;
    if (!known && count < CONFIG_MAX_ACTIVE_LINKS) keys[count++] = key;
  }
  return count;
}

void DeviceManager::touch(ActiveSlot& slot) {
  activePool_.touch(slot);
}

bool DeviceManager::addActive(InstanceId instanceId, ConnectionOwner owner) {
  return activePool_.add(instanceId, owner);
}

void DeviceManager::removeActive(InstanceId instanceId) {
  activePool_.remove(instanceId);
}

void DeviceManager::applySkipPeers(DeviceDriver& driver, const DeviceRecord& record) {
  for (size_t i = 0; i < configuration_.registry().count(); ++i) {
    const DeviceRecord* other = configuration_.registry().at(i);
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
    ActiveSlot& slot = activePool_.slots()[i];
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
    if (!runtime.commandPending) slot.pendingRequestId = 0;
    if (runtime.protocolReady) {
      slot.retained = true;
    }
    const bool pairingChanged = activeDriver->consumePairingUpdate(
        activeRecord->instanceId, *activeRecord);
    if (pairingChanged && !isPendingAdd(activeRecord->instanceId)) {
      save();
    }
    if (isPendingAdd(activeRecord->instanceId) && activeRecord->paired &&
        runtime.protocolReady && !configuration_.pendingCommitFailed()) {
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
  if (result.status == CommandStatus::Succeeded) {
    ActiveSlot* slot = slotFor(command.instanceId);
    if (slot != nullptr && runtimeState(command.instanceId).commandPending) {
      slot->pendingRequestId = command.requestId;
    }
  }
  if (!results_.push(result)) {
    CommandResult discarded;
    results_.pop(discarded);
    results_.push(result);
  }
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
  const RegistryStatus status = configuration_.transact(
      [driverId, displayName, descriptor, &outId](DeviceRegistry& registry) {
        return registry.add(driverId, displayName, descriptor->maxInstances,
                            outId);
      });
  if (status != RegistryStatus::Ok) {
    outId = kInvalidInstanceId;
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
  return configuration_.prepare(driverId, displayName,
                                descriptor->maxInstances, outId);
}

bool DeviceManager::retryPendingAdd(InstanceId instanceId) {
  if (!isPendingAdd(instanceId) || !configuration_.pendingCommitFailed()) {
    return false;
  }
  configuration_.setPendingCommitFailed(false);
  return commitPendingAdd();
}

size_t DeviceManager::onboardingCandidateCount(InstanceId instanceId) const {
  const DeviceRecord* record = find(instanceId);
  DeviceDriver* driver = record != nullptr ? driverFor(record->driverId) : nullptr;
  return driver != nullptr ? driver->onboardingCandidateCount(instanceId) : 0;
}

bool DeviceManager::onboardingCandidate(
    InstanceId instanceId, size_t index,
    OnboardingCandidate& candidate) const {
  const DeviceRecord* record = find(instanceId);
  DeviceDriver* driver = record != nullptr ? driverFor(record->driverId) : nullptr;
  return driver != nullptr &&
         driver->onboardingCandidate(instanceId, index, candidate);
}

bool DeviceManager::selectOnboardingCandidate(InstanceId instanceId,
                                              uint32_t token) {
  if (!isPendingAdd(instanceId) || token == 0) return false;
  const DeviceRecord* record = find(instanceId);
  DeviceDriver* driver = record != nullptr ? driverFor(record->driverId) : nullptr;
  return driver != nullptr &&
         driver->selectOnboardingCandidate(instanceId, token);
}

RegistryStatus DeviceManager::cancelPendingAdd(InstanceId instanceId) {
  if (!isPendingAdd(instanceId)) {
    return RegistryStatus::NotFound;
  }
  DeviceDriver* driver = driverFor(configuration_.pending().driverId);
  if (driver != nullptr && !driver->cancelOnboarding(configuration_.pending())) {
    return RegistryStatus::Invalid;
  }
  if (isActive(instanceId)) {
    deactivate(instanceId);
  }
  configuration_.cancelPending();
  return RegistryStatus::Ok;
}

bool DeviceManager::commitPendingAdd() {
  if (pendingAdd() == kInvalidInstanceId || !configuration_.pending().paired ||
      !runtimeState(configuration_.pending().instanceId).protocolReady) {
    return false;
  }
  const DriverDescriptor* descriptor =
      DriverCatalog::find(configuration_.pending().driverId);
  if (descriptor == nullptr) {
    return false;
  }
  return configuration_.commitPending(descriptor->maxInstances);
}

RegistryStatus DeviceManager::remove(InstanceId instanceId) {
  DeviceRecord* record = configuration_.registry().find(instanceId);
  if (record == nullptr) return RegistryStatus::NotFound;
  const DeviceRecord removed = *record;
  const RegistryStatus status = configuration_.transact(
      [instanceId](DeviceRegistry& registry) {
        return registry.remove(instanceId);
      });
  if (status != RegistryStatus::Ok) return status;
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
  const DeviceRecord* record = configuration_.registry().find(instanceId);
  return record == nullptr ? RegistryStatus::NotFound
                           : update(instanceId, displayName, record->enabled);
}

RegistryStatus DeviceManager::update(InstanceId instanceId,
                                     const char* displayName, bool enabled) {
  char safeName[kDeviceNameCapacity] = "";
  if (displayName != nullptr) {
    std::strncpy(safeName, displayName, sizeof(safeName) - 1);
  }
  const RegistryStatus status = configuration_.transact(
      [instanceId, &safeName, enabled](DeviceRegistry& registry) {
        const RegistryStatus renamed = registry.rename(instanceId, safeName);
        return renamed == RegistryStatus::Ok
                   ? registry.setEnabled(instanceId, enabled)
                   : renamed;
      });
  if (status != RegistryStatus::Ok) return status;
  if (!enabled && isActive(instanceId)) deactivate(instanceId);
  return RegistryStatus::Ok;
}

RegistryStatus DeviceManager::setEnabled(InstanceId instanceId, bool enabled) {
  const DeviceRecord* record = configuration_.registry().find(instanceId);
  return record == nullptr ? RegistryStatus::NotFound
                           : update(instanceId, record->displayName, enabled);
}

RegistryStatus DeviceManager::clearPairing(InstanceId instanceId) {
  DeviceRecord* record = configuration_.registry().find(instanceId);
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
  const RegistryStatus status = configuration_.registry().clearPairing(instanceId);
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
      configuration_.registry().configureHomeAssistant(outId, domain, entityId);
  if (configured != RegistryStatus::Ok || !save()) {
    configuration_.registry().remove(outId);
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
      configuration_.registry().configureHomeAssistant(instanceId, domain, entityId);
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

  DeviceRegistry previous = configuration_.registry();
  if (!previous.valid()) return RegistryStatus::Full;
  for (size_t i = 0; i < configuration_.registry().count(); ++i) {
    const DeviceRecord* record = configuration_.registry().at(i);
    if (record != nullptr && record->driverId == DriverId::HomeAssistant &&
        isActive(record->instanceId)) {
      deactivate(record->instanceId);
    }
  }
  for (size_t i = configuration_.registry().count(); i > 0; --i) {
    const DeviceRecord* record = configuration_.registry().at(i - 1);
    if (record == nullptr || record->driverId != DriverId::HomeAssistant) continue;
    bool keep = false;
    for (size_t s = 0; s < count; ++s) {
      keep = keep || selections[s].instanceId == record->instanceId;
    }
    if (!keep) configuration_.registry().remove(record->instanceId);
  }
  for (size_t s = 0; s < count; ++s) {
    InstanceId id = selections[s].instanceId;
    DeviceRecord* existing = configuration_.registry().find(id);
    if (existing == nullptr) {
      const DriverDescriptor* descriptor = DriverCatalog::find(DriverId::HomeAssistant);
      if (descriptor == nullptr ||
          configuration_.registry().add(DriverId::HomeAssistant, selections[s].displayName,
                        descriptor->maxInstances, id) != RegistryStatus::Ok) {
        configuration_.registry() = previous;
        return RegistryStatus::Full;
      }
    } else if (existing->driverId != DriverId::HomeAssistant) {
      configuration_.registry() = previous;
      return RegistryStatus::Invalid;
    } else {
      configuration_.registry().rename(id, selections[s].displayName);
    }
    const RegistryStatus configured = configuration_.registry().configureHomeAssistant(
        id, selections[s].domain, selections[s].entityId);
    if (configured != RegistryStatus::Ok) {
      configuration_.registry() = previous;
      return configured;
    }
  }
  if (!save()) {
    configuration_.registry() = previous;
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
  if (activePool_.count() >= kMaxActiveInstances && !evictOldestIdleInstance()) {
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
    if (activePool_.slots()[i].instanceId != kInvalidInstanceId &&
        bleSlotKey(activePool_.slots()[i].instanceId) == requested) {
      return true;
    }
  }
  return bleSlotCount() < CONFIG_MAX_ACTIVE_LINKS || evictOldestIdleBleGroup();
}

bool DeviceManager::evictOldestIdleInstance() {
  ActiveSlot* oldest = nullptr;
  for (size_t i = 0; i < kMaxActiveInstances; ++i) {
    ActiveSlot& slot = activePool_.slots()[i];
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
    const ActiveSlot& candidate = activePool_.slots()[i];
    if (candidate.instanceId == kInvalidInstanceId) continue;
    const BleSlotKey key = bleSlotKey(candidate.instanceId);
    if (!key.valid()) continue;
    bool safe = true;
    uint32_t groupUse = 0;
    for (size_t j = 0; j < kMaxActiveInstances; ++j) {
      const ActiveSlot& member = activePool_.slots()[j];
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
    if (activePool_.slots()[i].instanceId != kInvalidInstanceId &&
        bleSlotKey(activePool_.slots()[i].instanceId) == oldest) {
      members[memberCount++] = activePool_.slots()[i].instanceId;
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

CommandStatus DeviceManager::disconnectIdle(InstanceId instanceId) {
  const ActiveSlot* slot = slotFor(instanceId);
  if (slot == nullptr) {
    return CommandStatus::Unavailable;
  }
  if (slot->owners != 0) {
    return CommandStatus::Busy;
  }
  return disconnect(instanceId);
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
    if (activePool_.slots()[i].instanceId != kInvalidInstanceId) {
      deactivate(activePool_.slots()[i].instanceId);
    }
  }
}

bool DeviceManager::enqueue(DeviceCommand command, uint32_t* assignedRequestId) {
  if (command.requestId == 0) {
    command.requestId = nextRequestId_++;
  }
  if (!commands_.push(command)) return false;
  if (assignedRequestId != nullptr) *assignedRequestId = command.requestId;
  return true;
}

bool DeviceManager::cancelCommand(uint32_t requestId, InstanceId instanceId) {
  if (requestId == 0 || instanceId == kInvalidInstanceId) return false;
  const bool queued = commands_.removeFirst(
      [requestId](const DeviceCommand& command) {
        return command.requestId == requestId;
      });
  results_.removeFirst([requestId](const CommandResult& result) {
    return result.requestId == requestId;
  });
  ActiveSlot* slot = slotFor(instanceId);
  const bool dispatched = slot != nullptr && slot->pendingRequestId == requestId;
  const DeviceRecord* record = find(instanceId);
  DeviceDriver* driver = record != nullptr ? driverFor(record->driverId) : nullptr;
  if (driver != nullptr && dispatched) {
    driver->cancelPendingCommand(instanceId);
    slot->pendingRequestId = 0;
  }
  return queued || dispatched;
}

bool DeviceManager::takeResult(uint32_t requestId, CommandResult& result) {
  if (requestId == 0) return false;
  return results_.takeFirst(
      [requestId](const CommandResult& candidate) {
        return candidate.requestId == requestId;
      },
      result);
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

bool DeviceManager::lightControlState(InstanceId instanceId,
                                      LightControlState& state) const {
  state = LightControlState{};
  const DeviceRecord* record = find(instanceId);
  if (record == nullptr || !isActive(instanceId)) return false;
  DeviceDriver* driver = driverFor(record->driverId);
  return driver != nullptr && driver->lightControlState(instanceId, state);
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
  if (descriptor == nullptr) return {};
  const InstanceProfile catalogProfile{descriptor->type,
                                       descriptor->capabilities};
  DeviceDriver* driver = driverFor(record->driverId);
  return driver != nullptr ? driver->instanceProfile(*record, catalogProfile)
                           : catalogProfile;
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
  return isPendingAdd(instanceId) ? &configuration_.pending()
                                  : configuration_.registry().find(instanceId);
}

bool DeviceManager::save() { return configuration_.save(); }

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
