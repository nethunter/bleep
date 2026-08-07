#include "devices/zhiyun_x100/client.h"

#include <Arduino.h>
#include <NimBLEDevice.h>

#include <cmath>
#include <cstring>

#include "core/ble/ble_runtime.h"
#include "core/ble/ble_timing.h"
#include "devices/zhiyun_x100/ble_match.h"
#include "freertos/FreeRTOS.h"
#include "freertos/stream_buffer.h"

namespace zhiyun_x100 {
namespace {

const NimBLEUUID kControlService(kControlServiceUuid);
const NimBLEUUID kWriteCharacteristic(kWriteCharacteristicUuid);
const NimBLEUUID kNotifyCharacteristic(kNotifyCharacteristicUuid);
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

void X100Client::activate(const char* address, uint8_t addressType,
                          const char* name, bool paired) {
  begin();
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
  gNotifyClient = nullptr;
  setupPending_ = false;
  awaitingResponse_ = false;
  operation_ = Operation::None;
  scanner_.reset();
  state_.link = X100State::Link::Disconnected;
  state_.phase = X100State::Phase::Idle;
  state_.commandPending = false;
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
      if (valid) {
        state_.brightness = actual;
        step_ = 1;
        if (!sendVerificationStep()) finishCommand(false, "CCT readback failed");
        return;
      }
    } else {
      uint16_t actual = 0;
      valid = parseCct(frame, actual) && actual == desiredKelvin_;
      if (valid) state_.kelvin = actual;
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
  verifyAtMs_ = millis() + 250;
  return true;
}

bool X100Client::setCct(uint16_t kelvin, uint8_t brightness) {
  if (!protocolReady() || state_.commandPending || kelvin < kMinKelvin ||
      kelvin > kMaxKelvin || brightness > 100) return false;
  if (!writeFrame(buildBrightnessWrite(nextSequence(), brightness)) ||
      !writeFrame(buildCctWrite(nextSequence(), kelvin))) return false;
  desiredBrightness_ = brightness;
  desiredKelvin_ = kelvin;
  operation_ = Operation::Cct;
  step_ = 0;
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

void X100Client::finishCommand(bool success, const char* error) {
  awaitingResponse_ = false;
  verifyAtMs_ = 0;
  state_.commandPending = false;
  state_.lastCommandFailed = !success;
  if (success) {
    state_.confirmed = true;
    state_.error[0] = '\0';
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
  setupPending_ = false;
  awaitingResponse_ = false;
  verifyAtMs_ = 0;
  operation_ = Operation::None;
  scanner_.reset();
  state_.link = X100State::Link::Disconnected;
  state_.phase = X100State::Phase::Idle;
  state_.commandPending = false;
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
  if (link != linkHandle_ || !matchesAdvertisement(advertisement)) return;
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
    scanner_.feed(bytes, received,
                  [this](const ParsedFrame& frame) { handleFrame(frame); });
  }
}

}  // namespace zhiyun_x100
