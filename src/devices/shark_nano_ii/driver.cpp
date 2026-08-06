#include "devices/shark_nano_ii/driver.h"

#include <cstring>

namespace studio {

bool SharkDriver::activate(const DeviceRecord& record) {
  if (session_.instanceId == record.instanceId) {
    return true;
  }
  if (session_.instanceId != kInvalidInstanceId) {
    return false;
  }
  session_.instanceId = record.instanceId;
  session_.client.activate(record.bleAddress, record.bleAddressType,
                   record.bleName[0] != '\0' ? record.bleName : record.displayName,
                   record.paired);
  return true;
}

void SharkDriver::deactivate(InstanceId instanceId) {
  if (session_.instanceId == instanceId) {
    session_.client.deactivate();
    session_.instanceId = kInvalidInstanceId;
  }
}

void SharkDriver::loop() {
  if (session_.instanceId != kInvalidInstanceId) {
    session_.client.loop();
  }
}

CommandStatus SharkDriver::dispatch(const DeviceCommand& command) {
  if (command.instanceId != session_.instanceId) {
    return CommandStatus::Unavailable;
  }

  switch (command.type) {
    case CommandType::Connect:
      session_.client.startScan();
      return CommandStatus::Succeeded;
    case CommandType::Disconnect:
      deactivate(command.instanceId);
      return CommandStatus::Succeeded;
    case CommandType::ForgetPairing:
      session_.client.forgetDevice();
      return CommandStatus::Succeeded;
    case CommandType::Refresh:
      session_.client.refreshAll();
      return session_.client.connected() ? CommandStatus::Succeeded
                                         : CommandStatus::Unavailable;
    case CommandType::KeypointSet:
      session_.client.keypointSet(command.value0);
      return CommandStatus::Succeeded;
    case CommandType::KeypointGo:
      session_.client.keypointGo(command.value0);
      return CommandStatus::Succeeded;
    case CommandType::KeypointDelete:
      session_.client.keypointDelete(command.value0);
      return CommandStatus::Succeeded;
    case CommandType::SetSpeed:
      session_.client.setSpeed(command.value0, command.value1);
      return CommandStatus::Succeeded;
    case CommandType::SetHold:
      session_.client.setHold(command.value0, command.value1);
      return CommandStatus::Succeeded;
    case CommandType::SetRunState:
      session_.client.setRunState(static_cast<uint8_t>(command.value0));
      return CommandStatus::Succeeded;
    case CommandType::SetLoop:
      session_.client.setLoop(command.value0 != 0);
      return CommandStatus::Succeeded;
    case CommandType::SetDirection:
      session_.client.setDirection(command.value0 != 0);
      return CommandStatus::Succeeded;
    case CommandType::SetManualTracking:
      session_.client.setManualTracking(command.value0 != 0);
      return CommandStatus::Succeeded;
    case CommandType::SetMotionVector:
      session_.client.setMotionVector(command.value0, command.value1);
      return CommandStatus::Succeeded;
    case CommandType::StopMotion:
      session_.client.stopMotion();
      return CommandStatus::Succeeded;
  }
  return CommandStatus::Unsupported;
}

DeviceRuntimeState SharkDriver::runtimeState(InstanceId instanceId) const {
  DeviceRuntimeState state;
  if (instanceId != session_.instanceId) {
    return state;
  }
  switch (session_.client.state().link) {
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
  state.protocolReady = session_.client.protocolReady();
  state.quality = state.link == LinkState::Connected ? StateQuality::Confirmed
                                                     : StateQuality::Unknown;
  return state;
}

const void* SharkDriver::specializedState(InstanceId instanceId) const {
  return instanceId == session_.instanceId ? &session_.client.state() : nullptr;
}

void SharkDriver::cancelOnboarding(const DeviceRecord& record) {
  if (session_.instanceId == record.instanceId) {
    session_.client.forgetDevice();
  }
}

bool SharkDriver::consumePairingUpdate(InstanceId instanceId,
                                       DeviceRecord& record) {
  char address[kBleAddressCapacity] = "";
  char name[kBleNameCapacity] = "";
  uint8_t addressType = 0;
  bool paired = false;
  if (instanceId != session_.instanceId ||
      !session_.client.consumePairingUpdate(address, sizeof(address), addressType,
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
