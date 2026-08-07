#include "devices/zhiyun_x100/client.h"

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <esp_random.h>

#include <cmath>
#include <cstring>

#include "core/ble/ble_runtime.h"
#include "core/ble/ble_timing.h"
#include "core/preferences_store.h"
#include "core/mesh/mesh_repository.h"
#include "devices/zhiyun_x100/ble_match.h"
#include "freertos/FreeRTOS.h"
#include "freertos/stream_buffer.h"

namespace zhiyun_x100 {
namespace {

const NimBLEUUID kControlService(kControlServiceUuid);
const NimBLEUUID kWriteCharacteristic(kWriteCharacteristicUuid);
const NimBLEUUID kNotifyCharacteristic(kNotifyCharacteristicUuid);
const NimBLEUUID kProvisionService("00001827-0000-1000-8000-00805f9b34fb");
const NimBLEUUID kProvisionIn("00002adb-0000-1000-8000-00805f9b34fb");
const NimBLEUUID kProvisionOut("00002adc-0000-1000-8000-00805f9b34fb");
X100Client* gNotifyClient = nullptr;

void notifyTrampoline(NimBLERemoteCharacteristic*, uint8_t* data,
                      size_t length, bool) {
  if (gNotifyClient != nullptr) gNotifyClient->onNotifyBytes(data, length);
}

}  // namespace

void X100Client::begin() {
  if (initialized_) return;
  if (notifyStream_ == nullptr) notifyStream_ = xStreamBufferCreate(512, 1);
  studio::ble::ConnectPolicy policy;
  policy.connectTimeoutMs = 4000;
  policy.connectWatchdogMs = 7000;
  policy.security = studio::ble::SecurityPolicy::None;
  policy.diagnosticTag = "zhiyun_x100";
  linkHandle_ = studio::ble::bleCentral().acquire(*this, policy);
  initialized_ = linkHandle_ != studio::ble::kInvalidLinkHandle;
  gNotifyClient = this;
}

void X100Client::activate(studio::InstanceId instanceId, const char* address,
                          uint8_t addressType, const char* name, bool paired) {
  begin();
  instanceId_ = instanceId;
  connectRequested_ = initialized_;
  haveTarget_ = paired && address != nullptr && address[0] != '\0';
  targetAddress_[0] = '\0';
  targetName_[0] = '\0';
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
  std::strncpy(state_.deviceName, targetName_, sizeof(state_.deviceName) - 1);
  state_.deviceName[sizeof(state_.deviceName) - 1] = '\0';
  state_.error[0] = '\0';
  state_.lastCommandFailed = false;
  state_.brightnessLimited = false;
  state_.maxBrightness = 100;
  if (!connectRequested_) return;
  if (haveTarget_) beginConnect();
  else beginScan();
}

void X100Client::deactivate() {
  connectRequested_ = false;
  if (linkHandle_ != studio::ble::kInvalidLinkHandle)
    studio::ble::bleCentral().release(linkHandle_);
  linkHandle_ = studio::ble::kInvalidLinkHandle;
  initialized_ = false;
  client_ = nullptr;
  writeCharacteristic_ = nullptr;
  provisioningIn_ = nullptr;
  gNotifyClient = nullptr;
  setupPending_ = false;
  awaitingResponse_ = false;
  operation_ = Operation::None;
  provisioner_.cancel();
  provisioningDeadlineMs_ = 0;
  provisioningLink_ = false;
  scanAfterProvision_ = false;
  provisioningLength_ = 0;
  scanner_.reset();
  state_.link = X100State::Link::Disconnected;
  state_.phase = X100State::Phase::Idle;
  state_.commandPending = false;
  instanceId_ = studio::kInvalidInstanceId;
}

void X100Client::loop() {
  drainNotifications();
  if (!connectRequested_) return;
  const uint32_t now = millis();
  if (setupPending_ && static_cast<int32_t>(now - setupAtMs_) >= 0) {
    setupPending_ = false;
    if (client_ == nullptr || !client_->isConnected() || !completeConnect()) {
      state_.phase = X100State::Phase::Failed;
      std::strncpy(state_.error, "X100 setup failed",
                   sizeof(state_.error) - 1);
      state_.error[sizeof(state_.error) - 1] = '\0';
      studio::ble::bleCentral().markProtocolFailed(linkHandle_);
      return;
    }
  }
  if (state_.phase == X100State::Phase::Provisioning &&
      provisioningDeadlineMs_ != 0 &&
      static_cast<int32_t>(now - provisioningDeadlineMs_) >= 0) {
    provisioningDeadlineMs_ = 0;
    provisioner_.cancel();
    state_.phase = X100State::Phase::Failed;
    std::strncpy(state_.error, "Provisioning timeout",
                 sizeof(state_.error) - 1);
    state_.error[sizeof(state_.error) - 1] = '\0';
    studio::ble::bleCentral().markProtocolFailed(linkHandle_);
    return;
  }
  if (awaitingResponse_ &&
      static_cast<int32_t>(now - responseDeadlineMs_) >= 0) {
    if (operation_ == Operation::Initialize) {
      state_.phase = X100State::Phase::Failed;
      std::strncpy(state_.error, "Initialization timeout",
                   sizeof(state_.error) - 1);
      state_.error[sizeof(state_.error) - 1] = '\0';
      studio::ble::bleCentral().markProtocolFailed(linkHandle_);
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

bool X100Client::protocolReady() const {
  return linkHandle_ != studio::ble::kInvalidLinkHandle &&
         studio::ble::bleCentral().protocolReady(linkHandle_) &&
         state_.phase == X100State::Phase::Ready;
}

uint16_t X100Client::nextSequence() {
  const uint16_t result = sequence_;
  ++sequence_;
  if (sequence_ == 0) sequence_ = 1;
  return result;
}

bool X100Client::writeFrame(const FrameBytes& frame) {
  return writeCharacteristic_ != nullptr && frame.length > 0 &&
         writeCharacteristic_->writeValue(frame.bytes, frame.length, false);
}

bool X100Client::sendQuery(uint16_t command, const uint8_t* payload,
                           size_t payloadLength) {
  const uint16_t sequence = nextSequence();
  const FrameBytes frame = payload == nullptr
                               ? buildReadRequest(sequence, command)
                               : buildRequest(sequence, command, payload,
                                              payloadLength);
  if (!writeFrame(frame)) return false;
  expectedSequence_ = sequence;
  expectedCommand_ = command;
  awaitingResponse_ = true;
  responseDeadlineMs_ = millis() + 2000;
  return true;
}

bool X100Client::sendInitializationStep() {
  static constexpr uint16_t commands[] = {
      kCommandIdentity, kCommandFirmware, kCommandStatus, kCommandMode,
      kCommandBrightness, kCommandCct, kCommandPower,
  };
  if (step_ >= sizeof(commands) / sizeof(commands[0])) return false;
  if (step_ <= 2) {
    const uint16_t sequence = nextSequence();
    const FrameBytes frame = buildRequest(sequence, commands[step_], nullptr, 0);
    if (!writeFrame(frame)) return false;
    expectedSequence_ = sequence;
    expectedCommand_ = commands[step_];
    awaitingResponse_ = true;
    responseDeadlineMs_ = millis() + 2000;
    return true;
  }
  if (step_ == 3) {
    const uint8_t modePayload[] = {0x00, 0x80, 0x00, 0x00};
    return sendQuery(kCommandMode, modePayload, sizeof(modePayload));
  }
  return sendQuery(commands[step_]);
}

bool X100Client::completeConnect() {
  if (provisioningLink_) return setupProvisioning();
  const uint32_t started = millis();
  NimBLERemoteService* service = client_->getService(kControlService);
  if (service == nullptr) return false;
  writeCharacteristic_ = service->getCharacteristic(kWriteCharacteristic);
  NimBLERemoteCharacteristic* notify =
      service->getCharacteristic(kNotifyCharacteristic);
  if (writeCharacteristic_ == nullptr || notify == nullptr ||
      !notify->subscribe(true, notifyTrampoline, true)) {
    writeCharacteristic_ = nullptr;
    return false;
  }
  scanner_.reset();
  sequence_ = 2;
  step_ = 0;
  operation_ = Operation::Initialize;
  state_.link = X100State::Link::Connected;
  state_.phase = X100State::Phase::Initializing;
  studio::ble::logTiming(
      "zhiyun_x100", linkHandle_, "gatt_setup", millis() - started,
      millis() - studio::ble::bleCentral().timingStartedAt(linkHandle_), "ok");
  return sendInitializationStep();
}

bool X100Client::loadMesh() {
  return studio::mesh::repository().begin();
}

bool X100Client::setupProvisioning() {
  NimBLERemoteService* service = client_->getService(kProvisionService);
  if (service == nullptr) return false;
  provisioningIn_ = service->getCharacteristic(kProvisionIn);
  NimBLERemoteCharacteristic* out = service->getCharacteristic(kProvisionOut);
  if (provisioningIn_ == nullptr || out == nullptr ||
      !out->subscribe(true, notifyTrampoline, true) || !loadMesh()) {
    provisioningIn_ = nullptr;
    return false;
  }
  provisioningLength_ = 0;
  state_.link = X100State::Link::Connected;
  state_.phase = X100State::Phase::Provisioning;
  provisioningDeadlineMs_ = millis() + 30000;
  studio::mesh::StoreData& meshData = studio::mesh::repository().data();
  return provisioner_.begin(meshData.network.networkKey,
                            meshData.network.ivIndex,
                            meshData.network.nextUnicastAddress, *this);
}

bool X100Client::sendProvisioningPdu(const uint8_t* pdu, size_t length) {
  if (provisioningIn_ == nullptr || pdu == nullptr || length + 1 > 80)
    return false;
  uint8_t wrapped[80] = {0x03};
  std::memcpy(wrapped + 1, pdu, length);
  return provisioningIn_->writeValue(wrapped, length + 1, false);
}

bool X100Client::finishProvisioning() {
  if (!provisioner_.complete() || instanceId_ == studio::kInvalidInstanceId)
    return false;
  studio::mesh::StoreData& meshData = studio::mesh::repository().data();
  const studio::mesh::StoreData previous = meshData;
  studio::mesh::NodeRecord node;
  node.instanceId = instanceId_;
  node.model = studio::DriverId::ZhiyunX100;
  node.unicastAddress = meshData.network.nextUnicastAddress;
  node.elementCount = provisioner_.elementCount();
  node.configured = true;
  std::memcpy(node.deviceKey, provisioner_.deviceKey(), sizeof(node.deviceKey));
  std::strncpy(node.bleAddress, targetAddress_, sizeof(node.bleAddress) - 1);
  node.bleAddress[sizeof(node.bleAddress) - 1] = '\0';
  node.bleAddressType = targetAddressType_;
  if (node.unicastAddress > 0x7fff - node.elementCount) return false;
  if (!studio::mesh::upsertNode(meshData, node)) return false;
  meshData.network.nextUnicastAddress =
      static_cast<uint16_t>(node.unicastAddress + node.elementCount);
  if (!studio::mesh::repository().save()) {
    meshData = previous;
    return false;
  }
  provisioner_.cancel();
  provisioningDeadlineMs_ = 0;
  provisioningIn_ = nullptr;
  provisioningLink_ = false;
  scanAfterProvision_ = true;
  haveTarget_ = false;
  state_.phase = X100State::Phase::Initializing;
  studio::ble::bleCentral().disconnect(linkHandle_, false);
  return true;
}

void X100Client::handleProvisioningBytes(const uint8_t* data, size_t length) {
  if (data == nullptr || length == 0 ||
      length > sizeof(provisioningBytes_) - provisioningLength_) {
    provisioner_.cancel();
    state_.phase = X100State::Phase::Failed;
    std::strncpy(state_.error, "Provisioning data overflow",
                 sizeof(state_.error) - 1);
    state_.error[sizeof(state_.error) - 1] = '\0';
    studio::ble::bleCentral().markProtocolFailed(linkHandle_);
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
      state_.phase = X100State::Phase::Failed;
      std::strncpy(state_.error, "Provisioning failed",
                   sizeof(state_.error) - 1);
      state_.error[sizeof(state_.error) - 1] = '\0';
      studio::ble::bleCentral().markProtocolFailed(linkHandle_);
      return;
    }
  }
}

void X100Client::handleFrame(const ParsedFrame& frame) {
  if (!frame.response || !awaitingResponse_ ||
      frame.sequence != expectedSequence_ || frame.command != expectedCommand_) {
    return;
  }
  awaitingResponse_ = false;
  if (operation_ == Operation::Initialize) {
    bool valid = true;
    if (step_ == 0) valid = identityIsX100(frame);
    else if (step_ == 4) valid = parseBrightness(frame, state_.brightness);
    else if (step_ == 5) valid = parseCct(frame, state_.kelvin);
    else if (step_ == 6) valid = parsePower(frame, state_.on);
    if (!valid) {
      state_.phase = X100State::Phase::Failed;
      std::strncpy(state_.error, "Unexpected X100 response",
                   sizeof(state_.error) - 1);
      state_.error[sizeof(state_.error) - 1] = '\0';
      studio::ble::bleCentral().markProtocolFailed(linkHandle_);
      return;
    }
    ++step_;
    if (step_ == 7) finishInitialization();
    else if (!sendInitializationStep()) {
      state_.phase = X100State::Phase::Failed;
      std::strncpy(state_.error, "Initialization write failed",
                   sizeof(state_.error) - 1);
      state_.error[sizeof(state_.error) - 1] = '\0';
      studio::ble::bleCentral().markProtocolFailed(linkHandle_);
    }
    return;
  }

  bool valid = false;
  if (operation_ == Operation::Power) {
    bool actual = false;
    valid = parsePower(frame, actual) && actual == desiredPower_;
    if (valid) state_.on = actual;
  } else if (operation_ == Operation::Cct) {
    if (step_ == 0) {
      float actual = 0.0f;
      valid = parseBrightness(frame, actual) &&
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
        if (!writeFrame(buildCctWrite(nextSequence(), desiredKelvin_))) {
          finishCommand(false, "CCT write failed");
        } else {
          verifyAtMs_ = millis() + 250;
        }
        return;
      }
    } else {
      uint16_t actual = 0;
      valid = parseCct(frame, actual) && actual == desiredKelvin_;
      state_.readbackKelvin = actual;
      if (valid) state_.kelvin = actual;
    }
    if (!valid && retryCctVerification()) return;
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
  } else if (operation_ == Operation::Refresh) {
    if (step_ == 0) {
      valid = parseBrightness(frame, state_.brightness);
    } else if (step_ == 1) {
      valid = parseCct(frame, state_.kelvin);
    } else {
      valid = parsePower(frame, state_.on);
    }
    if (valid && step_ < 2) {
      ++step_;
      if (!sendVerificationStep()) finishCommand(false, "Refresh failed");
      return;
    }
  }
  finishCommand(valid, valid ? nullptr : "State mismatch");
}

void X100Client::finishInitialization() {
  state_.phase = X100State::Phase::Ready;
  state_.confirmed = true;
  state_.hasSavedDevice = true;
  state_.lastCommandFailed = false;
  state_.error[0] = '\0';
  haveTarget_ = true;
  pairingChanged_ = true;
  operation_ = Operation::None;
  studio::ble::bleCentral().markProtocolReady(linkHandle_);
}

bool X100Client::setPower(bool on) {
  if (!protocolReady() || state_.commandPending) return false;
  const FrameBytes frame = buildPowerWrite(nextSequence(), on);
  if (!writeFrame(frame)) return false;
  desiredPower_ = on;
  operation_ = Operation::Power;
  step_ = 0;
  state_.commandPending = true;
  state_.lastCommandFailed = false;
  state_.requestedKelvin = 0;
  state_.readbackKelvin = 0;
  state_.verificationField = 0;
  verifyAtMs_ = millis() + 250;
  return true;
}

bool X100Client::setCct(uint16_t kelvin, uint8_t brightness) {
  if (!protocolReady() || state_.commandPending || kelvin < kMinKelvin ||
      kelvin > kMaxKelvin || brightness > 100) return false;
  const uint16_t normalizedKelvin = normalizeCct(kelvin);
  // The X100 can acknowledge two immediate writes while dropping the first
  // state change. Apply and confirm brightness before sending CCT.
  if (!writeFrame(buildBrightnessWrite(nextSequence(), brightness)))
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
  verifyAtMs_ = millis() + 250;
  return true;
}

bool X100Client::refresh() {
  if (!protocolReady() || state_.commandPending) return false;
  operation_ = Operation::Refresh;
  step_ = 0;
  state_.commandPending = true;
  state_.lastCommandFailed = false;
  if (sendVerificationStep()) return true;
  finishCommand(false, "Refresh failed");
  return false;
}

bool X100Client::sendVerificationStep() {
  uint16_t command = kCommandPower;
  if (operation_ == Operation::Cct || operation_ == Operation::Refresh) {
    command = step_ == 0 ? kCommandBrightness
                        : (step_ == 1 ? kCommandCct : kCommandPower);
  }
  return sendQuery(command);
}

bool X100Client::retryCctVerification() {
  constexpr uint8_t kMaxRetries = 7;
  if (operation_ != Operation::Cct || verificationAttempts_ >= kMaxRetries)
    return false;
  ++verificationAttempts_;
  verifyAtMs_ = millis() + 250;
  return true;
}

void X100Client::finishCommand(bool success, const char* error) {
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

void X100Client::startScan() {
  begin();
  connectRequested_ = initialized_;
  haveTarget_ = false;
  state_.hasSavedDevice = false;
  if (linkHandle_ != studio::ble::kInvalidLinkHandle)
    studio::ble::bleCentral().disconnect(linkHandle_);
  beginScan();
}

void X100Client::forgetDevice() {
  haveTarget_ = false;
  targetAddress_[0] = '\0';
  targetName_[0] = '\0';
  state_.hasSavedDevice = false;
  state_.confirmed = false;
  pairingChanged_ = true;
  if (connectRequested_) startScan();
}

void X100Client::beginScan() {
  state_.link = X100State::Link::Scanning;
  state_.phase = X100State::Phase::Idle;
  studio::ble::bleCentral().requestScan(linkHandle_, true);
}

void X100Client::beginConnect() {
  studio::ble::Address address;
  std::strncpy(address.value, targetAddress_, sizeof(address.value) - 1);
  address.value[sizeof(address.value) - 1] = '\0';
  address.type = targetAddressType_;
  state_.link = X100State::Link::Connecting;
  studio::ble::bleCentral().requestConnect(linkHandle_, address);
}

void X100Client::handleDisconnect() {
  client_ = nullptr;
  writeCharacteristic_ = nullptr;
  provisioningIn_ = nullptr;
  setupPending_ = false;
  awaitingResponse_ = false;
  verifyAtMs_ = 0;
  operation_ = Operation::None;
  scanner_.reset();
  provisioningLength_ = 0;
  provisioningDeadlineMs_ = 0;
  state_.link = X100State::Link::Disconnected;
  state_.phase = X100State::Phase::Idle;
  state_.commandPending = false;
  if (scanAfterProvision_ && connectRequested_) {
    scanAfterProvision_ = false;
    beginScan();
  }
}

bool X100Client::consumePairingUpdate(
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

void X100Client::onBleAdvertisement(
    studio::ble::LinkHandle link,
    const studio::ble::Advertisement& advertisement) {
  if (link != linkHandle_) return;
  const bool provisioned = matchesAdvertisement(advertisement);
  const bool unprovisioned = matchesUnprovisionedAdvertisement(advertisement);
  if (!provisioned && !unprovisioned) return;
  provisioningLink_ = unprovisioned;
  std::strncpy(targetAddress_, advertisement.address.value,
               sizeof(targetAddress_) - 1);
  targetAddress_[sizeof(targetAddress_) - 1] = '\0';
  targetAddressType_ = advertisement.address.type;
  studio::ble::advertisementName(advertisement, targetName_,
                                 sizeof(targetName_));
  haveTarget_ = true;
  state_.link = X100State::Link::Connecting;
  studio::ble::bleCentral().selectAdvertisement(linkHandle_, advertisement);
}

void X100Client::onBleEvent(studio::ble::LinkHandle link,
                            const studio::ble::Event& event) {
  if (link != linkHandle_) return;
  if (event.type == studio::ble::EventType::Connected) {
    client_ = static_cast<NimBLEClient*>(
        studio::ble::bleCentral().nativeClient(linkHandle_));
    setupPending_ = true;
    setupAtMs_ = millis();
  } else if (event.type == studio::ble::EventType::ConnectFailed) {
    state_.link = X100State::Link::Disconnected;
  } else if (event.type == studio::ble::EventType::Disconnected) {
    handleDisconnect();
  }
}

void X100Client::onNotifyBytes(const uint8_t* data, size_t length) {
  if (notifyStream_ == nullptr || data == nullptr || length == 0) return;
  xStreamBufferSend(static_cast<StreamBufferHandle_t>(notifyStream_), data,
                    length, 0);
}

void X100Client::drainNotifications() {
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

}  // namespace zhiyun_x100
