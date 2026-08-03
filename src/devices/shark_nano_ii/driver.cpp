#include "devices/shark_nano_ii/driver.h"

#include <cstring>

namespace studio {

void SharkDriver::activate(const DeviceRecord& record) {
  activeInstance_ = record.instanceId;
  active_ = true;
  client_.activate(record.bleAddress, record.bleAddressType,
                   record.bleName[0] != '\0' ? record.bleName : record.displayName,
                   record.paired);
}

void SharkDriver::deactivate() {
  if (active_) {
    client_.deactivate();
  }
  active_ = false;
  activeInstance_ = kInvalidInstanceId;
}

void SharkDriver::loop() {
  if (active_) {
    client_.loop();
  }
}

CommandStatus SharkDriver::dispatch(const DeviceCommand& command) {
  if (!active_ || command.instanceId != activeInstance_) {
    return CommandStatus::Unavailable;
  }

  switch (command.type) {
    case CommandType::Connect:
      client_.startScan();
      return CommandStatus::Succeeded;
    case CommandType::Disconnect:
      deactivate();
      return CommandStatus::Succeeded;
    case CommandType::ForgetPairing:
      client_.forgetDevice();
      return CommandStatus::Succeeded;
    case CommandType::Refresh:
      client_.refreshAll();
      return client_.connected() ? CommandStatus::Succeeded : CommandStatus::Unavailable;
    case CommandType::KeypointSet:
      client_.keypointSet(command.value0);
      return CommandStatus::Succeeded;
    case CommandType::KeypointGo:
      client_.keypointGo(command.value0);
      return CommandStatus::Succeeded;
    case CommandType::KeypointDelete:
      client_.keypointDelete(command.value0);
      return CommandStatus::Succeeded;
    case CommandType::SetSpeed:
      client_.setSpeed(command.value0, command.value1);
      return CommandStatus::Succeeded;
    case CommandType::SetHold:
      client_.setHold(command.value0, command.value1);
      return CommandStatus::Succeeded;
    case CommandType::SetRunState:
      client_.setRunState(static_cast<uint8_t>(command.value0));
      return CommandStatus::Succeeded;
    case CommandType::SetLoop:
      client_.setLoop(command.value0 != 0);
      return CommandStatus::Succeeded;
    case CommandType::SetDirection:
      client_.setDirection(command.value0 != 0);
      return CommandStatus::Succeeded;
    case CommandType::SetManualTracking:
      client_.setManualTracking(command.value0 != 0);
      return CommandStatus::Succeeded;
    case CommandType::SetMotionVector:
      client_.setMotionVector(command.value0, command.value1);
      return CommandStatus::Succeeded;
    case CommandType::StopMotion:
      client_.stopMotion();
      return CommandStatus::Succeeded;
  }
  return CommandStatus::Unsupported;
}

DeviceRuntimeState SharkDriver::runtimeState() const {
  DeviceRuntimeState state;
  switch (client_.state().link) {
    case shark::SharkClient::Link::Disconnected:
      state.link = LinkState::Disconnected;
      break;
    case shark::SharkClient::Link::Scanning:
      state.link = LinkState::Scanning;
      break;
    case shark::SharkClient::Link::Connecting:
      state.link = LinkState::Connecting;
      break;
    case shark::SharkClient::Link::Connected:
      state.link = LinkState::Connected;
      break;
  }
  state.quality = state.link == LinkState::Connected ? StateQuality::Confirmed
                                                     : StateQuality::Unknown;
  return state;
}

bool SharkDriver::consumePairingUpdate(DeviceRecord& record) {
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

