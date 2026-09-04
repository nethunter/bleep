#include "devices/zhiyun_light/driver.h"

#include <Arduino.h>

#include <cstring>
#include <new>

#include "core/mesh/mesh_repository.h"
#include "devices/aputure_light/runtime.h"
#include "devices/zhiyun_light/ble_match.h"

#if ARDUINO_USB_CDC_ON_BOOT
#define ZHIYUN_DRIVER_LOG Serial0
#else
#define ZHIYUN_DRIVER_LOG Serial
#endif

namespace studio {

InstanceProfile ZhiyunLightDriver::instanceProfile(
    const DeviceRecord& record,
    const InstanceProfile& catalogProfile) const {
  InstanceProfile profile = catalogProfile;
  zhiyun_light::MolusModel model = zhiyun_light::MolusModel::Unknown;
  if (const Session* session = find(record.instanceId))
    model = session->client.state().model;
  if (model == zhiyun_light::MolusModel::Unknown)
    model = zhiyun_light::modelFromName(record.bleName);
  if (model == zhiyun_light::MolusModel::Unknown)
    model = zhiyun_light::modelFromName(record.displayName);
  if (!zhiyun_light::supportsRgb(model))
    profile.capabilities &= ~capabilityBit(Capability::SetLightRgb);
  return profile;
}

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

bool ZhiyunLightDriver::migrateToSharedGateway(
    Session& session, const DeviceRecord& record) {
  if (session.sharedGateway || !record.paired ||
      !studio::mesh::repository().begin() ||
      studio::mesh::findNode(studio::mesh::repository().data(),
                             record.instanceId) == nullptr) {
    ZHIYUN_DRIVER_LOG.printf(
        "zhiyun_driver event=migrate_skipped instance=%lu shared=%u paired=%u\n",
        static_cast<unsigned long>(record.instanceId),
        session.sharedGateway ? 1u : 0u, record.paired ? 1u : 0u);
    return false;
  }

  // Onboarding owns a temporary direct GATT link. Once that record is saved,
  // the next owner must join the panel-owned mesh bearer instead of retaining
  // a second physical connection beside the shared Aputure/Zhiyun transport.
  session.client.deactivate();
  if (!session.client.activateShared(
          record.instanceId, record.bleAddress, record.bleAddressType,
          record.bleName[0] != '\0' ? record.bleName : record.displayName,
          record.paired)) {
    ZHIYUN_DRIVER_LOG.printf(
        "zhiyun_driver event=migrate_fallback instance=%lu reason=shared_client\n",
        static_cast<unsigned long>(record.instanceId));
    return session.client.activate(
        record.instanceId, record.bleAddress, record.bleAddressType,
        record.bleName[0] != '\0' ? record.bleName : record.displayName,
        record.paired);
  }

  aputure_light::AputureLightRuntime* gateway = aputure_light::runtime();
  if (gateway == nullptr || !gateway->acquireGateway(record)) {
    ZHIYUN_DRIVER_LOG.printf(
        "zhiyun_driver event=migrate_fallback instance=%lu reason=gateway\n",
        static_cast<unsigned long>(record.instanceId));
    session.client.deactivate();
    aputure_light::releaseRuntimeIfIdle();
    return session.client.activate(
        record.instanceId, record.bleAddress, record.bleAddressType,
        record.bleName[0] != '\0' ? record.bleName : record.displayName,
        record.paired);
  }

  session.sharedGateway = true;
  session.gatewayAttached = false;
  session.gatewayGeneration = 0xffffffffu;
  session.gatewayAttachRetryAt = 0;
  ZHIYUN_DRIVER_LOG.printf("zhiyun_driver event=migrated instance=%lu\n",
                           static_cast<unsigned long>(record.instanceId));
  return true;
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
  ZHIYUN_DRIVER_LOG.printf(
      "zhiyun_driver event=activate instance=%lu paired=%u mesh_node=%u shared=%u\n",
      static_cast<unsigned long>(record.instanceId), record.paired ? 1u : 0u,
      hasMeshNode ? 1u : 0u, session->sharedGateway ? 1u : 0u);
  if (session->sharedGateway) {
    aputure_light::AputureLightRuntime* gateway = aputure_light::runtime();
    if (gateway == nullptr || !gateway->acquireGateway(record)) {
      aputure_light::releaseRuntimeIfIdle();
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
      aputure_light::releaseRuntimeIfIdle();
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

bool ZhiyunLightDriver::resume(const DeviceRecord& record) {
  Session* session = find(record.instanceId);
  if (session == nullptr) return false;
  ZHIYUN_DRIVER_LOG.printf(
      "zhiyun_driver event=resume instance=%lu shared=%u paired=%u\n",
      static_cast<unsigned long>(record.instanceId),
      session->sharedGateway ? 1u : 0u, record.paired ? 1u : 0u);
  session->record = record;
  if (!session->sharedGateway && record.paired &&
      studio::mesh::repository().begin() &&
      studio::mesh::findNode(studio::mesh::repository().data(),
                             record.instanceId) != nullptr) {
    return migrateToSharedGateway(*session, record);
  }
  if (session->sharedGateway) {
    aputure_light::AputureLightRuntime* gateway = aputure_light::runtime();
    return gateway != nullptr && gateway->acquireGateway(record);
  }
  return session->client.resumeConnection();
}

void ZhiyunLightDriver::deactivate(InstanceId instanceId) {
  Session* session = find(instanceId);
  if (session == nullptr) return;
  session->client.deactivate();
  if (session->sharedGateway) {
    if (aputure_light::AputureLightRuntime* gateway =
            aputure_light::runtimeIfActive()) {
      gateway->releaseGateway(instanceId);
    }
  }
  for (Session*& candidate : sessions_) {
    if (candidate != session) continue;
    delete candidate;
    candidate = nullptr;
    break;
  }
  aputure_light::releaseRuntimeIfIdle();
  if (sessionCount_ > 0) --sessionCount_;
  if (sessionCount_ == 0 && repositoryHeld_) {
    studio::mesh::releaseRepository();
    repositoryHeld_ = false;
  }
}

void ZhiyunLightDriver::loop() {
  if (aputure_light::AputureLightRuntime* runtime =
          aputure_light::runtimeIfActive()) {
    runtime->loop();
  }
  bool sharedInitializationInFlight = false;
  for (Session* session : sessions_) {
    if (session == nullptr) continue;
    if (session->sharedGateway) {
      aputure_light::AputureLightRuntime* gateway = aputure_light::runtimeIfActive();
      if (gateway == nullptr) continue;
      const uint32_t generation = gateway->gatewayGeneration();
      if (!gateway->gatewayConnected()) {
        if (session->gatewayAttached) session->client.detachShared();
        session->gatewayAttached = false;
        session->gatewayGeneration = generation;
        session->gatewayAttachRetryAt = 0;
      } else if (!session->gatewayAttached ||
                 session->gatewayGeneration != generation) {
        if (sharedInitializationInFlight) {
          session->client.loop();
          continue;
        }
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
    const zhiyun_light::ZhiyunLightState::Phase phase =
        session->client.state().phase;
    if (session->sharedGateway && session->gatewayAttached &&
        (phase == zhiyun_light::ZhiyunLightState::Phase::Initializing ||
         phase == zhiyun_light::ZhiyunLightState::Phase::ReadingState)) {
      sharedInitializationInFlight = true;
    }
  }
}

CommandStatus ZhiyunLightDriver::dispatch(const DeviceCommand& command) {
  Session* session = find(command.instanceId);
  if (session == nullptr) return CommandStatus::Unavailable;
  zhiyun_light::ZhiyunLightClient& client = session->client;
  switch (command.type) {
    case CommandType::Connect:
      if (session->sharedGateway) {
        aputure_light::AputureLightRuntime* gateway = aputure_light::runtime();
        return gateway != nullptr && gateway->acquireGateway(session->record)
                   ? CommandStatus::Succeeded
                   : CommandStatus::Unavailable;
      }
      return client.resumeConnection() ? CommandStatus::Succeeded
                                       : CommandStatus::Unavailable;
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
    case CommandType::SetLightCctAndOn:
      if (!zhiyun_light::validCctCommand(command.value0, command.value1,
                                        command.value2))
        return CommandStatus::InvalidArgument;
      // The look and the following power-on are one client transaction, so the
      // fixture never shows its previous look and the command stays pending
      // until both halves are confirmed.
      return client.setCct(static_cast<uint16_t>(command.value0),
                           static_cast<uint8_t>(command.value1),
                           command.type == CommandType::SetLightCctAndOn)
                 ? CommandStatus::Succeeded
                 : CommandStatus::Unavailable;
    case CommandType::SetLightRgb:
    case CommandType::SetLightRgbAndOn:
      if (!zhiyun_light::validRgbCommand(command.value0, command.value1))
        return CommandStatus::InvalidArgument;
      return client.setRgb(static_cast<uint32_t>(command.value0),
                           static_cast<uint8_t>(command.value1),
                           command.type == CommandType::SetLightRgbAndOn)
                 ? CommandStatus::Succeeded
                 : CommandStatus::Unavailable;
    default:
      return CommandStatus::Unsupported;
  }
}

void ZhiyunLightDriver::cancelPendingCommand(InstanceId instanceId) {
  Session* session = find(instanceId);
  if (session == nullptr) return;
  session->client.cancelPendingCommand();
}

DeviceRuntimeState ZhiyunLightDriver::runtimeState(InstanceId instanceId) const {
  DeviceRuntimeState runtime;
  const Session* session = find(instanceId);
  if (session == nullptr) return runtime;
  const zhiyun_light::ZhiyunLightState& state = session->client.state();
  switch (state.link) {
    case zhiyun_light::ZhiyunLightState::Link::Disconnected:
      runtime.link = LinkState::Disconnected;
      break;
    case zhiyun_light::ZhiyunLightState::Link::Scanning:
      runtime.link = LinkState::Scanning;
      break;
    case zhiyun_light::ZhiyunLightState::Link::Connecting:
      runtime.link = LinkState::Connecting;
      break;
    case zhiyun_light::ZhiyunLightState::Link::Connected:
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

bool ZhiyunLightDriver::lightControlState(InstanceId instanceId,
                                          LightControlState& out) const {
  const Session* session = find(instanceId);
  if (session == nullptr) return false;
  const zhiyun_light::ZhiyunLightState& state = session->client.state();
  out.available = state.phase == zhiyun_light::ZhiyunLightState::Phase::Ready;
  out.supportsPower = out.supportsCct = true;
  out.supportsRgb = zhiyun_light::supportsRgb(state.model);
  out.on = state.on;
  out.stateKnown = state.confirmed;
  out.commandPending = state.commandPending;
  out.commandFailed = state.lastCommandFailed;
  out.quality = state.confirmed ? StateQuality::Confirmed : StateQuality::Unknown;
  out.minKelvin = zhiyun_light::kMinKelvin;
  out.maxKelvin = zhiyun_light::kMaxKelvin;
  out.kelvin = state.kelvin;
  out.brightness = static_cast<uint8_t>(state.brightness + 0.5f);
  out.cctBrightness = out.brightness;
  out.rgbBrightness = out.brightness;
  out.rgb = state.rgb;
  out.rgbMode = out.supportsRgb && state.mode == zhiyun_light::ZhiyunLightState::Mode::Rgb;
  std::strncpy(out.status, state.error[0] != '\0' ? state.error
                                                   : (state.confirmed ? "Ready / confirmed"
                                                                      : "State unknown"),
               sizeof(out.status) - 1);
  return true;
}

bool ZhiyunLightDriver::cancelOnboarding(const DeviceRecord& record) {
  Session* session = find(record.instanceId);
  return session == nullptr || session->client.cancelOnboarding();
}

size_t ZhiyunLightDriver::onboardingCandidateCount(
    InstanceId instanceId) const {
  const Session* session = find(instanceId);
  return session != nullptr ? session->client.onboardingCandidateCount() : 0;
}

bool ZhiyunLightDriver::onboardingCandidate(
    InstanceId instanceId, size_t index,
    OnboardingCandidate& candidate) const {
  const Session* session = find(instanceId);
  return session != nullptr &&
         session->client.onboardingCandidate(index, candidate);
}

bool ZhiyunLightDriver::selectOnboardingCandidate(InstanceId instanceId,
                                                  uint32_t token) {
  Session* session = find(instanceId);
  return session != nullptr &&
         session->client.selectOnboardingCandidate(token);
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
                                    zhiyun_light::MolusModel::X60Rgb
                                ? "MOLUS X60RGB"
                                : "MOLUS X100";
    std::strncpy(record.displayName, modelName,
                 sizeof(record.displayName) - 1);
    record.displayName[sizeof(record.displayName) - 1] = '\0';
  }
  session->record = record;
  return true;
}

}  // namespace studio
