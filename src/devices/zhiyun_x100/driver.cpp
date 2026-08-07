#include "devices/zhiyun_x100/driver.h"

#include <cstring>

namespace studio {

ZhiyunLightDriver::Session* ZhiyunLightDriver::find(InstanceId instanceId) {
  for (Session& session : sessions_) {
    if (session.instanceId == instanceId) return &session;
  }
  return nullptr;
}

const ZhiyunLightDriver::Session* ZhiyunLightDriver::find(
    InstanceId instanceId) const {
  for (const Session& session : sessions_) {
    if (session.instanceId == instanceId) return &session;
  }
  return nullptr;
}

bool ZhiyunLightDriver::activate(const DeviceRecord& record) {
  if (find(record.instanceId) != nullptr) return true;
  Session* session = find(kInvalidInstanceId);
  if (session == nullptr) return false;
  session->instanceId = record.instanceId;
  session->client.activate(
      record.instanceId, record.bleAddress, record.bleAddressType,
      record.bleName[0] != '\0' ? record.bleName : record.displayName,
      record.paired);
  return true;
}

void ZhiyunLightDriver::deactivate(InstanceId instanceId) {
  Session* session = find(instanceId);
  if (session == nullptr) return;
  session->client.deactivate();
  session->instanceId = kInvalidInstanceId;
}

void ZhiyunLightDriver::loop() {
  for (Session& session : sessions_) {
    if (session.instanceId != kInvalidInstanceId) session.client.loop();
  }
}

CommandStatus ZhiyunLightDriver::dispatch(const DeviceCommand& command) {
  Session* session = find(command.instanceId);
  if (session == nullptr) return CommandStatus::Unavailable;
  zhiyun_x100::X100Client& client = session->client;
  switch (command.type) {
    case CommandType::Connect:
      client.startScan();
      return CommandStatus::Succeeded;
    case CommandType::ForgetPairing:
      client.forgetDevice();
      return CommandStatus::Succeeded;
    case CommandType::Refresh:
      return client.refresh() ? CommandStatus::Succeeded
                               : CommandStatus::Unavailable;
    case CommandType::TurnOn:
      return client.setPower(true) ? CommandStatus::Succeeded
                                    : CommandStatus::Unavailable;
    case CommandType::TurnOff:
      return client.setPower(false) ? CommandStatus::Succeeded
                                     : CommandStatus::Unavailable;
    case CommandType::SetLightCct:
      if (!zhiyun_x100::validCctCommand(command.value0, command.value1,
                                        command.value2))
        return CommandStatus::InvalidArgument;
      return client.setCct(static_cast<uint16_t>(command.value0),
                           static_cast<uint8_t>(command.value1))
                 ? CommandStatus::Succeeded
                 : CommandStatus::Unavailable;
    case CommandType::SetLightRgb:
      if (!zhiyun_x100::validRgbCommand(command.value0, command.value1))
        return CommandStatus::InvalidArgument;
      return client.setRgb(static_cast<uint32_t>(command.value0),
                           static_cast<uint8_t>(command.value1))
                 ? CommandStatus::Succeeded
                 : CommandStatus::Unavailable;
    default:
      return CommandStatus::Unsupported;
  }
}

DeviceRuntimeState ZhiyunLightDriver::runtimeState(InstanceId instanceId) const {
  DeviceRuntimeState runtime;
  const Session* session = find(instanceId);
  if (session == nullptr) return runtime;
  const zhiyun_x100::X100State& state = session->client.state();
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
  runtime.protocolReady = session->client.protocolReady();
  runtime.quality = state.confirmed ? StateQuality::Confirmed
                                    : StateQuality::Unknown;
  runtime.commandPending = state.commandPending;
  runtime.commandFailed = state.lastCommandFailed;
  return runtime;
}

const void* ZhiyunLightDriver::specializedState(InstanceId instanceId) const {
  const Session* session = find(instanceId);
  return session != nullptr ? &session->client.state() : nullptr;
}

void ZhiyunLightDriver::cancelOnboarding(const DeviceRecord& record) {
  Session* session = find(record.instanceId);
  if (session != nullptr) session->client.forgetDevice();
}

void ZhiyunLightDriver::preferSkipPeer(InstanceId instanceId,
                                       const char* bleAddress) {
  Session* session = find(instanceId);
  if (session != nullptr) session->client.ignorePeerAddress(bleAddress);
}

bool ZhiyunLightDriver::consumePairingUpdate(InstanceId instanceId,
                                              DeviceRecord& record) {
  char address[kBleAddressCapacity] = "";
  char name[kBleNameCapacity] = "";
  uint8_t addressType = 0;
  bool paired = false;
  Session* session = find(instanceId);
  if (session == nullptr ||
      !session->client.consumePairingUpdate(address, sizeof(address),
                                            addressType, name, sizeof(name),
                                            paired))
    return false;
  record.paired = paired;
  record.bleAddressType = addressType;
  std::strncpy(record.bleAddress, address, sizeof(record.bleAddress) - 1);
  record.bleAddress[sizeof(record.bleAddress) - 1] = '\0';
  std::strncpy(record.bleName, name, sizeof(record.bleName) - 1);
  record.bleName[sizeof(record.bleName) - 1] = '\0';
  if (std::strcmp(record.displayName, "Zhiyun Light") == 0) {
    const char* modelName = session->client.state().model ==
                                    zhiyun_x100::MolusModel::X60Rgb
                                ? "MOLUS X60RGB"
                                : "MOLUS X100";
    std::strncpy(record.displayName, modelName,
                 sizeof(record.displayName) - 1);
    record.displayName[sizeof(record.displayName) - 1] = '\0';
  }
  return true;
}

}  // namespace studio
