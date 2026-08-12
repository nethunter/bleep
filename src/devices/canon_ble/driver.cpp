#include "devices/canon_ble/driver.h"

#include <cstring>
#include <new>

#include "devices/canon_camera_name.h"

namespace studio {

CanonBleDriver::Session* CanonBleDriver::sessionFor(InstanceId instanceId) {
  for (Session* session : sessions_) {
    if (session != nullptr && session->instanceId == instanceId) {
      return session;
    }
  }
  return nullptr;
}

const CanonBleDriver::Session* CanonBleDriver::sessionFor(
    InstanceId instanceId) const {
  for (const Session* session : sessions_) {
    if (session != nullptr && session->instanceId == instanceId) {
      return session;
    }
  }
  return nullptr;
}

bool CanonBleDriver::activate(const DeviceRecord& record) {
  if (sessionFor(record.instanceId) != nullptr) {
    return true;
  }
  for (Session*& session : sessions_) {
    if (session != nullptr) continue;
    session = new (std::nothrow) Session;
    if (session == nullptr) return false;
    session->instanceId = record.instanceId;
    char canonical[kDeviceNameCapacity] = "";
    session->metadataRepairPending =
        canon_camera::isGenericDisplayName(record.displayName) &&
        canon_camera::canonicalDisplayName(record.bleName, canonical,
                                           sizeof(canonical));
    const char* identityName =
        record.bleName[0] != '\0'
            ? record.bleName
            : (record.paired ? record.displayName : "");
    if (!session->client.activate(record.bleAddress, record.bleAddressType,
                                  identityName, record.paired)) {
      delete session;
      session = nullptr;
      return false;
    }
    return true;
  }
  return false;
}

bool CanonBleDriver::resume(const DeviceRecord& record) {
  Session* session = sessionFor(record.instanceId);
  if (session == nullptr) {
    return false;
  }
  if (session->client.state().phase ==
      canon_ble::CanonBleState::Phase::PoweredOff) {
    return session->client.powerOn();
  }
  return true;
}

bool CanonBleDriver::retainWhileDisconnected(InstanceId instanceId) const {
  const Session* session = sessionFor(instanceId);
  return session != nullptr && session->client.state().phase ==
                                   canon_ble::CanonBleState::Phase::PoweredOff;
}

void CanonBleDriver::deactivate(InstanceId instanceId) {
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

void CanonBleDriver::loop() {
  for (Session* session : sessions_) {
    if (session != nullptr) session->client.loop();
  }
}

CommandStatus CanonBleDriver::dispatch(const DeviceCommand& command) {
  Session* session = sessionFor(command.instanceId);
  if (session == nullptr) {
    return CommandStatus::Unavailable;
  }
  switch (command.type) {
    case CommandType::Connect:
      session->client.retry();
      return CommandStatus::Succeeded;
    case CommandType::ForgetPairing:
      session->client.forgetDevice();
      return CommandStatus::Succeeded;
    case CommandType::RecordStart:
      return session->client.startRecording() ? CommandStatus::Succeeded
                                              : CommandStatus::Unavailable;
    case CommandType::RecordStop:
      return session->client.stopRecording() ? CommandStatus::Succeeded
                                             : CommandStatus::Unavailable;
    case CommandType::CameraPowerOn:
      return session->client.powerOn() ? CommandStatus::Succeeded
                                       : CommandStatus::Unavailable;
    case CommandType::CameraPowerOff:
      return session->client.powerOff() ? CommandStatus::Succeeded
                                        : CommandStatus::Unavailable;
    default:
      return CommandStatus::Unsupported;
  }
}

DeviceRuntimeState CanonBleDriver::runtimeState(InstanceId instanceId) const {
  DeviceRuntimeState state;
  const Session* session = sessionFor(instanceId);
  if (session == nullptr) {
    return state;
  }
  const canon_ble::CanonBleState& clientState = session->client.state();
  switch (clientState.link) {
    case canon_ble::CanonBleState::Link::Disconnected:
      state.link = LinkState::Disconnected;
      break;
    case canon_ble::CanonBleState::Link::Scanning:
      state.link = LinkState::Scanning;
      break;
    case canon_ble::CanonBleState::Link::Connecting:
      state.link = LinkState::Connecting;
      break;
    case canon_ble::CanonBleState::Link::Connected:
      state.link = LinkState::Connected;
      break;
  }
  state.protocolReady = session->client.protocolReady();
  state.quality = clientState.recordingConfirmed ? StateQuality::Confirmed
                                                 : StateQuality::Unknown;
  state.commandPending = clientState.commandPending;
  state.commandFailed = clientState.lastCommandFailed;
  state.recordingConfirmed = clientState.recordingConfirmed;
  state.recording =
      clientState.recording == canon_ble::CanonBleState::Recording::Recording;
  return state;
}

const void* CanonBleDriver::specializedState(InstanceId instanceId) const {
  const Session* session = sessionFor(instanceId);
  return session != nullptr ? &session->client.state() : nullptr;
}

void CanonBleDriver::forgetPairing(const DeviceRecord& record) {
  Session* session = sessionFor(record.instanceId);
  if (session != nullptr) {
    session->client.forgetBond(record.bleAddress, record.bleAddressType);
    return;
  }
  canon_ble::CanonBleClient transient;
  transient.forgetBond(record.bleAddress, record.bleAddressType);
}

void CanonBleDriver::cancelOnboarding(const DeviceRecord& record) {
  Session* session = sessionFor(record.instanceId);
  if (session != nullptr) {
    session->client.forgetDevice();
  } else {
    forgetPairing(record);
  }
}

void CanonBleDriver::preferSkipPeer(InstanceId instanceId,
                                    const char* bleAddress) {
  Session* session = sessionFor(instanceId);
  if (session != nullptr) {
    session->client.ignorePeerAddress(bleAddress);
  }
}

bool CanonBleDriver::consumePairingUpdate(InstanceId instanceId,
                                           DeviceRecord& record) {
  Session* session = sessionFor(instanceId);
  char address[kBleAddressCapacity] = "";
  char name[kBleNameCapacity] = "";
  uint8_t addressType = 0;
  bool paired = false;
  if (session == nullptr) {
    return false;
  }
  const bool pairingChanged = session->client.consumePairingUpdate(
      address, sizeof(address), addressType, name, sizeof(name), paired);
  if (!pairingChanged && !session->metadataRepairPending) return false;
  if (pairingChanged) {
    record.paired = paired;
    record.bleAddressType = addressType;
    std::strncpy(record.bleAddress, address, sizeof(record.bleAddress) - 1);
    record.bleAddress[sizeof(record.bleAddress) - 1] = '\0';
    std::strncpy(record.bleName, name, sizeof(record.bleName) - 1);
    record.bleName[sizeof(record.bleName) - 1] = '\0';
  }
  session->metadataRepairPending = false;
  if (canon_camera::isGenericDisplayName(record.displayName)) {
    char canonical[kDeviceNameCapacity] = "";
    if (canon_camera::canonicalDisplayName(record.bleName, canonical,
                                           sizeof(canonical))) {
      std::strncpy(record.displayName, canonical,
                   sizeof(record.displayName) - 1);
      record.displayName[sizeof(record.displayName) - 1] = '\0';
    }
  }
  return true;
}

}  // namespace studio
