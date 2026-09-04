#include "devices/tascam_x8/client.h"

#include <Arduino.h>
#include <NimBLEDevice.h>

#include <cstring>
#include <string>

#include "core/ble/ble_runtime.h"
#include "core/ble/ble_timing.h"
#include "devices/tascam_x8/ble_match.h"
#include "freertos/FreeRTOS.h"
#include "freertos/stream_buffer.h"

#if ARDUINO_USB_CDC_ON_BOOT
#define TASCAM_LOG Serial0
#else
#define TASCAM_LOG Serial
#endif

namespace tascam_x8 {

namespace {

TascamX8Client* gNotifyClient = nullptr;

void dataNotifyTrampoline(NimBLERemoteCharacteristic*, uint8_t* data,
                          size_t length, bool) {
  if (gNotifyClient != nullptr) {
    gNotifyClient->onDataBytes(data, length);
  }
}

void sessionNotifyTrampoline(NimBLERemoteCharacteristic*, uint8_t* data,
                             size_t length, bool) {
  if (gNotifyClient != nullptr && data != nullptr && length > 0) {
    gNotifyClient->onSessionByte(data[length - 1]);
  }
}

}  // namespace

bool TascamX8Client::begin() {
  if (initialized_) {
    return true;
  }
  if (notifyStream_ == nullptr) {
    notifyStream_ = xStreamBufferCreate(2048, 1);
  }
  if (notifyStream_ == nullptr) return false;
  studio::ble::ConnectPolicy policy;
  policy.connectTimeoutMs = 4000;
  policy.connectWatchdogMs = 6000;
  // The unbonded AK-BT1 can accept a quick saved-address connection while its
  // radio is awake. After that single attempt, wait for fresh advertisement
  // evidence instead of spending more backoff cycles on a silent radio.
  policy.directAttemptsBeforeScan = kDirectAttemptsBeforeScan;
  policy.diagnosticTag = "tascam_x8";
  linkHandle_ = studio::ble::bleCentral().acquire(*this, policy);
  initialized_ = linkHandle_ != studio::ble::kInvalidLinkHandle;
  if (!initialized_) {
    vStreamBufferDelete(static_cast<StreamBufferHandle_t>(notifyStream_));
    notifyStream_ = nullptr;
    return false;
  }
  gNotifyClient = this;
  return true;
}

bool TascamX8Client::activate(const char* address, uint8_t addressType,
                              const char* name, bool paired) {
  if (!begin()) return false;
  connectRequested_ = true;
  haveTarget_ = paired && address != nullptr && address[0] != '\0';
  targetAddr_[0] = '\0';
  targetName_[0] = '\0';
  targetAddrType_ = addressType;
  if (haveTarget_) {
    std::strncpy(targetAddr_, address, sizeof(targetAddr_) - 1);
    targetAddr_[sizeof(targetAddr_) - 1] = '\0';
  }
  if (name != nullptr) {
    std::strncpy(targetName_, name, sizeof(targetName_) - 1);
    targetName_[sizeof(targetName_) - 1] = '\0';
  }
  state_.hasSavedDevice = haveTarget_;
  std::strncpy(state_.deviceName, targetName_, sizeof(state_.deviceName) - 1);
  state_.deviceName[sizeof(state_.deviceName) - 1] = '\0';
  if (haveTarget_) {
    beginConnect();
  } else {
    beginScan();
  }
  return true;
}

void TascamX8Client::deactivate() {
  connectRequested_ = false;
  studio::ble::bleCentral().release(linkHandle_);
  linkHandle_ = studio::ble::kInvalidLinkHandle;
  initialized_ = false;
  client_ = nullptr;
  gNotifyClient = nullptr;
  if (notifyStream_ != nullptr) {
    vStreamBufferDelete(static_cast<StreamBufferHandle_t>(notifyStream_));
    notifyStream_ = nullptr;
  }
  startRequested_ = false;
  stopRequested_ = false;
  setupPending_ = false;
  sessionOpening_ = false;
  scanner_.reset();
  resetTransientState(state_);
  state_.link = Link::Disconnected;
}

void TascamX8Client::loop() {
  drainNotifications();

  if (!connectRequested_) {
    return;
  }

  const uint32_t now = millis();
  if (state_.link == Link::Disconnected) {
    const studio::ble::LinkPhase phase =
        studio::ble::bleCentral().phase(linkHandle_);
    if (phase == studio::ble::LinkPhase::Scanning) {
      state_.link = Link::Scanning;
    } else if (phase == studio::ble::LinkPhase::WaitingRetry ||
               phase == studio::ble::LinkPhase::WaitingConnect ||
               phase == studio::ble::LinkPhase::Connecting) {
      state_.link = Link::Connecting;
    }
  }
  if (setupPending_ && static_cast<int32_t>(now - setupAtMs_) >= 0) {
    setupPending_ = false;
    if (!connectRequested_ || client_ == nullptr || !client_->isConnected() ||
        !completeConnect()) {
      studio::ble::bleCentral().markProtocolFailed(linkHandle_);
      return;
    }
  }
  if (state_.link == Link::Connecting &&
      sessionByte_ == kSessionOpenResponse) {
    sessionByte_ = 0;
    sessionOpening_ = false;
    // LinkState and protocol readiness are independent. The transport write
    // helper requires the physical link state, but the sequence remains gated
    // by BleCentral::protocolReady until this initialization write succeeds.
    state_.link = Link::Connected;
    const uint32_t initializationStartedMs = millis();
    if (!sendData(buildInitialization())) {
      state_.link = Link::Disconnected;
      studio::ble::logTiming(
          "tascam_x8", linkHandle_, "session_initialization",
          millis() - initializationStartedMs,
          millis() - studio::ble::bleCentral().timingStartedAt(linkHandle_),
          "failed");
      studio::ble::bleCentral().markProtocolFailed(linkHandle_);
      return;
    }
    studio::ble::logTiming(
        "tascam_x8", linkHandle_, "session_initialization",
        millis() - initializationStartedMs,
        millis() - studio::ble::bleCentral().timingStartedAt(linkHandle_),
        "ok");
    state_.hasSavedDevice = true;
    std::strncpy(state_.deviceName, targetName_,
                 sizeof(state_.deviceName) - 1);
    state_.deviceName[sizeof(state_.deviceName) - 1] = '\0';
    haveTarget_ = true;
    pairingChanged_ = true;
    studio::ble::bleCentral().markProtocolReady(linkHandle_);
    keepaliveAtMs_ = now + 4000;
  } else if (sessionOpening_ && state_.link == Link::Connecting &&
             static_cast<int32_t>(now - sessionDeadlineMs_) >= 0) {
    studio::ble::bleCentral().markProtocolFailed(linkHandle_);
    return;
  }
  if (connected() && startRequested_) {
    startRequested_ = false;
    if (!sendData(buildRecordStart())) {
      markCommandWriteFailed(state_);
    } else {
      commandDeadlineMs_ = now + 6000;
    }
  }
  if (connected() && stopRequested_) {
    stopRequested_ = false;
    if (!sendData(buildRecordStop())) {
      markCommandWriteFailed(state_);
    } else {
      commandDeadlineMs_ = now + 6000;
    }
  }
  if (connected() && state_.commandPending &&
      static_cast<int32_t>(now - commandDeadlineMs_) >= 0) {
    markCommandWriteFailed(state_);
  }
  if (connected() && static_cast<int32_t>(now - keepaliveAtMs_) >= 0) {
    const uint8_t value = kSessionKeepalive;
    if (sessionChar_ == nullptr ||
        !sessionChar_->writeValue(&value, 1, true)) {
      studio::ble::bleCentral().markProtocolFailed(linkHandle_);
    } else {
      keepaliveAtMs_ = now + kSessionKeepaliveMs;
    }
  }
}

void TascamX8Client::startScan() {
  begin();
  connectRequested_ = true;
  teardownConnection();
  haveTarget_ = false;
  state_.hasSavedDevice = false;
  resetTransientState(state_);
  beginScan();
}

void TascamX8Client::forgetDevice() {
  haveTarget_ = false;
  targetAddr_[0] = '\0';
  targetName_[0] = '\0';
  state_.hasSavedDevice = false;
  pairingChanged_ = true;
  startScan();
}

bool TascamX8Client::startRecording() {
  if (!connected() || state_.commandPending ||
      (state_.recordingConfirmed &&
       state_.recording == State::Recording::Recording)) {
    return false;
  }
  markCommandQueued(state_, true);
  if (!sendData(buildRecordStart())) {
    markCommandWriteFailed(state_);
    return false;
  }
  commandDeadlineMs_ = millis() + 6000;
  return true;
}

bool TascamX8Client::stopRecording() {
  if (!connected() || state_.commandPending) {
    return false;
  }
  if (completeStopIfAlreadyStopped(state_)) {
    return true;
  }
  markCommandQueued(state_, false);
  if (!sendData(buildRecordStop())) {
    markCommandWriteFailed(state_);
    return false;
  }
  commandDeadlineMs_ = millis() + 6000;
  return true;
}

bool TascamX8Client::consumePairingUpdate(
    char* address, size_t addressCapacity, uint8_t& addressType, char* name,
    size_t nameCapacity, bool& paired) {
  if (!pairingChanged_) {
    return false;
  }
  pairingChanged_ = false;
  paired = haveTarget_;
  addressType = targetAddrType_;
  if (address != nullptr && addressCapacity > 0) {
    std::strncpy(address, targetAddr_, addressCapacity - 1);
    address[addressCapacity - 1] = '\0';
  }
  if (name != nullptr && nameCapacity > 0) {
    std::strncpy(name, targetName_, nameCapacity - 1);
    name[nameCapacity - 1] = '\0';
  }
  return true;
}

void TascamX8Client::beginScan() {
  studio::ble::bleCentral().requestScan(linkHandle_, !haveTarget_);
  state_.link = Link::Scanning;
}

void TascamX8Client::beginConnect() {
  state_.link = Link::Connecting;
  setupPending_ = false;
  sessionOpening_ = false;
  studio::ble::Address address;
  std::strncpy(address.value, targetAddr_, sizeof(address.value) - 1);
  address.type = targetAddrType_;
  studio::ble::bleCentral().requestConnect(linkHandle_, address);
}

bool TascamX8Client::completeConnect() {
  const uint32_t discoveryStartedMs = millis();
  NimBLERemoteService* service =
      client_->getService(NimBLEUUID(kPrimaryServiceUuid));
  if (service == nullptr) {
    studio::ble::logTiming(
        "tascam_x8", linkHandle_, "gatt_setup",
        millis() - discoveryStartedMs,
        millis() - studio::ble::bleCentral().timingStartedAt(linkHandle_),
        "failed");
    return false;
  }
  dataChar_ =
      service->getCharacteristic(NimBLEUUID(kDataCharacteristicUuid));
  sessionChar_ =
      service->getCharacteristic(NimBLEUUID(kSessionCharacteristicUuid));
  if (dataChar_ == nullptr || sessionChar_ == nullptr ||
      !sessionChar_->subscribe(true, sessionNotifyTrampoline, true) ||
      !dataChar_->subscribe(true, dataNotifyTrampoline, true)) {
    dataChar_ = nullptr;
    sessionChar_ = nullptr;
    return false;
  }
  studio::ble::logTiming(
      "tascam_x8", linkHandle_, "gatt_setup",
      millis() - discoveryStartedMs,
      millis() - studio::ble::bleCentral().timingStartedAt(linkHandle_), "ok");

  scanner_.reset();
  sessionByte_ = 0;
  const uint8_t open = kSessionOpen;
  if (!sessionChar_->writeValue(&open, 1, true)) {
    return false;
  }

  resetTransientState(state_);
  state_.link = Link::Connecting;
  sessionOpening_ = true;
  sessionDeadlineMs_ = millis() + 3000;
  return true;
}

void TascamX8Client::teardownConnection() {
  studio::ble::bleCentral().disconnect(linkHandle_);
  setupPending_ = false;
  sessionOpening_ = false;
  dataChar_ = nullptr;
  sessionChar_ = nullptr;
  sessionByte_ = 0;
}

void TascamX8Client::handleDisconnect() {
  dataChar_ = nullptr;
  sessionChar_ = nullptr;
  startRequested_ = false;
  stopRequested_ = false;
  setupPending_ = false;
  sessionOpening_ = false;
  sessionByte_ = 0;
  scanner_.reset();
  resetTransientState(state_);
  state_.link = Link::Disconnected;
}

void TascamX8Client::drainNotifications() {
  if (notifyStream_ == nullptr) {
    return;
  }
  StreamBufferHandle_t stream =
      static_cast<StreamBufferHandle_t>(notifyStream_);
  uint8_t bytes[256];
  size_t received = 0;
  while ((received =
              xStreamBufferReceive(stream, bytes, sizeof(bytes), 0)) > 0) {
    scanner_.feed(bytes, received, [this](const ParsedFrame& frame) {
      reduceFrame(state_, frame);
    });
  }
}

bool TascamX8Client::sendData(const FrameBytes& frame) {
  return connected() && dataChar_ != nullptr && frame.len > 0 &&
         dataChar_->writeValue(frame.bytes, frame.len, true);
}

void TascamX8Client::onBleAdvertisement(
    studio::ble::LinkHandle link,
    const studio::ble::Advertisement& advertisement) {
  if (link == linkHandle_ && matchesAdvertisement(advertisement)) {
    // Sightings tell wake diagnosis whether the AK-BT1 was silent or merely
    // missed by the scan duty cycle.
    TASCAM_LOG.printf("tascam adv addr=%s type=%u rssi=%d\n",
                      advertisement.address.value,
                      static_cast<unsigned>(advertisement.address.type),
                      static_cast<int>(advertisement.rssi));
  }
  if (link != linkHandle_ ||
      !matchesSavedAdvertisement(advertisement,
                                 haveTarget_ ? targetAddr_ : nullptr,
                                 targetAddrType_)) {
    return;
  }
  std::strncpy(targetAddr_, advertisement.address.value,
               sizeof(targetAddr_) - 1);
  targetAddrType_ = advertisement.address.type;
  studio::ble::advertisementName(advertisement, targetName_,
                                 sizeof(targetName_));
  haveTarget_ = true;
  state_.link = Link::Connecting;
  studio::ble::bleCentral().selectAdvertisement(linkHandle_, advertisement);
}

void TascamX8Client::onBleEvent(studio::ble::LinkHandle link,
                                const studio::ble::Event& event) {
  if (link != linkHandle_) {
    return;
  }
  if (event.type == studio::ble::EventType::Connected) {
    client_ = static_cast<NimBLEClient*>(
        studio::ble::bleCentral().nativeClient(linkHandle_));
    setupPending_ = true;
    setupAtMs_ = millis();
  } else if (event.type == studio::ble::EventType::ConnectFailed) {
    studio::ble::logEventReason("tascam_x8", linkHandle_, "connect_failed",
                                event.reason);
    state_.link = Link::Disconnected;
  } else if (event.type == studio::ble::EventType::Disconnected) {
    studio::ble::logEventReason("tascam_x8", linkHandle_, "disconnected",
                                event.reason);
    client_ = nullptr;
    handleDisconnect();
  }
}

bool TascamX8Client::protocolReady() const {
  return studio::ble::bleCentral().protocolReady(linkHandle_);
}

void TascamX8Client::onDataBytes(const uint8_t* data, size_t len) {
  if (notifyStream_ == nullptr || data == nullptr || len == 0) {
    return;
  }
  StreamBufferHandle_t stream =
      static_cast<StreamBufferHandle_t>(notifyStream_);
  xStreamBufferSend(stream, data, len, 0);
}

void TascamX8Client::onSessionByte(uint8_t value) {
  sessionByte_ = value;
}

}  // namespace tascam_x8
