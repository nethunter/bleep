#include "devices/tascam_x8/driver.h"

#include <cstring>
#include <new>

namespace studio {

bool TascamX8Driver::activate(const DeviceRecord& record) {
  if (session_ != nullptr && session_->instanceId == record.instanceId) {
    return true;
  }
  if (session_ != nullptr) {
    return false;
  }
  session_ = new (std::nothrow) Session;
  if (session_ == nullptr) return false;
  session_->instanceId = record.instanceId;
  session_->client.activate(record.bleAddress, record.bleAddressType,
                           record.bleName[0] != '\0' ? record.bleName
                                                     : record.displayName,
                           record.paired);
  return true;
}

void TascamX8Driver::deactivate(InstanceId instanceId) {
  if (session_ != nullptr && session_->instanceId == instanceId) {
    session_->client.deactivate();
    delete session_;
    session_ = nullptr;
  }
}

void TascamX8Driver::loop() {
  if (session_ != nullptr) {
    session_->client.loop();
  }
}

CommandStatus TascamX8Driver::dispatch(const DeviceCommand& command) {
  if (session_ == nullptr || command.instanceId != session_->instanceId) {
    return CommandStatus::Unavailable;
  }
  switch (command.type) {
    case CommandType::Connect:
      session_->client.startScan();
      return CommandStatus::Succeeded;
    case CommandType::ForgetPairing:
      session_->client.forgetDevice();
      return CommandStatus::Succeeded;
    case CommandType::RecordStart:
      return session_->client.startRecording() ? CommandStatus::Succeeded
                                              : CommandStatus::Unavailable;
    case CommandType::RecordStop:
      return session_->client.stopRecording() ? CommandStatus::Succeeded
                                             : CommandStatus::Unavailable;
    default:
      return CommandStatus::Unsupported;
  }
}

DeviceRuntimeState TascamX8Driver::runtimeState(InstanceId instanceId) const {
  DeviceRuntimeState state;
  if (session_ == nullptr || instanceId != session_->instanceId) {
    return state;
  }
  const tascam_x8::TascamX8State& clientState = session_->client.state();
  switch (clientState.link) {
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
  state.protocolReady = session_->client.protocolReady();
  state.quality = clientState.recordingConfirmed ? StateQuality::Confirmed
                                                 : StateQuality::Unknown;
  state.commandPending = clientState.commandPending;
  state.commandFailed = clientState.lastCommandFailed;
  state.recordingConfirmed = clientState.recordingConfirmed;
  state.recording =
      clientState.recording == tascam_x8::TascamX8State::Recording::Recording;
  return state;
}

const void* TascamX8Driver::specializedState(InstanceId instanceId) const {
  return session_ != nullptr && instanceId == session_->instanceId
             ? &session_->client.state()
             : nullptr;
}

void TascamX8Driver::cancelOnboarding(const DeviceRecord& record) {
  if (session_ != nullptr && session_->instanceId == record.instanceId) {
    session_->client.forgetDevice();
  }
}

bool TascamX8Driver::consumePairingUpdate(InstanceId instanceId,
                                          DeviceRecord& record) {
  char address[kBleAddressCapacity] = "";
  char name[kBleNameCapacity] = "";
  uint8_t addressType = 0;
  bool paired = false;
  if (session_ == nullptr || instanceId != session_->instanceId ||
      !session_->client.consumePairingUpdate(address, sizeof(address), addressType,
                                            name, sizeof(name), paired)) {
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
