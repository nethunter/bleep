#include "devices/tascam_x8/driver.h"

#include <cstring>

namespace studio {

void TascamX8Driver::activate(const DeviceRecord& record) {
  activeInstance_ = record.instanceId;
  active_ = true;
  client_.activate(record.bleAddress, record.bleAddressType,
                   record.bleName[0] != '\0' ? record.bleName
                                             : record.displayName,
                   record.paired);
}

void TascamX8Driver::deactivate() {
  if (active_) {
    client_.deactivate();
  }
  active_ = false;
  activeInstance_ = kInvalidInstanceId;
}

void TascamX8Driver::loop() {
  if (active_) {
    client_.loop();
  }
}

CommandStatus TascamX8Driver::dispatch(const DeviceCommand& command) {
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
    default:
      return CommandStatus::Unsupported;
  }
}

DeviceRuntimeState TascamX8Driver::runtimeState() const {
  DeviceRuntimeState state;
  switch (client_.state().link) {
    case tascam_x8::TascamX8State::Link::Disconnected:
      state.link = LinkState::Disconnected;
      break;
    case tascam_x8::TascamX8State::Link::Scanning:
      state.link = LinkState::Scanning;
      break;
    case tascam_x8::TascamX8State::Link::Connecting:
      state.link = LinkState::Connecting;
      break;
    case tascam_x8::TascamX8State::Link::Connected:
      state.link = LinkState::Connected;
      break;
  }
  state.quality = client_.state().recordingConfirmed
                      ? StateQuality::Confirmed
                      : StateQuality::Unknown;
  return state;
}

bool TascamX8Driver::consumePairingUpdate(DeviceRecord& record) {
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
  record.bleAddress[sizeof(record.bleAddress) - 1] = '\0';
  std::strncpy(record.bleName, name, sizeof(record.bleName) - 1);
  record.bleName[sizeof(record.bleName) - 1] = '\0';
  return true;
}

}  // namespace studio
