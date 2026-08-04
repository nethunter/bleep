#include "devices/canon_ble/driver.h"

#include <cstring>

namespace studio {

void CanonBleDriver::activate(const DeviceRecord& record) {
  activeInstance_ = record.instanceId;
  active_ = true;
  client_.activate(record.bleAddress, record.bleAddressType,
                   record.bleName[0] != '\0' ? record.bleName
                                             : record.displayName,
                   record.paired);
}

void CanonBleDriver::deactivate() {
  if (active_) {
    client_.deactivate();
  }
  active_ = false;
  activeInstance_ = kInvalidInstanceId;
}

void CanonBleDriver::loop() {
  if (active_) {
    client_.loop();
  }
}

CommandStatus CanonBleDriver::dispatch(const DeviceCommand& command) {
  if (!active_ || command.instanceId != activeInstance_) {
    return CommandStatus::Unavailable;
  }
  switch (command.type) {
    case CommandType::Connect:
      client_.startScan();
      return CommandStatus::Succeeded;
    case CommandType::ForgetPairing:
      client_.forgetDevice();
      return CommandStatus::Succeeded;
    case CommandType::RecordStart:
      return client_.startRecording() ? CommandStatus::Succeeded
                                      : CommandStatus::Unavailable;
    case CommandType::RecordStop:
      return client_.stopRecording() ? CommandStatus::Succeeded
                                     : CommandStatus::Unavailable;
    case CommandType::CameraPowerOn:
      return client_.powerOn() ? CommandStatus::Succeeded
                               : CommandStatus::Unavailable;
    case CommandType::CameraPowerOff:
      return client_.powerOff() ? CommandStatus::Succeeded
                                : CommandStatus::Unavailable;
    default:
      return CommandStatus::Unsupported;
  }
}

DeviceRuntimeState CanonBleDriver::runtimeState() const {
  DeviceRuntimeState state;
  switch (client_.state().link) {
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
  state.protocolReady = client_.protocolReady();
  state.quality = client_.state().recordingConfirmed
                      ? StateQuality::Confirmed
                      : StateQuality::Unknown;
  return state;
}

void CanonBleDriver::forgetPairing(const DeviceRecord& record) {
  client_.forgetBond(record.bleAddress, record.bleAddressType);
}

void CanonBleDriver::preferSkipPeer(const char* bleAddress) {
  client_.ignorePeerAddress(bleAddress);
}

bool CanonBleDriver::consumePairingUpdate(DeviceRecord& record) {
  char address[kBleAddressCapacity] = "";
  char name[kBleNameCapacity] = "";
  uint8_t addressType = 0;
  bool paired = false;
  if (!client_.consumePairingUpdate(address, sizeof(address), addressType, name,
                                    sizeof(name), paired)) {
    return false;
  }
  record.paired = paired;
  record.bleAddressType = addressType;
  std::strncpy(record.bleAddress, address, sizeof(record.bleAddress) - 1);
  std::strncpy(record.bleName, name, sizeof(record.bleName) - 1);
  return true;
}

}  // namespace studio
