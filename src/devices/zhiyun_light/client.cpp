#include "devices/zhiyun_light/client.h"

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <esp_random.h>

#include <cmath>
#include <cstring>
#include <new>

#include "core/ble/ble_runtime.h"
#include "core/ble/ble_timing.h"
#include "core/preferences_store.h"
#include "core/mesh/mesh_repository.h"
#include "devices/zhiyun_light/ble_match.h"
#include "freertos/FreeRTOS.h"
#include "freertos/stream_buffer.h"

#if ARDUINO_USB_CDC_ON_BOOT
#define ZHIYUN_LIGHT_LOG Serial0
#else
#define ZHIYUN_LIGHT_LOG Serial
#endif

namespace zhiyun_light {
namespace {

constexpr size_t kMaxMolusClients = 4;
ZhiyunLightClient* gNotifyClients[kMaxMolusClients] = {};
uint16_t gSharedSequence = 2;

void notifyTrampoline(NimBLERemoteCharacteristic* characteristic, uint8_t* data,
                      size_t length, bool) {
  for (ZhiyunLightClient* client : gNotifyClients) {
    if (client != nullptr && client->ownsNotifyCharacteristic(characteristic)) {
      client->onNotifyBytes(data, length);
    }
  }
}

void registerNotifyClient(ZhiyunLightClient* client) {
  for (ZhiyunLightClient*& slot : gNotifyClients) {
    if (slot == client) return;
    if (slot == nullptr) {
      slot = client;
      return;
    }
  }
}

void unregisterNotifyClient(ZhiyunLightClient* client) {
  for (ZhiyunLightClient*& slot : gNotifyClients) {
    if (slot == client) slot = nullptr;
  }
}

}  // namespace

bool ZhiyunLightClient::begin() {
  if (initialized_) return true;
  if (notifyStream_ == nullptr) notifyStream_ = xStreamBufferCreate(512, 1);
  if (notifyStream_ == nullptr) return false;
  studio::ble::ConnectPolicy policy;
  policy.connectTimeoutMs = 4000;
  policy.connectWatchdogMs = 7000;
  policy.directAttemptsBeforeScan = kDirectAttemptsBeforeScan;
  policy.security = studio::ble::SecurityPolicy::None;
  policy.diagnosticTag = "zhiyun_light";
  linkHandle_ = studio::ble::bleCentral().acquire(*this, policy);
  initialized_ = linkHandle_ != studio::ble::kInvalidLinkHandle;
  if (!initialized_) {
    vStreamBufferDelete(static_cast<StreamBufferHandle_t>(notifyStream_));
    notifyStream_ = nullptr;
    return false;
  }
  registerNotifyClient(this);
  return true;
}

bool ZhiyunLightClient::beginShared() {
  if (initialized_ && sharedTransport_) return true;
  if (notifyStream_ == nullptr) notifyStream_ = xStreamBufferCreate(512, 1);
  initialized_ = notifyStream_ != nullptr;
  if (!initialized_) return false;
  sharedTransport_ = true;
  registerNotifyClient(this);
  return true;
}

void ZhiyunLightClient::prepareActivation(studio::InstanceId instanceId,
                                   const char* address, uint8_t addressType,
                                   const char* name, bool paired) {
  instanceId_ = instanceId;
  connectRequested_ = initialized_;
  haveTarget_ = paired && address != nullptr && address[0] != '\0';
  targetAddress_[0] = '\0';
  targetName_[0] = '\0';
  ignoredAddressCount_ = 0;
  candidates_.clear();
  targetAddressType_ = addressType;
  if (haveTarget_) {
    std::strncpy(targetAddress_, address, sizeof(targetAddress_) - 1);
    targetAddress_[sizeof(targetAddress_) - 1] = '\0';
  }
  if (name != nullptr) {
    std::strncpy(targetName_, name, sizeof(targetName_) - 1);
    targetName_[sizeof(targetName_) - 1] = '\0';
  }
  state_.hasSavedDevice = haveTarget_;
  state_.model = modelFromName(targetName_);
  std::strncpy(state_.deviceName, targetName_, sizeof(state_.deviceName) - 1);
  state_.deviceName[sizeof(state_.deviceName) - 1] = '\0';
  state_.error[0] = '\0';
  state_.lastCommandFailed = false;
  state_.brightnessLimited = false;
  state_.maxBrightness = 100;
}

bool ZhiyunLightClient::activate(studio::InstanceId instanceId, const char* address,
                          uint8_t addressType, const char* name, bool paired) {
  sharedTransport_ = false;
  if (!begin()) return false;
  prepareActivation(instanceId, address, addressType, name, paired);
  routingSelector_ = 0;
  if (loadMesh()) {
    const studio::mesh::NodeRecord* node =
        studio::mesh::findNode(studio::mesh::repository().data(), instanceId);
    if (node != nullptr && node->routingSelector != 0xff) {
      routingSelector_ = node->routingSelector;
    }
  }
  if (!connectRequested_) return false;
  if (haveTarget_) beginConnect();
  else beginScan();
  return true;
}

bool ZhiyunLightClient::activateShared(studio::InstanceId instanceId,
                                const char* address, uint8_t addressType,
                                const char* name, bool paired) {
  if (!beginShared()) return false;
  prepareActivation(instanceId, address, addressType, name, paired);
  routingSelector_ = 0;
  if (loadMesh()) {
    const studio::mesh::NodeRecord* node =
        studio::mesh::findNode(studio::mesh::repository().data(), instanceId);
    if (node != nullptr && node->routingSelector != 0xff) {
      routingSelector_ = node->routingSelector;
    }
  }
  state_.link = ZhiyunLightState::Link::Connecting;
  state_.phase = ZhiyunLightState::Phase::Idle;
  return true;
}

bool ZhiyunLightClient::attachShared(void* nativeClient,
                              studio::ble::LinkHandle link) {
  if (!sharedTransport_ || nativeClient == nullptr ||
      link == studio::ble::kInvalidLinkHandle) return false;
  NimBLEClient* client = static_cast<NimBLEClient*>(nativeClient);
  if (client_ == client && state_.phase == ZhiyunLightState::Phase::Ready) return true;
  client_ = client;
  linkHandle_ = link;
  setupPending_ = false;
  awaitingResponse_ = false;
  operation_ = Operation::None;
  return client_->isConnected() && completeConnect();
}

void ZhiyunLightClient::detachShared() {
  if (!sharedTransport_) return;
  client_ = nullptr;
  writeCharacteristic_ = nullptr;
  notifyCharacteristic_ = nullptr;
  awaitingResponse_ = false;
  operation_ = Operation::None;
  initializationReadyAtMs_ = 0;
  state_.link = ZhiyunLightState::Link::Disconnected;
  state_.phase = ZhiyunLightState::Phase::Idle;
  state_.commandPending = false;
  linkHandle_ = studio::ble::kInvalidLinkHandle;
}

void ZhiyunLightClient::cancelPendingCommand() {
  awaitingResponse_ = false;
  operation_ = Operation::None;
  verifyAtMs_ = 0;
  responseDeadlineMs_ = 0;
  verificationAttempts_ = 0;
  state_.commandPending = false;
  state_.lastCommandFailed = false;
  state_.verificationField = 0;
  state_.error[0] = '\0';
}

bool ZhiyunLightClient::resumeConnection() {
  if (!initialized_ || sharedTransport_) return false;
  connectRequested_ = true;
  state_.error[0] = '\0';
  state_.lastCommandFailed = false;
  const studio::ble::LinkPhase phase =
      studio::ble::bleCentral().phase(linkHandle_);
  if (protocolReady()) return true;
  if (state_.hasSavedDevice && haveTarget_) {
    if (state_.link != ZhiyunLightState::Link::Connected)
      state_.link = ZhiyunLightState::Link::Connecting;
    if (state_.phase == ZhiyunLightState::Phase::Failed)
      state_.phase = ZhiyunLightState::Phase::Idle;
    if (phase == studio::ble::LinkPhase::Idle) beginConnect();
    return true;
  }
  startScan();
  return true;
}

void ZhiyunLightClient::deactivate() {
  discardProvisioningSnapshot();
  connectRequested_ = false;
  if (!sharedTransport_ && linkHandle_ != studio::ble::kInvalidLinkHandle)
    studio::ble::bleCentral().release(linkHandle_);
  linkHandle_ = studio::ble::kInvalidLinkHandle;
  initialized_ = false;
  sharedTransport_ = false;
  client_ = nullptr;
  writeCharacteristic_ = nullptr;
  notifyCharacteristic_ = nullptr;
  provisioningIn_ = nullptr;
  provisioningOut_ = nullptr;
  unregisterNotifyClient(this);
  if (notifyStream_ != nullptr) {
    vStreamBufferDelete(static_cast<StreamBufferHandle_t>(notifyStream_));
    notifyStream_ = nullptr;
  }
  setupPending_ = false;
  awaitingResponse_ = false;
  operation_ = Operation::None;
  provisioner_.cancel();
  provisioningDeadlineMs_ = 0;
  initializationReadyAtMs_ = 0;
  provisioningLink_ = false;
  scanAfterProvision_ = false;
  provisioningLength_ = 0;
  scanner_.reset();
  state_.link = ZhiyunLightState::Link::Disconnected;
  state_.phase = ZhiyunLightState::Phase::Idle;
  state_.commandPending = false;
  instanceId_ = studio::kInvalidInstanceId;
}

void ZhiyunLightClient::loop() {
  drainNotifications();
  if (!connectRequested_) return;
  const uint32_t now = millis();
  if (state_.phase == ZhiyunLightState::Phase::ReadingState &&
      initializationReadyAtMs_ != 0 &&
      static_cast<int32_t>(now - initializationReadyAtMs_) >= 0) {
    initializationReadyAtMs_ = 0;
    markInitializationReady();
  }
  if (setupPending_ && static_cast<int32_t>(now - setupAtMs_) >= 0) {
    setupPending_ = false;
    if (client_ == nullptr || !client_->isConnected() || !completeConnect()) {
      if (!state_.hasSavedDevice) {
        returnToOnboardingPicker("Zhiyun setup failed");
        return;
      }
      state_.phase = ZhiyunLightState::Phase::Failed;
      std::strncpy(state_.error, "Zhiyun setup failed",
                   sizeof(state_.error) - 1);
      state_.error[sizeof(state_.error) - 1] = '\0';
      markProtocolFailed();
      return;
    }
  }
  if (state_.phase == ZhiyunLightState::Phase::Provisioning &&
      provisioningDeadlineMs_ != 0 &&
      static_cast<int32_t>(now - provisioningDeadlineMs_) >= 0) {
    provisioningDeadlineMs_ = 0;
    provisioner_.cancel();
    if (!state_.hasSavedDevice) {
      returnToOnboardingPicker("Provisioning timeout");
      return;
    }
    state_.phase = ZhiyunLightState::Phase::Failed;
    std::strncpy(state_.error, "Provisioning timeout",
                 sizeof(state_.error) - 1);
    state_.error[sizeof(state_.error) - 1] = '\0';
    markProtocolFailed();
    return;
  }
  if (awaitingResponse_ &&
      static_cast<int32_t>(now - responseDeadlineMs_) >= 0) {
    ZHIYUN_LIGHT_LOG.printf(
        "zhiyun_light event=response_timeout instance=%lu operation=%u "
        "step=%u command=0x%04x selector=%u shared=%u\n",
        static_cast<unsigned long>(instanceId_),
        static_cast<unsigned>(operation_), step_, expectedCommand_, selector(),
        sharedTransport_ ? 1u : 0u);
    if (operation_ == Operation::Initialize) {
      if (onboardingSelectionActive_) {
        returnToOnboardingPicker("Initialization timeout");
        awaitingResponse_ = false;
        return;
      }
      state_.phase = ZhiyunLightState::Phase::Failed;
      std::strncpy(state_.error, "Initialization timeout",
                   sizeof(state_.error) - 1);
      state_.error[sizeof(state_.error) - 1] = '\0';
      markProtocolFailed();
    } else {
      finishCommand(false, "No state confirmation");
    }
    awaitingResponse_ = false;
  }
  if (state_.commandPending && !awaitingResponse_ && verifyAtMs_ != 0 &&
      static_cast<int32_t>(now - verifyAtMs_) >= 0) {
    verifyAtMs_ = 0;
    if (!sendVerificationStep()) finishCommand(false, "Readback failed");
  }
}

bool ZhiyunLightClient::protocolReady() const {
  return linkHandle_ != studio::ble::kInvalidLinkHandle &&
         state_.phase == ZhiyunLightState::Phase::Ready &&
         (sharedTransport_ ||
          studio::ble::bleCentral().protocolReady(linkHandle_));
}

bool ZhiyunLightClient::ownsNotifyCharacteristic(
    const NimBLERemoteCharacteristic* characteristic) const {
  return characteristic != nullptr &&
         (characteristic == notifyCharacteristic_ ||
          characteristic == provisioningOut_);
}

const char* ZhiyunLightClient::identityMarker() const {
  return state_.model == MolusModel::X60Rgb ? "plx104" : "pl105";
}

uint16_t ZhiyunLightClient::nextSequence() {
  if (sharedTransport_) {
    const uint16_t result = gSharedSequence;
    ++gSharedSequence;
    if (gSharedSequence == 0) gSharedSequence = 1;
    return result;
  }
  const uint16_t result = sequence_;
  ++sequence_;
  if (sequence_ == 0) sequence_ = 1;
  return result;
}

void ZhiyunLightClient::markProtocolReady() {
  if (!sharedTransport_) studio::ble::bleCentral().markProtocolReady(linkHandle_);
}

void ZhiyunLightClient::markProtocolFailed() {
  if (!sharedTransport_) studio::ble::bleCentral().markProtocolFailed(linkHandle_);
}

bool ZhiyunLightClient::writeFrame(const FrameBytes& frame) {
  return writeCharacteristic_ != nullptr && frame.length > 0 &&
         writeCharacteristic_->writeValue(frame.bytes, frame.length, false);
}

bool ZhiyunLightClient::sendQuery(uint16_t command, const uint8_t* payload,
                           size_t payloadLength) {
  const uint16_t sequence = nextSequence();
  const FrameBytes frame = payload == nullptr
                               ? buildReadRequest(sequence, command, selector())
                               : buildRequest(sequence, command, payload,
                                              payloadLength);
  if (!writeFrame(frame)) return false;
  expectedSequence_ = sequence;
  expectedCommand_ = command;
  awaitingResponse_ = true;
  responseDeadlineMs_ = millis() + 2000;
  return true;
}

bool ZhiyunLightClient::sendInitializationStep() {
  static constexpr uint16_t x100Commands[] = {
      kCommandIdentity, kCommandFirmware, kCommandStatus, kCommandMode,
      kCommandBrightness, kCommandCct, kCommandPower,
  };
  static constexpr uint16_t x60Commands[] = {
      kCommandIdentity, kCommandFirmware, kCommandStatus, kCommandMode,
      kCommandCct, kCommandPower, kCommandBrightness,
  };
  if (step_ >= sizeof(x100Commands) / sizeof(x100Commands[0])) return false;
  const uint16_t* commands =
      state_.model == MolusModel::X60Rgb ? x60Commands : x100Commands;
  if (step_ <= 2) {
    const uint16_t sequence = nextSequence();
    const FrameBytes frame = buildRequest(sequence, commands[step_], nullptr, 0);
    if (!writeFrame(frame)) return false;
    expectedSequence_ = sequence;
    expectedCommand_ = commands[step_];
    awaitingResponse_ = true;
    responseDeadlineMs_ = millis() + 2000;
    if (sharedTransport_) {
      ZHIYUN_LIGHT_LOG.printf(
          "zhiyun_light event=init_send instance=%lu step=%u command=0x%04x "
          "sequence=%u selector=%u\n",
          static_cast<unsigned long>(instanceId_), step_, commands[step_],
          sequence, selector());
    }
    return true;
  }
  if (step_ == 3) {
    const uint8_t modePayload[] = {selector(), 0x80, 0x00, 0x00};
    const bool sent = sendQuery(kCommandMode, modePayload, sizeof(modePayload));
    if (sent && sharedTransport_) {
      ZHIYUN_LIGHT_LOG.printf(
          "zhiyun_light event=init_send instance=%lu step=%u command=0x%04x "
          "sequence=%u selector=%u\n",
          static_cast<unsigned long>(instanceId_), step_, kCommandMode,
          expectedSequence_, selector());
    }
    return sent;
  }
  const bool sent = sendQuery(commands[step_]);
  if (sent && sharedTransport_) {
    ZHIYUN_LIGHT_LOG.printf(
        "zhiyun_light event=init_send instance=%lu step=%u command=0x%04x "
        "sequence=%u selector=%u\n",
        static_cast<unsigned long>(instanceId_), step_, commands[step_],
        expectedSequence_, selector());
  }
  return sent;
}

bool ZhiyunLightClient::completeConnect() {
  if (provisioningLink_) return setupProvisioning();
  const uint32_t started = millis();
  NimBLERemoteService* service =
      client_->getService(NimBLEUUID(kControlServiceUuid));
  if (service == nullptr) return false;
  writeCharacteristic_ =
      service->getCharacteristic(NimBLEUUID(kWriteCharacteristicUuid));
  notifyCharacteristic_ =
      service->getCharacteristic(NimBLEUUID(kNotifyCharacteristicUuid));
  if (writeCharacteristic_ == nullptr || notifyCharacteristic_ == nullptr ||
      !notifyCharacteristic_->subscribe(true, notifyTrampoline, true)) {
    writeCharacteristic_ = nullptr;
    notifyCharacteristic_ = nullptr;
    return false;
  }
  scanner_.reset();
  sequence_ = 2;
  step_ = 0;
  operation_ = Operation::Initialize;
  state_.link = ZhiyunLightState::Link::Connected;
  state_.phase = ZhiyunLightState::Phase::Initializing;
  studio::ble::logTiming(
      "zhiyun_light", linkHandle_, "gatt_setup", millis() - started,
      millis() - studio::ble::bleCentral().timingStartedAt(linkHandle_), "ok");
  return sendInitializationStep();
}

bool ZhiyunLightClient::loadMesh() {
  return studio::mesh::repository().begin();
}

bool ZhiyunLightClient::setupProvisioning() {
  NimBLERemoteService* service = client_->getService(
      NimBLEUUID("00001827-0000-1000-8000-00805f9b34fb"));
  if (service == nullptr) return false;
  provisioningIn_ = service->getCharacteristic(
      NimBLEUUID("00002adb-0000-1000-8000-00805f9b34fb"));
  provisioningOut_ = service->getCharacteristic(
      NimBLEUUID("00002adc-0000-1000-8000-00805f9b34fb"));
  if (provisioningIn_ == nullptr || provisioningOut_ == nullptr ||
      !provisioningOut_->subscribe(true, notifyTrampoline, true) || !loadMesh()) {
    provisioningIn_ = nullptr;
    provisioningOut_ = nullptr;
    return false;
  }
  provisioningLength_ = 0;
  state_.link = ZhiyunLightState::Link::Connected;
  state_.phase = ZhiyunLightState::Phase::Provisioning;
  provisioningDeadlineMs_ = millis() + 30000;
  studio::mesh::StoreData& meshData = studio::mesh::repository().data();
  return provisioner_.begin(meshData.network.networkKey,
                            meshData.network.ivIndex,
                            meshData.network.nextUnicastAddress, *this);
}

bool ZhiyunLightClient::sendProvisioningPdu(const uint8_t* pdu, size_t length) {
  if (provisioningIn_ == nullptr || pdu == nullptr || length + 1 > 80)
    return false;
  uint8_t wrapped[80] = {0x03};
  std::memcpy(wrapped + 1, pdu, length);
  return provisioningIn_->writeValue(wrapped, length + 1, false);
}

bool ZhiyunLightClient::finishProvisioning() {
  if (!provisioner_.complete() || instanceId_ == studio::kInvalidInstanceId)
    return false;
  studio::mesh::StoreData& meshData = studio::mesh::repository().data();
  const studio::mesh::StoreData previous = meshData;
  studio::mesh::StoreData* snapshot =
      new (std::nothrow) studio::mesh::StoreData(previous);
  if (snapshot == nullptr) return false;
  studio::mesh::NodeRecord node;
  node.instanceId = instanceId_;
  node.model = studio::DriverId::ZhiyunLight;
  node.unicastAddress = meshData.network.nextUnicastAddress;
  node.elementCount = provisioner_.elementCount();
  node.configured = true;
  node.routingSelector =
      studio::mesh::nextZhiyunRoutingSelector(meshData);
  if (node.routingSelector == 0xff) {
    delete snapshot;
    return false;
  }
  std::memcpy(node.deviceKey, provisioner_.deviceKey(), sizeof(node.deviceKey));
  std::strncpy(node.bleAddress, targetAddress_, sizeof(node.bleAddress) - 1);
  node.bleAddress[sizeof(node.bleAddress) - 1] = '\0';
  node.bleAddressType = targetAddressType_;
  if (node.unicastAddress > 0x7fff - node.elementCount ||
      !studio::mesh::upsertNode(meshData, node)) {
    delete snapshot;
    return false;
  }
  meshData.network.nextUnicastAddress =
      static_cast<uint16_t>(node.unicastAddress + node.elementCount);
  if (!studio::mesh::repository().save()) {
    meshData = previous;
    delete snapshot;
    return false;
  }
  delete provisioningSnapshot_;
  provisioningSnapshot_ = snapshot;
  routingSelector_ = node.routingSelector;
  provisioner_.cancel();
  provisioningDeadlineMs_ = 0;
  provisioningIn_ = nullptr;
  provisioningOut_ = nullptr;
  provisioningLink_ = false;
  scanAfterProvision_ = true;
  haveTarget_ = false;
  state_.phase = ZhiyunLightState::Phase::Initializing;
  studio::ble::bleCentral().disconnect(linkHandle_, false);
  return true;
}

void ZhiyunLightClient::handleProvisioningBytes(const uint8_t* data, size_t length) {
  if (data == nullptr || length == 0 ||
      length > sizeof(provisioningBytes_) - provisioningLength_) {
    returnToOnboardingPicker("Provisioning data overflow");
    return;
  }
  std::memcpy(provisioningBytes_ + provisioningLength_, data, length);
  provisioningLength_ += length;
  while (provisioningLength_ >= 2) {
    if (provisioningBytes_[0] != 0x03) {
      std::memmove(provisioningBytes_, provisioningBytes_ + 1,
                   --provisioningLength_);
      continue;
    }
    size_t pduLength = 0;
    switch (provisioningBytes_[1]) {
      case 0x01: pduLength = 12; break;
      case 0x03: pduLength = 65; break;
      case 0x05:
      case 0x06: pduLength = 17; break;
      case 0x08: pduLength = 1; break;
      case 0x09: pduLength = 2; break;
      default: pduLength = 0; break;
    }
    if (pduLength == 0) {
      std::memmove(provisioningBytes_, provisioningBytes_ + 1,
                   --provisioningLength_);
      continue;
    }
    const size_t wrappedLength = pduLength + 1;
    if (provisioningLength_ < wrappedLength) return;
    const bool handled = provisioner_.handle(provisioningBytes_ + 1, pduLength);
    provisioningLength_ -= wrappedLength;
    std::memmove(provisioningBytes_, provisioningBytes_ + wrappedLength,
                 provisioningLength_);
    if (!handled || (provisioner_.complete() && !finishProvisioning())) {
      returnToOnboardingPicker("Provisioning failed");
      return;
    }
  }
}

void ZhiyunLightClient::handleFrame(const ParsedFrame& frame) {
  if (sharedTransport_ && state_.commandPending) {
    ZHIYUN_LIGHT_LOG.printf(
        "zhiyun_light event=command_frame instance=%lu response=%u "
        "sequence=%u command=0x%04x awaiting=%u expected_sequence=%u "
        "expected_command=0x%04x payload=",
        static_cast<unsigned long>(instanceId_), frame.response ? 1u : 0u,
        frame.sequence, frame.command, awaitingResponse_ ? 1u : 0u,
        expectedSequence_, expectedCommand_);
    for (size_t i = 0; i < frame.payloadLength; ++i) {
      ZHIYUN_LIGHT_LOG.printf("%02x", frame.payload[i]);
    }
    ZHIYUN_LIGHT_LOG.println();
  }
  if (!frame.response || !awaitingResponse_ ||
      frame.sequence != expectedSequence_ || frame.command != expectedCommand_) {
    return;
  }
  awaitingResponse_ = false;
  if (operation_ == Operation::Initialize) {
    if (sharedTransport_) {
      ZHIYUN_LIGHT_LOG.printf(
          "zhiyun_light event=init_response instance=%lu step=%u "
          "command=0x%04x sequence=%u selector=%u\n",
          static_cast<unsigned long>(instanceId_), step_, frame.command,
          frame.sequence, selector());
    }
    bool valid = true;
    if (step_ == 0) {
      valid = sharedTransport_
                  ? (identityContains(frame, "pl105") ||
                     identityContains(frame, "plx104"))
                  : identityContains(frame, identityMarker());
    }
    else if (frame.command == kCommandBrightness)
      valid = parseBrightness(frame, state_.brightness, selector());
    else if (frame.command == kCommandCct)
      valid = parseCct(frame, state_.kelvin, selector());
    else if (frame.command == kCommandPower)
      valid = parsePower(frame, state_.on, selector());
    if (!valid) {
      ZHIYUN_LIGHT_LOG.printf(
          "zhiyun_light event=unexpected_response instance=%lu step=%u "
          "command=0x%04x selector=%u payload=",
          static_cast<unsigned long>(instanceId_), step_, frame.command,
          selector());
      for (size_t i = 0; i < frame.payloadLength; ++i) {
        ZHIYUN_LIGHT_LOG.printf("%02x", frame.payload[i]);
      }
      ZHIYUN_LIGHT_LOG.println();
      if (onboardingSelectionActive_) {
        returnToOnboardingPicker("Unexpected Zhiyun response");
        return;
      }
      state_.phase = ZhiyunLightState::Phase::Failed;
      std::strncpy(state_.error, "Unexpected Zhiyun response",
                   sizeof(state_.error) - 1);
      state_.error[sizeof(state_.error) - 1] = '\0';
      markProtocolFailed();
      return;
    }
    ++step_;
    if (step_ == 7) finishInitialization();
    else if (!sendInitializationStep()) {
      if (onboardingSelectionActive_) {
        returnToOnboardingPicker("Initialization write failed");
        return;
      }
      state_.phase = ZhiyunLightState::Phase::Failed;
      std::strncpy(state_.error, "Initialization write failed",
                   sizeof(state_.error) - 1);
      state_.error[sizeof(state_.error) - 1] = '\0';
      markProtocolFailed();
    }
    return;
  }

  bool valid = false;
  if (operation_ == Operation::Power) {
    bool actual = false;
    const bool writeReply = state_.model == MolusModel::X60Rgb;
    valid = parsePower(frame, actual,
                       writeReply ? writeReplySelector() : selector(),
                       writeReply) &&
            actual == desiredPower_;
    if (valid) state_.on = actual;
  } else if (operation_ == Operation::Cct) {
    const bool writeReply = state_.model == MolusModel::X60Rgb;
    if (step_ == 0) {
      float actual = 0.0f;
      valid = parseBrightness(
                  frame, actual,
                  writeReply ? writeReplySelector() : selector(), writeReply) &&
              std::fabs(actual - desiredBrightness_) <= 0.05f;
      state_.verificationField = 1;
      state_.readbackBrightness = actual;
      if (valid) {
        state_.brightness = actual;
        if (actual > state_.maxBrightness) {
          state_.maxBrightness = static_cast<uint8_t>(actual + 0.5f);
        }
        step_ = 1;
        verificationAttempts_ = 0;
        state_.verificationField = 2;
        const uint16_t sequence = nextSequence();
        if (!writeFrame(buildCctWrite(sequence, desiredKelvin_, selector()))) {
          finishCommand(false, "CCT write failed");
        } else if (writeReply) {
          expectedSequence_ = sequence;
          expectedCommand_ = kCommandCct;
          awaitingResponse_ = true;
          responseDeadlineMs_ = millis() + 2000;
        } else {
          verifyAtMs_ = millis() + verificationDelayMs();
        }
        return;
      }
    } else {
      uint16_t actual = 0;
      valid = parseCct(frame, actual,
                       writeReply ? writeReplySelector() : selector(),
                       writeReply) &&
              actual == desiredKelvin_;
      state_.readbackKelvin = actual;
      if (valid) {
        state_.kelvin = actual;
        state_.mode = ZhiyunLightState::Mode::Cct;
      }
    }
    if (!valid && !writeReply && retryCctVerification()) return;
    if (!valid && step_ == 0 &&
        desiredBrightness_ > state_.readbackBrightness + 0.05f &&
        std::fabs(state_.readbackBrightness - state_.brightness) <= 0.05f) {
      // The fixture silently rejects brightness above the power source's
      // ceiling (for example, 60% with a 60 W USB-C supply). Seven stable
      // readbacks distinguish that limit from normal setter latency.
      state_.brightnessLimited = true;
      state_.maxBrightness =
          static_cast<uint8_t>(state_.readbackBrightness + 0.5f);
    }
  } else if (operation_ == Operation::Rgb) {
    if (step_ == 0) {
      float actual = 0.0f;
      valid = parseHue(frame, actual, writeReplySelector(), true) &&
              std::fabs(actual - desiredHue_) <= 0.05f;
      state_.verificationField = 3;
      state_.readbackHue = static_cast<uint16_t>(actual + 0.5f);
      if (valid) state_.hue = state_.readbackHue;
    } else if (step_ == 1) {
      float actual = 0.0f;
      valid = parseSaturation(frame, actual, writeReplySelector(), true) &&
              std::fabs(actual - desiredSaturation_) <= 0.05f;
      state_.verificationField = 4;
      state_.readbackSaturation = static_cast<uint8_t>(actual + 0.5f);
      if (valid) state_.saturation = state_.readbackSaturation;
    } else {
      float actual = 0.0f;
      valid = parseBrightness(frame, actual, writeReplySelector(), true) &&
              std::fabs(actual - desiredBrightness_) <= 0.05f;
      state_.verificationField = 1;
      state_.readbackBrightness = actual;
      if (valid) {
        state_.brightness = actual;
        state_.rgb = desiredRgb_;
        state_.mode = ZhiyunLightState::Mode::Rgb;
      }
    }
    if (valid && step_ < 2) {
      ++step_;
      if (!sendRgbStep()) finishCommand(false, "RGB write failed");
      return;
    }
  } else if (operation_ == Operation::Refresh) {
    if (step_ == 0) {
      valid = parseBrightness(frame, state_.brightness, selector());
    } else if (step_ == 1) {
      valid = parseCct(frame, state_.kelvin, selector());
    } else {
      valid = parsePower(frame, state_.on, selector());
    }
    if (valid && step_ < 2) {
      ++step_;
      if (!sendVerificationStep()) finishCommand(false, "Refresh failed");
      return;
    }
  }
  finishCommand(valid, valid ? nullptr : "State mismatch");
}

void ZhiyunLightClient::finishInitialization() {
  operation_ = Operation::None;
  if (sharedTransport_) {
    // The proprietary gateway can drop a member write issued in the same loop
    // turn as another member's final setup reply. Keep readiness false until
    // the shared characteristic has had a short quiet interval.
    state_.phase = ZhiyunLightState::Phase::ReadingState;
    initializationReadyAtMs_ = millis() + 300;
    return;
  }
  markInitializationReady();
}

void ZhiyunLightClient::markInitializationReady() {
  state_.phase = ZhiyunLightState::Phase::Ready;
  state_.confirmed = true;
  state_.hasSavedDevice = true;
  state_.lastCommandFailed = false;
  state_.error[0] = '\0';
  haveTarget_ = true;
  pairingChanged_ = true;
  delete provisioningSnapshot_;
  provisioningSnapshot_ = nullptr;
  onboardingSelectionActive_ = false;
  markProtocolReady();
}

bool ZhiyunLightClient::setPower(bool on) {
  if (!protocolReady() || state_.commandPending) return false;
  const uint16_t sequence = nextSequence();
  const FrameBytes frame = buildPowerWrite(sequence, on, selector());
  if (!writeFrame(frame)) return false;
  desiredPower_ = on;
  operation_ = Operation::Power;
  step_ = 0;
  state_.commandPending = true;
  state_.lastCommandFailed = false;
  state_.requestedKelvin = 0;
  state_.readbackKelvin = 0;
  state_.verificationField = 0;
  if (state_.model == MolusModel::X60Rgb) {
    expectedSequence_ = sequence;
    expectedCommand_ = kCommandPower;
    awaitingResponse_ = true;
    responseDeadlineMs_ = millis() + 2000;
  } else {
    verifyAtMs_ = millis() + verificationDelayMs();
  }
  return true;
}

bool ZhiyunLightClient::setCct(uint16_t kelvin, uint8_t brightness) {
  if (!protocolReady() || state_.commandPending || kelvin < kMinKelvin ||
      kelvin > kMaxKelvin || brightness > 100) return false;
  const uint16_t normalizedKelvin = normalizeCct(kelvin);
  // The X100 can acknowledge two immediate writes while dropping the first
  // state change. Apply and confirm brightness before sending CCT.
  const uint16_t sequence = nextSequence();
  if (!writeFrame(buildBrightnessWrite(sequence, brightness, selector())))
    return false;
  desiredBrightness_ = brightness;
  desiredKelvin_ = normalizedKelvin;
  state_.requestedBrightness = brightness;
  state_.readbackBrightness = 0.0f;
  state_.requestedKelvin = normalizedKelvin;
  state_.readbackKelvin = 0;
  state_.verificationField = 1;
  operation_ = Operation::Cct;
  step_ = 0;
  verificationAttempts_ = 0;
  state_.commandPending = true;
  state_.lastCommandFailed = false;
  if (state_.model == MolusModel::X60Rgb) {
    expectedSequence_ = sequence;
    expectedCommand_ = kCommandBrightness;
    awaitingResponse_ = true;
    responseDeadlineMs_ = millis() + 2000;
  } else {
    verifyAtMs_ = millis() + verificationDelayMs();
  }
  return true;
}

bool ZhiyunLightClient::setRgb(uint32_t rgb, uint8_t brightness) {
  if (!protocolReady() || state_.commandPending ||
      !supportsRgb(state_.model) || rgb > 0xffffff || brightness > 100)
    return false;
  desiredRgb_ = rgb;
  rgbToHsv(rgb, desiredHue_, desiredSaturation_);
  desiredBrightness_ = brightness;
  state_.requestedBrightness = brightness;
  state_.readbackBrightness = 0.0f;
  state_.requestedHue = desiredHue_;
  state_.readbackHue = 0;
  state_.requestedSaturation = desiredSaturation_;
  state_.readbackSaturation = 0;
  state_.verificationField = 1;
  operation_ = Operation::Rgb;
  step_ = 0;
  state_.commandPending = true;
  state_.lastCommandFailed = false;
  if (sendRgbStep()) return true;
  finishCommand(false, "RGB write failed");
  return false;
}

bool ZhiyunLightClient::refresh() {
  if (!protocolReady() || state_.commandPending) return false;
  operation_ = Operation::Refresh;
  step_ = 0;
  state_.commandPending = true;
  state_.lastCommandFailed = false;
  if (sendVerificationStep()) return true;
  finishCommand(false, "Refresh failed");
  return false;
}

bool ZhiyunLightClient::sendVerificationStep() {
  uint16_t command = kCommandPower;
  if (operation_ == Operation::Cct || operation_ == Operation::Refresh) {
    command = step_ == 0 ? kCommandBrightness
                        : (step_ == 1 ? kCommandCct : kCommandPower);
  }
  return sendQuery(command);
}

bool ZhiyunLightClient::sendRgbStep() {
  if (operation_ != Operation::Rgb || step_ > 2) return false;
  const uint16_t sequence = nextSequence();
  uint16_t command = kCommandHue;
  FrameBytes frame;
  if (step_ == 0) {
    state_.verificationField = 3;
    frame = buildHueWrite(sequence, desiredHue_, selector());
  } else if (step_ == 1) {
    state_.verificationField = 4;
    command = kCommandSaturation;
    frame = buildSaturationWrite(sequence, desiredSaturation_, selector());
  } else {
    state_.verificationField = 1;
    command = kCommandBrightness;
    frame = buildBrightnessWrite(sequence, desiredBrightness_, selector());
  }
  if (!writeFrame(frame)) return false;
  expectedSequence_ = sequence;
  expectedCommand_ = command;
  awaitingResponse_ = true;
  responseDeadlineMs_ = millis() + 2000;
  return true;
}

bool ZhiyunLightClient::retryCctVerification() {
  constexpr uint8_t kMaxRetries = 7;
  if (operation_ != Operation::Cct || verificationAttempts_ >= kMaxRetries)
    return false;
  ++verificationAttempts_;
  verifyAtMs_ = millis() + verificationDelayMs();
  return true;
}

void ZhiyunLightClient::finishCommand(bool success, const char* error) {
  awaitingResponse_ = false;
  verifyAtMs_ = 0;
  state_.commandPending = false;
  state_.lastCommandFailed = !success;
  verificationAttempts_ = 0;
  if (success) {
    state_.confirmed = true;
    state_.error[0] = '\0';
    state_.verificationField = 0;
  } else if (error != nullptr) {
    std::strncpy(state_.error, error, sizeof(state_.error) - 1);
    state_.error[sizeof(state_.error) - 1] = '\0';
  }
  operation_ = Operation::None;
}

void ZhiyunLightClient::startScan() {
  if (provisioningSnapshot_ != nullptr) {
    returnToOnboardingPicker();
  }
  begin();
  connectRequested_ = initialized_;
  haveTarget_ = false;
  state_.hasSavedDevice = false;
  if (linkHandle_ != studio::ble::kInvalidLinkHandle)
    studio::ble::bleCentral().disconnect(linkHandle_);
  beginScan();
}

void ZhiyunLightClient::forgetDevice() {
  haveTarget_ = false;
  targetAddress_[0] = '\0';
  targetName_[0] = '\0';
  state_.hasSavedDevice = false;
  state_.confirmed = false;
  pairingChanged_ = true;
  if (connectRequested_) startScan();
}

bool ZhiyunLightClient::cancelOnboarding() {
  discardProvisioningSnapshot();
  provisioner_.cancel();
  candidates_.clear();
  haveTarget_ = false;
  targetAddress_[0] = '\0';
  targetName_[0] = '\0';
  provisioningLink_ = false;
  pairingChanged_ = false;
  onboardingSelectionActive_ = false;
  connectRequested_ = false;
  if (!sharedTransport_ && linkHandle_ != studio::ble::kInvalidLinkHandle) {
    studio::ble::bleCentral().disconnect(linkHandle_, false);
  }
  return true;
}

bool ZhiyunLightClient::onboardingCandidate(
    size_t index, studio::OnboardingCandidate& candidate) const {
  const auto* entry = candidates_.at(index);
  if (entry == nullptr) return false;
  candidate = studio::OnboardingCandidate{};
  candidate.token = entry->token;
  candidate.addressType = entry->advertisement.address.type;
  candidate.rssi = entry->advertisement.rssi;
  std::strncpy(candidate.address, entry->advertisement.address.value,
               sizeof(candidate.address) - 1);
  studio::ble::advertisementName(entry->advertisement, candidate.name,
                                 sizeof(candidate.name));
  if (candidate.name[0] == '\0') {
    std::strncpy(candidate.name, "Zhiyun Light", sizeof(candidate.name) - 1);
  }
  return true;
}

bool ZhiyunLightClient::selectOnboardingCandidate(uint32_t token) {
  const auto* entry = candidates_.find(token);
  if (entry == nullptr || haveTarget_ || state_.hasSavedDevice) return false;
  if (!studio::ble::bleCentral().selectAdvertisement(linkHandle_,
                                                      entry->advertisement)) {
    state_.link = ZhiyunLightState::Link::Scanning;
    state_.phase = ZhiyunLightState::Phase::Idle;
    studio::ble::bleCentral().requestScan(linkHandle_, true);
    return false;
  }
  const MolusModel model = advertisementModel(entry->advertisement);
  state_.model = model;
  provisioningLink_ = matchesMolusAdvertisement(entry->advertisement, false);
  std::strncpy(targetAddress_, entry->advertisement.address.value,
               sizeof(targetAddress_) - 1);
  targetAddress_[sizeof(targetAddress_) - 1] = '\0';
  targetAddressType_ = entry->advertisement.address.type;
  studio::ble::advertisementName(entry->advertisement, targetName_,
                                 sizeof(targetName_));
  haveTarget_ = true;
  onboardingSelectionActive_ = true;
  state_.link = ZhiyunLightState::Link::Connecting;
  return true;
}

void ZhiyunLightClient::ignorePeerAddress(const char* address) {
  if (address == nullptr || address[0] == '\0') return;
  for (uint8_t i = 0; i < ignoredAddressCount_; ++i) {
    if (std::strcmp(ignoredAddresses_[i], address) == 0) return;
  }
  if (ignoredAddressCount_ >= CONFIG_BLE_MAX_SKIP_ADDRESSES) return;
  std::strncpy(ignoredAddresses_[ignoredAddressCount_], address,
               sizeof(ignoredAddresses_[0]) - 1);
  ignoredAddresses_[ignoredAddressCount_][sizeof(ignoredAddresses_[0]) - 1] =
      '\0';
  ++ignoredAddressCount_;
}

void ZhiyunLightClient::beginScan() {
  state_.link = ZhiyunLightState::Link::Scanning;
  state_.phase = ZhiyunLightState::Phase::Idle;
  studio::ble::bleCentral().requestScan(linkHandle_, true);
}

void ZhiyunLightClient::returnToOnboardingPicker(const char* error) {
  discardProvisioningSnapshot();
  provisioner_.cancel();
  candidates_.clear();
  haveTarget_ = false;
  targetAddress_[0] = '\0';
  targetName_[0] = '\0';
  provisioningLink_ = false;
  onboardingSelectionActive_ = false;
  routingSelector_ = 0;
  state_.model = MolusModel::Unknown;
  setupPending_ = false;
  provisioningDeadlineMs_ = 0;
  state_.link = ZhiyunLightState::Link::Scanning;
  state_.phase = ZhiyunLightState::Phase::Idle;
  if (error != nullptr) {
    std::strncpy(state_.error, error, sizeof(state_.error) - 1);
    state_.error[sizeof(state_.error) - 1] = '\0';
  }
  if (linkHandle_ != studio::ble::kInvalidLinkHandle) {
    studio::ble::bleCentral().disconnect(linkHandle_, false);
    studio::ble::bleCentral().requestScan(linkHandle_, true);
  }
}

void ZhiyunLightClient::discardProvisioningSnapshot() {
  // Provisioning Complete is irreversible from this controller: the fixture
  // has already committed the new keys. The newly persisted node must survive
  // later control-service failure, cancellation, or session teardown so a
  // retry can rediscover it instead of leaving an orphaned mesh member.
  delete provisioningSnapshot_;
  provisioningSnapshot_ = nullptr;
}

void ZhiyunLightClient::beginConnect() {
  studio::ble::Address address;
  std::strncpy(address.value, targetAddress_, sizeof(address.value) - 1);
  address.value[sizeof(address.value) - 1] = '\0';
  address.type = targetAddressType_;
  state_.link = ZhiyunLightState::Link::Connecting;
  studio::ble::bleCentral().requestConnect(linkHandle_, address);
}

void ZhiyunLightClient::handleDisconnect() {
  client_ = nullptr;
  writeCharacteristic_ = nullptr;
  notifyCharacteristic_ = nullptr;
  provisioningIn_ = nullptr;
  provisioningOut_ = nullptr;
  setupPending_ = false;
  awaitingResponse_ = false;
  verifyAtMs_ = 0;
  operation_ = Operation::None;
  scanner_.reset();
  provisioningLength_ = 0;
  provisioningDeadlineMs_ = 0;
  state_.link = ZhiyunLightState::Link::Disconnected;
  state_.phase = ZhiyunLightState::Phase::Idle;
  state_.commandPending = false;
  if (scanAfterProvision_ && connectRequested_) {
    scanAfterProvision_ = false;
    beginScan();
  }
}

bool ZhiyunLightClient::consumePairingUpdate(
    char* address, size_t addressCapacity, uint8_t& addressType, char* name,
    size_t nameCapacity, bool& paired) {
  if (!pairingChanged_) return false;
  pairingChanged_ = false;
  paired = haveTarget_;
  addressType = targetAddressType_;
  if (address != nullptr && addressCapacity > 0) {
    std::strncpy(address, targetAddress_, addressCapacity - 1);
    address[addressCapacity - 1] = '\0';
  }
  if (name != nullptr && nameCapacity > 0) {
    std::strncpy(name, targetName_, nameCapacity - 1);
    name[nameCapacity - 1] = '\0';
  }
  return true;
}

void ZhiyunLightClient::onBleAdvertisement(
    studio::ble::LinkHandle link,
    const studio::ble::Advertisement& advertisement) {
  if (link != linkHandle_) return;
  for (uint8_t i = 0; i < ignoredAddressCount_; ++i) {
    if (std::strcmp(ignoredAddresses_[i], advertisement.address.value) == 0)
      return;
  }
  const MolusModel advertisedModel = advertisementModel(advertisement);
  if (advertisedModel == MolusModel::Unknown ||
      (state_.model != MolusModel::Unknown &&
       state_.model != advertisedModel))
    return;
  const bool provisioned = matchesMolusAdvertisement(advertisement, true);
  const bool unprovisioned = matchesMolusAdvertisement(advertisement, false);
  if (!provisioned && !unprovisioned) return;
  if (onboardingSelectionActive_ && provisioningSnapshot_ != nullptr &&
      provisioned && !haveTarget_) {
    studio::ble::Address selectedAddress;
    std::strncpy(selectedAddress.value, targetAddress_,
                 sizeof(selectedAddress.value) - 1);
    selectedAddress.type = targetAddressType_;
    if (!matchesSelectedProvisionedAdvertisement(
            advertisement, selectedAddress,
            studio::mesh::repository().data().network.networkKey)) {
      return;
    }
    if (!studio::ble::bleCentral().selectAdvertisement(linkHandle_,
                                                        advertisement)) {
      returnToOnboardingPicker("Provisioned light unavailable");
      return;
    }
    std::strncpy(targetAddress_, advertisement.address.value,
                 sizeof(targetAddress_) - 1);
    targetAddress_[sizeof(targetAddress_) - 1] = '\0';
    targetAddressType_ = advertisement.address.type;
    studio::ble::advertisementName(advertisement, targetName_,
                                   sizeof(targetName_));
    haveTarget_ = true;
    state_.link = ZhiyunLightState::Link::Connecting;
  } else if (state_.hasSavedDevice && haveTarget_) {
    if (std::strcmp(targetAddress_, advertisement.address.value) == 0) {
      state_.link = ZhiyunLightState::Link::Connecting;
      studio::ble::bleCentral().selectAdvertisement(linkHandle_, advertisement);
    }
  } else if (!haveTarget_) {
    candidates_.observe(advertisement);
  }
}

void ZhiyunLightClient::onBleEvent(studio::ble::LinkHandle link,
                            const studio::ble::Event& event) {
  if (link != linkHandle_) return;
  if (event.type == studio::ble::EventType::Connected) {
    client_ = static_cast<NimBLEClient*>(
        studio::ble::bleCentral().nativeClient(linkHandle_));
    setupPending_ = true;
    setupAtMs_ = millis();
  } else if (event.type == studio::ble::EventType::ConnectFailed) {
    if (!state_.hasSavedDevice) returnToOnboardingPicker("Connect failed");
    else state_.link = ZhiyunLightState::Link::Disconnected;
  } else if (event.type == studio::ble::EventType::Disconnected) {
    if (onboardingSelectionActive_ && provisioningLink_ &&
        provisioningSnapshot_ == nullptr) {
      returnToOnboardingPicker("Provisioning disconnected");
    } else {
      handleDisconnect();
    }
  }
}

void ZhiyunLightClient::onNotifyBytes(const uint8_t* data, size_t length) {
  if (notifyStream_ == nullptr || data == nullptr || length == 0) return;
  xStreamBufferSend(static_cast<StreamBufferHandle_t>(notifyStream_), data,
                    length, 0);
}

void ZhiyunLightClient::drainNotifications() {
  if (notifyStream_ == nullptr) return;
  uint8_t bytes[128];
  size_t received = 0;
  while ((received = xStreamBufferReceive(
              static_cast<StreamBufferHandle_t>(notifyStream_), bytes,
              sizeof(bytes), 0)) > 0) {
    if (provisioningLink_)
      handleProvisioningBytes(bytes, received);
    else
      scanner_.feed(bytes, received,
                    [this](const ParsedFrame& frame) { handleFrame(frame); });
  }
}

}  // namespace zhiyun_light
