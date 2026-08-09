#include "devices/zhiyun_x100/driver.h"

#include <Arduino.h>

#include <cstring>
#include <new>

#include "core/mesh/mesh_repository.h"
#include "devices/amaran_light/runtime.h"

namespace studio {

ZhiyunLightDriver::Session* ZhiyunLightDriver::find(InstanceId instanceId) {
  for (Session* session : sessions_) {
    if (session != nullptr && session->instanceId == instanceId) return session;
  }
  return nullptr;
}

const ZhiyunLightDriver::Session* ZhiyunLightDriver::find(
    InstanceId instanceId) const {
  for (const Session* session : sessions_) {
    if (session != nullptr && session->instanceId == instanceId) return session;
  }
  return nullptr;
}

bool ZhiyunLightDriver::activate(const DeviceRecord& record) {
  if (find(record.instanceId) != nullptr) return true;
  if (!repositoryHeld_) {
    if (!studio::mesh::retainRepository()) return false;
    repositoryHeld_ = true;
  }
  Session* session = nullptr;
  for (Session*& candidate : sessions_) {
    if (candidate != nullptr) continue;
    candidate = new (std::nothrow) Session;
    session = candidate;
    break;
  }
  if (session == nullptr) {
    if (sessionCount_ == 0 && repositoryHeld_) {
      studio::mesh::releaseRepository();
      repositoryHeld_ = false;
    }
    return false;
  }
  ++sessionCount_;
  session->instanceId = record.instanceId;
  session->record = record;
  const bool hasMeshNode = studio::mesh::repository().begin() &&
      studio::mesh::findNode(studio::mesh::repository().data(),
                             record.instanceId) != nullptr;
  session->sharedGateway = record.paired && hasMeshNode;
  if (session->sharedGateway) {
    amaran_light::AmaranRuntime* gateway = amaran_light::runtime();
    if (gateway == nullptr || !gateway->acquireGateway(record)) {
      amaran_light::releaseRuntimeIfIdle();
      for (Session*& candidate : sessions_) {
        if (candidate != session) continue;
        delete candidate;
        candidate = nullptr;
        break;
      }
      if (sessionCount_ > 0) --sessionCount_;
      if (sessionCount_ == 0 && repositoryHeld_) {
        studio::mesh::releaseRepository();
        repositoryHeld_ = false;
      }
      return false;
    }
    if (!session->client.activateShared(
            record.instanceId, record.bleAddress, record.bleAddressType,
            record.bleName[0] != '\0' ? record.bleName : record.displayName,
            record.paired)) {
      gateway->releaseGateway(record.instanceId);
      for (Session*& candidate : sessions_) {
        if (candidate != session) continue;
        delete candidate;
        candidate = nullptr;
        break;
      }
      if (sessionCount_ > 0) --sessionCount_;
      amaran_light::releaseRuntimeIfIdle();
      if (sessionCount_ == 0 && repositoryHeld_) {
        studio::mesh::releaseRepository();
        repositoryHeld_ = false;
      }
      return false;
    }
  } else {
    if (!session->client.activate(
            record.instanceId, record.bleAddress, record.bleAddressType,
            record.bleName[0] != '\0' ? record.bleName : record.displayName,
            record.paired)) {
      for (Session*& candidate : sessions_) {
        if (candidate != session) continue;
        delete candidate;
        candidate = nullptr;
        break;
      }
      if (sessionCount_ > 0) --sessionCount_;
      if (sessionCount_ == 0 && repositoryHeld_) {
        studio::mesh::releaseRepository();
        repositoryHeld_ = false;
      }
      return false;
    }
  }
  return true;
}

void ZhiyunLightDriver::deactivate(InstanceId instanceId) {
  Session* session = find(instanceId);
  if (session == nullptr) return;
  session->client.deactivate();
  if (session->sharedGateway) {
    if (amaran_light::AmaranRuntime* gateway =
            amaran_light::runtimeIfActive()) {
      gateway->releaseGateway(instanceId);
    }
  }
  for (Session*& candidate : sessions_) {
    if (candidate != session) continue;
    delete candidate;
    candidate = nullptr;
    break;
  }
  amaran_light::releaseRuntimeIfIdle();
  if (sessionCount_ > 0) --sessionCount_;
  if (sessionCount_ == 0 && repositoryHeld_) {
    studio::mesh::releaseRepository();
    repositoryHeld_ = false;
  }
}

void ZhiyunLightDriver::loop() {
  if (amaran_light::AmaranRuntime* runtime =
          amaran_light::runtimeIfActive()) {
    runtime->loop();
  }
  for (Session* session : sessions_) {
    if (session == nullptr) continue;
    if (session->sharedGateway) {
      amaran_light::AmaranRuntime* gateway = amaran_light::runtimeIfActive();
      if (gateway == nullptr) continue;
      const uint32_t generation = gateway->gatewayGeneration();
      if (!gateway->gatewayConnected()) {
        if (session->gatewayAttached) session->client.detachShared();
        session->gatewayAttached = false;
        session->gatewayGeneration = generation;
        session->gatewayAttachRetryAt = 0;
      } else if (!session->gatewayAttached ||
                 session->gatewayGeneration != generation) {
        const uint32_t now = millis();
        if (session->gatewayGeneration == generation &&
            static_cast<int32_t>(now - session->gatewayAttachRetryAt) < 0) {
          session->client.loop();
          continue;
        }
        session->client.detachShared();
        session->gatewayAttached = session->client.attachShared(
            gateway->gatewayClient(), gateway->gatewayLink());
        session->gatewayGeneration = generation;
        session->gatewayAttachRetryAt =
            session->gatewayAttached ? 0 : now + 1000;
      }
    }
    session->client.loop();
  }
}

CommandStatus ZhiyunLightDriver::dispatch(const DeviceCommand& command) {
  Session* session = find(command.instanceId);
  if (session == nullptr) return CommandStatus::Unavailable;
  zhiyun_x100::X100Client& client = session->client;
  switch (command.type) {
    case CommandType::Connect:
      if (session->sharedGateway) {
        amaran_light::AmaranRuntime* gateway = amaran_light::runtime();
        return gateway != nullptr && gateway->acquireGateway(session->record)
                   ? CommandStatus::Succeeded
                   : CommandStatus::Unavailable;
      }
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
