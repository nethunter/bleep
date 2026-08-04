#include "devices/shark_nano_ii/client.h"

#include <Arduino.h>
#include <NimBLEDevice.h>

#include <cstring>
#include <string>

#include "core/ble/ble_runtime.h"
#include "core/ble/ble_timing.h"
#include "devices/shark_nano_ii/ble_match.h"
#include "freertos/FreeRTOS.h"
#include "freertos/stream_buffer.h"

namespace shark {

namespace {

const NimBLEUUID kServiceUuid("fff0");
const NimBLEUUID kWriteUuid("fff2");
const NimBLEUUID kNotifyUuid("fff1");
SharkClient* gNotifyClient = nullptr;

void notifyTrampoline(NimBLERemoteCharacteristic*, uint8_t* data, size_t length, bool) {
  if (gNotifyClient != nullptr) {
    gNotifyClient->onNotifyBytes(data, length);
  }
}

}  // namespace

void SharkClient::begin() {
  if (initialized_) {
    return;
  }
  if (notifyStream_ == nullptr) {
    notifyStream_ = xStreamBufferCreate(1024, 1);
  }
  studio::ble::ConnectPolicy policy;
  policy.connectTimeoutMs = 3000;
  policy.connectWatchdogMs = 3000;
  policy.diagnosticTag = "shark_nano_ii";
  linkHandle_ = studio::ble::bleCentral().acquire(*this, policy);
  initialized_ = linkHandle_ != studio::ble::kInvalidLinkHandle;
  gNotifyClient = this;
}

void SharkClient::activate(const char* address, uint8_t addressType, const char* name,
                           bool paired) {
  begin();
  connectRequested_ = true;
  haveTarget_ = paired && address != nullptr && address[0] != '\0';
  targetAddr_[0] = '\0';
  targetName_[0] = '\0';
  targetAddrType_ = addressType;
  if (haveTarget_) {
    strncpy(targetAddr_, address, sizeof(targetAddr_) - 1);
    targetAddr_[sizeof(targetAddr_) - 1] = '\0';
  }
  if (name != nullptr) {
    strncpy(targetName_, name, sizeof(targetName_) - 1);
    targetName_[sizeof(targetName_) - 1] = '\0';
  }
  state_.hasSavedDevice = haveTarget_;
  strncpy(state_.deviceName, targetName_, sizeof(state_.deviceName) - 1);
  state_.deviceName[sizeof(state_.deviceName) - 1] = '\0';
  if (haveTarget_) {
    beginConnect();
  } else {
    beginScan();
  }
}

void SharkClient::deactivate() {
  connectRequested_ = false;
  stopMotion();
  studio::ble::bleCentral().release(linkHandle_);
  linkHandle_ = studio::ble::kInvalidLinkHandle;
  initialized_ = false;
  client_ = nullptr;
  gNotifyClient = nullptr;
  setupPending_ = false;
  resetDeviceState();
  state_.link = Link::Disconnected;
}

void SharkClient::loop() {
  drainNotifications();

  if (!connectRequested_) {
    return;
  }

  if (setupPending_) {
    setupPending_ = false;
    if (client_ == nullptr || !client_->isConnected()) {
      studio::ble::bleCentral().markProtocolFailed(linkHandle_);
      return;
    }
    completeConnect();
  }

  const uint32_t now = millis();

  if (trackingPending_ && now > trackingPendingExpiryMs_) {
    trackingPending_ = false;
  }
}

void SharkClient::startScan() {
  // Manual (re)pairing: drop any current link and scan fresh.
  begin();
  connectRequested_ = true;
  teardownConnection();
  resetDeviceState();
  beginScan();
}

void SharkClient::disconnectLink() {
  studio::ble::bleCentral().disconnect(linkHandle_, true, 1500);
  resetDeviceState();
  state_.link = Link::Disconnected;
}

void SharkClient::forgetDevice() {
  haveTarget_ = false;
  targetAddr_[0] = '\0';
  targetName_[0] = '\0';
  state_.hasSavedDevice = false;
  pairingChanged_ = true;
  startScan();
}

bool SharkClient::consumePairingUpdate(char* address, size_t addressCapacity,
                                       uint8_t& addressType, char* name,
                                       size_t nameCapacity, bool& paired) {
  if (!pairingChanged_) {
    return false;
  }
  pairingChanged_ = false;
  paired = haveTarget_;
  addressType = targetAddrType_;
  if (address != nullptr && addressCapacity > 0) {
    strncpy(address, targetAddr_, addressCapacity - 1);
    address[addressCapacity - 1] = '\0';
  }
  if (name != nullptr && nameCapacity > 0) {
    strncpy(name, targetName_, nameCapacity - 1);
    name[nameCapacity - 1] = '\0';
  }
  return true;
}

void SharkClient::beginScan() {
  studio::ble::bleCentral().requestScan(linkHandle_, !haveTarget_);
  state_.link = Link::Scanning;
}

void SharkClient::beginConnect() {
  state_.link = Link::Connecting;
  studio::ble::Address address;
  std::strncpy(address.value, targetAddr_, sizeof(address.value) - 1);
  address.type = targetAddrType_;
  studio::ble::bleCentral().requestConnect(linkHandle_, address);
}

void SharkClient::completeConnect() {
  const uint32_t discoveryStartedMs = millis();
  NimBLERemoteService* service = client_->getService(kServiceUuid);
  if (service == nullptr) {
    state_.link = Link::Disconnected;
    studio::ble::bleCentral().markProtocolFailed(linkHandle_);
    studio::ble::logTiming(
        "shark_nano_ii", linkHandle_, "gatt_setup",
        millis() - discoveryStartedMs,
        millis() - studio::ble::bleCentral().timingStartedAt(linkHandle_),
        "failed");
    return;
  }

  writeChar_ = service->getCharacteristic(kWriteUuid);
  NimBLERemoteCharacteristic* notifyChar = service->getCharacteristic(kNotifyUuid);
  if (writeChar_ == nullptr || notifyChar == nullptr) {
    writeChar_ = nullptr;
    state_.link = Link::Disconnected;
    studio::ble::bleCentral().markProtocolFailed(linkHandle_);
    return;
  }

  scanner_.reset();
  if (!notifyChar->subscribe(true, notifyTrampoline, true)) {
    writeChar_ = nullptr;
    state_.link = Link::Disconnected;
    studio::ble::bleCentral().markProtocolFailed(linkHandle_);
    studio::ble::logTiming(
        "shark_nano_ii", linkHandle_, "gatt_setup",
        millis() - discoveryStartedMs,
        millis() - studio::ble::bleCentral().timingStartedAt(linkHandle_),
        "failed");
    return;
  }

  resetDeviceState();
  strncpy(state_.deviceName, targetName_, sizeof(state_.deviceName) - 1);
  state_.deviceName[sizeof(state_.deviceName) - 1] = '\0';
  state_.link = Link::Connected;
  if (!sendHandshake()) {
    state_.link = Link::Disconnected;
    studio::ble::bleCentral().markProtocolFailed(linkHandle_);
    studio::ble::logTiming(
        "shark_nano_ii", linkHandle_, "gatt_setup",
        millis() - discoveryStartedMs,
        millis() - studio::ble::bleCentral().timingStartedAt(linkHandle_),
        "failed");
    return;
  }
  state_.hasSavedDevice = true;
  pairingChanged_ = true;
  studio::ble::bleCentral().markProtocolReady(linkHandle_);
  studio::ble::logTiming(
      "shark_nano_ii", linkHandle_, "gatt_setup",
      millis() - discoveryStartedMs,
      millis() - studio::ble::bleCentral().timingStartedAt(linkHandle_), "ok");
}

void SharkClient::teardownConnection() {
  studio::ble::bleCentral().disconnect(linkHandle_);
  writeChar_ = nullptr;
}

void SharkClient::handleDisconnect() {
  writeChar_ = nullptr;
  setupPending_ = false;
  resetDeviceState();
  state_.link = Link::Disconnected;
}

bool SharkClient::sendHandshake() {
  // Replay the connection handshake documented in protocol.md so the slider
  // begins pushing state, then request the initial snapshots.
  const uint8_t initData[1] = {0x02};
  return writeFrameRaw(encodeFrame(0x06, 0x18, initData, 1)) &&
         writeFrameRaw(buildControlPing(0x15, nextTx())) &&
         writeFrameRaw(buildControlPing(0x00, nextTx())) &&
         writeFrameRaw(buildControlPing(0x03, nextTx())) &&
         writeFrameRaw(buildTimingQuery(nextTx()));
}

void SharkClient::refreshAll() {
  if (!connected()) {
    return;
  }
  sendFrame(buildControlPing(0x00, nextTx()));  // full status (battery)
  sendFrame(buildControlPing(0x03, nextTx()));  // point-state (presence)
  sendFrame(buildTimingQuery(nextTx()));         // timing table (speed/hold)
}

bool SharkClient::sendFrame(const FrameBytes& frame, bool response) {
  if (!connected()) {
    return false;
  }
  return writeFrameRaw(frame, response);
}

bool SharkClient::writeFrameRaw(const FrameBytes& frame, bool response) {
  if (writeChar_ == nullptr || frame.len == 0) {
    return false;
  }
  return writeChar_->writeValue(frame.bytes, frame.len, response);
}

void SharkClient::drainNotifications() {
  if (notifyStream_ == nullptr) {
    return;
  }
  StreamBufferHandle_t stream = static_cast<StreamBufferHandle_t>(notifyStream_);
  uint8_t buf[256];
  size_t got = 0;
  while ((got = xStreamBufferReceive(stream, buf, sizeof(buf), 0)) > 0) {
    scanner_.feed(buf, got, [this](const ParsedFrame& frame) { applyFrame(frame); });
  }
}

void SharkClient::applyFrame(const ParsedFrame& frame) {
  reduceFrame(state_, frame);

  if (frame.code == 0x08 && frame.dataLen == kTimingDataLen) {
    applyTimingTable(frame);
  }

  if (frame.family == 0x03 && frame.code == 0x02 && frame.kind == 0x0003 && frame.dataLen >= 3) {
    if (trackingPending_ && millis() <= trackingPendingExpiryMs_ &&
        frame.data[0] == trackingPendingTx_) {
      trackingPending_ = false;
      state_.tracking = trackingPendingValue_;
      state_.trackingKnown = true;
    }
  }
}

void SharkClient::applyTimingTable(const ParsedFrame& frame) {
  memcpy(timingTable_, frame.data, kTimingDataLen);
  haveTable_ = true;

  if (timingPending_ && timingPendingSlot_ > 0 && timingPendingSlot_ < kKeypointCount) {
    FrameBytes out;
    if (patchTimingTable(timingTable_, kTimingDataLen, timingPendingSlot_, timingPendingSpeed_,
                         timingPendingHold_, nextTx(), out)) {
      sendFrame(out);
    }
  }
  timingPending_ = false;
  timingPendingSlot_ = -1;
  timingPendingSpeed_ = -1;
  timingPendingHold_ = -1;
}

void SharkClient::keypointSet(int slot) {
  if (!connected() || slot < 0 || slot >= kKeypointCount) {
    return;
  }
  sendFrame(buildKeypointAction(slot, kMarkerSet, nextTx()));
  sendFrame(buildControlPing(0x05, nextTx()));
  sendFrame(buildControlPing(0x03, nextTx()));
  state_.present[slot] = true;
  state_.presenceKnown = true;
}

void SharkClient::keypointGo(int slot) {
  if (!connected() || slot < 0 || slot >= kKeypointCount) {
    return;
  }
  sendFrame(buildKeypointAction(slot, kMarkerGo, nextTx()));
}

void SharkClient::keypointDelete(int slot) {
  if (!connected() || slot < 0 || slot >= kKeypointCount) {
    return;
  }
  sendFrame(buildKeypointDelete(slot, state_.present, nextTx()));
  sendFrame(buildControlPing(0x05, nextTx()));
  sendFrame(buildControlPing(0x03, nextTx()));
  // Optimistic cascade: clears the target and any later configured slots.
  for (int i = slot; i < kKeypointCount; ++i) {
    if (i == slot || state_.present[i]) {
      state_.present[i] = false;
    }
  }
}

void SharkClient::editTiming(int slot, int speed, int holdSeconds) {
  if (!connected() || slot <= 0 || slot >= kKeypointCount) {
    return;  // A (slot 0) has no inbound travel segment.
  }
  if (haveTable_) {
    FrameBytes out;
    if (patchTimingTable(timingTable_, kTimingDataLen, slot, speed, holdSeconds, nextTx(), out)) {
      sendFrame(out);
      if (speed >= 0) {
        state_.speed[slot] = speed;
      }
      if (holdSeconds >= 0) {
        state_.hold[slot] = holdSeconds;
      }
    }
    return;
  }

  // No table yet: stash the edit and request the table; applied on arrival.
  timingPending_ = true;
  timingPendingSlot_ = slot;
  if (speed >= 0) {
    timingPendingSpeed_ = speed;
  }
  if (holdSeconds >= 0) {
    timingPendingHold_ = holdSeconds;
  }
  requestTiming();
}

void SharkClient::setSpeed(int slot, int percent) {
  if (percent < 0) {
    percent = 0;
  } else if (percent > 100) {
    percent = 100;
  }
  editTiming(slot, percent, -1);
}

void SharkClient::setHold(int slot, int seconds) {
  if (seconds < 0) {
    seconds = 0;
  } else if (seconds > 255) {
    seconds = 255;
  }
  editTiming(slot, -1, seconds);
}

void SharkClient::requestTiming() {
  if (!connected()) {
    return;
  }
  sendFrame(buildTimingQuery(nextTx()));
}

void SharkClient::setRunState(uint8_t runState) {
  if (!connected()) {
    return;
  }
  sendFrame(buildRunState(runState, nextTx()));
  const char* text = "idle";
  if (runState == kRunStart) {
    text = "running";
  } else if (runState == kRunStandby) {
    text = "standby";
  } else if (runState == kRunStop) {
    text = "stopped";
  }
  state_.runStateCode = runState;
  // A fresh command clears any stale/frozen progress; live notifications
  // repopulate it once a new run actually starts moving.
  state_.runProgressKnown = false;
  state_.runPercent = 0.0f;
  strncpy(state_.runText, text, sizeof(state_.runText) - 1);
  state_.runText[sizeof(state_.runText) - 1] = '\0';
}

void SharkClient::setLoop(bool on) {
  if (!connected()) {
    return;
  }
  sendFrame(buildLoop(on, nextTx()));
  state_.loopOn = on;
}

void SharkClient::setDirection(bool reverse) {
  if (!connected()) {
    return;
  }
  sendFrame(buildDirection(reverse, nextTx()));
  state_.reverse = reverse;
}

void SharkClient::setManualTracking(bool enabled) {
  if (!connected()) {
    return;
  }
  const uint8_t tx = nextTx();
  sendFrame(buildManualTracking(enabled, tx));
  trackingPending_ = true;
  trackingPendingTx_ = tx;
  trackingPendingValue_ = enabled;
  trackingPendingExpiryMs_ = millis() + 5000;
  state_.tracking = enabled;
  state_.trackingKnown = true;
}

void SharkClient::setMotionVector(int slideVelocity, int panVelocity) {
  if (!connected()) {
    return;
  }
  sendFrame(buildMotionVector(slideVelocity, panVelocity, nextTx()));
}

void SharkClient::stopMotion() {
  setMotionVector(0, 0);
}

void SharkClient::onBleAdvertisement(
    studio::ble::LinkHandle link,
    const studio::ble::Advertisement& advertisement) {
  if (link != linkHandle_ || !matchesAdvertisement(advertisement)) {
    return;
  }
  // When a device is remembered, only auto-reconnect to that exact address so
  // we don't latch onto a different nearby slider. Pairing (no saved device)
  // accepts the first matching Shark Nano.
  if (haveTarget_ && targetAddr_[0] != '\0' &&
      std::strcmp(advertisement.address.value, targetAddr_) != 0) {
    return;
  }
  std::strncpy(targetAddr_, advertisement.address.value,
               sizeof(targetAddr_) - 1);
  targetAddrType_ = advertisement.address.type;
  if (!studio::ble::advertisementName(advertisement, targetName_,
                                      sizeof(targetName_))) {
    std::strncpy(targetName_, "Shark Nano II", sizeof(targetName_) - 1);
  }
  haveTarget_ = true;
  state_.link = Link::Connecting;
  studio::ble::bleCentral().selectAdvertisement(linkHandle_, advertisement);
}

void SharkClient::onBleEvent(studio::ble::LinkHandle link,
                             const studio::ble::Event& event) {
  if (link != linkHandle_) {
    return;
  }
  if (event.type == studio::ble::EventType::Connected) {
    client_ = static_cast<NimBLEClient*>(
        studio::ble::bleCentral().nativeClient(linkHandle_));
    if (client_ != nullptr) {
      setupPending_ = true;
    } else {
      studio::ble::bleCentral().markProtocolFailed(linkHandle_);
    }
  } else if (event.type == studio::ble::EventType::ConnectFailed) {
    state_.link = Link::Disconnected;
  } else if (event.type == studio::ble::EventType::Disconnected) {
    client_ = nullptr;
    handleDisconnect();
  }
}

bool SharkClient::protocolReady() const {
  return studio::ble::bleCentral().protocolReady(linkHandle_);
}

void SharkClient::onNotifyBytes(const uint8_t* data, size_t len) {
  if (notifyStream_ == nullptr || data == nullptr || len == 0) {
    return;
  }
  StreamBufferHandle_t stream = static_cast<StreamBufferHandle_t>(notifyStream_);
  xStreamBufferSend(stream, data, len, 0);
}

void SharkClient::resetDeviceState() {
  shark::resetDeviceState(state_);
  haveTable_ = false;
  timingPending_ = false;
  trackingPending_ = false;
}

}  // namespace shark
