#include "devices/zhiyun_x100/driver.h"

#include <cstring>

namespace studio {

bool ZhiyunX100Driver::activate(const DeviceRecord& record) {
  if (instanceId_ == record.instanceId) return true;
  if (instanceId_ != kInvalidInstanceId) return false;
  instanceId_ = record.instanceId;
  client_.activate(record.instanceId, record.bleAddress, record.bleAddressType,
                   record.bleName[0] != '\0' ? record.bleName
                                              : record.displayName,
                   record.paired);
  return true;
}

void ZhiyunX100Driver::deactivate(InstanceId instanceId) {
  if (instanceId != instanceId_) return;
  client_.deactivate();
  instanceId_ = kInvalidInstanceId;
}

void ZhiyunX100Driver::loop() {
  if (instanceId_ != kInvalidInstanceId) client_.loop();
}

CommandStatus ZhiyunX100Driver::dispatch(const DeviceCommand& command) {
  if (command.instanceId != instanceId_) return CommandStatus::Unavailable;
  switch (command.type) {
    case CommandType::Connect:
      client_.startScan();
      return CommandStatus::Succeeded;
    case CommandType::ForgetPairing:
      client_.forgetDevice();
      return CommandStatus::Succeeded;
    case CommandType::Refresh:
      return client_.refresh() ? CommandStatus::Succeeded
                               : CommandStatus::Unavailable;
    case CommandType::TurnOn:
      return client_.setPower(true) ? CommandStatus::Succeeded
                                    : CommandStatus::Unavailable;
    case CommandType::TurnOff:
      return client_.setPower(false) ? CommandStatus::Succeeded
                                     : CommandStatus::Unavailable;
    case CommandType::SetLightCct:
      if (!zhiyun_x100::validCctCommand(command.value0, command.value1,
                                        command.value2))
        return CommandStatus::InvalidArgument;
      return client_.setCct(static_cast<uint16_t>(command.value0),
                            static_cast<uint8_t>(command.value1))
                 ? CommandStatus::Succeeded
                 : CommandStatus::Unavailable;
    default:
      return CommandStatus::Unsupported;
  }
}

DeviceRuntimeState ZhiyunX100Driver::runtimeState(InstanceId instanceId) const {
  DeviceRuntimeState runtime;
  if (instanceId != instanceId_) return runtime;
  const zhiyun_x100::X100State& state = client_.state();
  switch (state.link) {
    case zhiyun_x100::X100State::Link::Disconnected:
      runtime.link = LinkState::Disconnected;
      break;
    case zhiyun_x100::X100State::Link::Scanning:
      runtime.link = LinkState::Scanning;
      break;
    case zhiyun_x100::X100State::Link::Connecting:
      runtime.link = LinkState::Connecting;
      break;
    case zhiyun_x100::X100State::Link::Connected:
      runtime.link = LinkState::Connected;
      break;
  }
  runtime.protocolReady = client_.protocolReady();
  runtime.quality = state.confirmed ? StateQuality::Confirmed
                                    : StateQuality::Unknown;
  runtime.commandPending = state.commandPending;
  runtime.commandFailed = state.lastCommandFailed;
  return runtime;
}

const void* ZhiyunX100Driver::specializedState(InstanceId instanceId) const {
  return instanceId == instanceId_ ? &client_.state() : nullptr;
}

void ZhiyunX100Driver::cancelOnboarding(const DeviceRecord& record) {
  if (record.instanceId == instanceId_) client_.forgetDevice();
}

bool ZhiyunX100Driver::consumePairingUpdate(InstanceId instanceId,
                                             DeviceRecord& record) {
  char address[kBleAddressCapacity] = "";
  char name[kBleNameCapacity] = "";
  uint8_t addressType = 0;
  bool paired = false;
  if (instanceId != instanceId_ ||
      !client_.consumePairingUpdate(address, sizeof(address), addressType, name,
                                    sizeof(name), paired))
    return false;
  record.paired = paired;
  record.bleAddressType = addressType;
  std::strncpy(record.bleAddress, address, sizeof(record.bleAddress) - 1);
  record.bleAddress[sizeof(record.bleAddress) - 1] = '\0';
  std::strncpy(record.bleName, name, sizeof(record.bleName) - 1);
  record.bleName[sizeof(record.bleName) - 1] = '\0';
  return true;
}

}  // namespace studio
