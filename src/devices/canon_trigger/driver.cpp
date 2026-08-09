#include "devices/canon_trigger/driver.h"

#include <cstring>
#include <new>

namespace studio {

CanonTriggerDriver::Session* CanonTriggerDriver::sessionFor(InstanceId instanceId) {
  for (Session* session : sessions_) {
    if (session != nullptr && session->instanceId == instanceId) {
      return session;
    }
  }
  return nullptr;
}

const CanonTriggerDriver::Session* CanonTriggerDriver::sessionFor(
    InstanceId instanceId) const {
  for (const Session* session : sessions_) {
    if (session != nullptr && session->instanceId == instanceId) {
      return session;
    }
  }
  return nullptr;
}

bool CanonTriggerDriver::activate(const DeviceRecord& record) {
  if (sessionFor(record.instanceId) != nullptr) {
    return true;
  }
  for (Session*& session : sessions_) {
    if (session != nullptr) continue;
    session = new (std::nothrow) Session;
    if (session == nullptr) return false;
    session->instanceId = record.instanceId;
    if (!session->client.activate(
            record.bleAddress, record.bleAddressType,
            record.bleName[0] != '\0' ? record.bleName : record.displayName,
            record.paired)) {
      delete session;
      session = nullptr;
      return false;
    }
    return true;
  }
  return false;
}

void CanonTriggerDriver::deactivate(InstanceId instanceId) {
  Session* session = sessionFor(instanceId);
  if (session != nullptr) {
    session->client.deactivate();
    for (Session*& candidate : sessions_) {
      if (candidate != session) continue;
      delete candidate;
      candidate = nullptr;
      break;
    }
  }
}

void CanonTriggerDriver::loop() {
  for (Session* session : sessions_) {
    if (session != nullptr) session->client.loop();
  }
}

CommandStatus CanonTriggerDriver::dispatch(const DeviceCommand& command) {
  Session* session = sessionFor(command.instanceId);
  if (session == nullptr) {
    return CommandStatus::Unavailable;
  }
  switch (command.type) {
    case CommandType::Connect:
      session->client.startScan();
      return CommandStatus::Succeeded;
    case CommandType::ForgetPairing:
      session->client.forgetDevice();
      return CommandStatus::Succeeded;
    case CommandType::RecordTrigger:
      return session->client.triggerRecord() ? CommandStatus::Succeeded
                                             : CommandStatus::Unavailable;
    default:
      return CommandStatus::Unsupported;
  }
}

DeviceRuntimeState CanonTriggerDriver::runtimeState(InstanceId instanceId) const {
  DeviceRuntimeState state;
  const Session* session = sessionFor(instanceId);
  if (session == nullptr) {
    return state;
  }
  switch (session->client.state().link) {
    case canon_trigger::CanonTriggerState::Link::Disconnected:
      state.link = LinkState::Disconnected;
      break;
    case canon_trigger::CanonTriggerState::Link::Scanning:
      state.link = LinkState::Scanning;
      break;
    case canon_trigger::CanonTriggerState::Link::Connecting:
      state.link = LinkState::Connecting;
      break;
    case canon_trigger::CanonTriggerState::Link::Connected:
      state.link = LinkState::Connected;
      break;
  }
  state.protocolReady = session->client.protocolReady();
  state.commandPending = session->client.state().triggerPending;
  return state;
}

const void* CanonTriggerDriver::specializedState(InstanceId instanceId) const {
  const Session* session = sessionFor(instanceId);
  return session != nullptr ? &session->client.state() : nullptr;
}

void CanonTriggerDriver::forgetPairing(const DeviceRecord& record) {
  Session* session = sessionFor(record.instanceId);
  if (session != nullptr) {
    session->client.forgetBond(record.bleAddress, record.bleAddressType);
    return;
  }
  canon_trigger::CanonTriggerClient transient;
  transient.forgetBond(record.bleAddress, record.bleAddressType);
}

void CanonTriggerDriver::cancelOnboarding(const DeviceRecord& record) {
  Session* session = sessionFor(record.instanceId);
  if (session != nullptr) {
    session->client.forgetDevice();
  } else {
    forgetPairing(record);
  }
}

bool CanonTriggerDriver::consumePairingUpdate(InstanceId instanceId,
                                               DeviceRecord& record) {
  Session* session = sessionFor(instanceId);
  char address[kBleAddressCapacity] = "";
  char name[kBleNameCapacity] = "";
  uint8_t addressType = 0;
  bool paired = false;
  if (session == nullptr ||
      !session->client.consumePairingUpdate(address, sizeof(address), addressType,
                                            name, sizeof(name), paired)) {
    return false;
  }
  record.paired = paired;
  record.bleAddressType = addressType;
  std::strncpy(record.bleAddress, address, sizeof(record.bleAddress) - 1);
  std::strncpy(record.bleName, name, sizeof(record.bleName) - 1);
  return true;
}

}  // namespace studio
