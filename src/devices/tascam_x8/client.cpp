#include "devices/tascam_x8/client.h"

#include <Arduino.h>
#include <NimBLEDevice.h>

#include <cstring>
#include <string>

#include "freertos/FreeRTOS.h"
#include "freertos/stream_buffer.h"

namespace tascam_x8 {

namespace {

const NimBLEUUID kPrimaryService(kPrimaryServiceUuid);
const NimBLEUUID kDataCharacteristic(kDataCharacteristicUuid);
const NimBLEUUID kSessionCharacteristic(kSessionCharacteristicUuid);
TascamX8Client* gCallbackClient = nullptr;

class ScanCallbacks : public NimBLEScanCallbacks {
 public:
  void onResult(const NimBLEAdvertisedDevice* device) override {
    if (device != nullptr && device->getName() == kDeviceName &&
        gCallbackClient != nullptr) {
      gCallbackClient->onScanMatch(device);
    }
  }
};

class ClientCallbacks : public NimBLEClientCallbacks {
 public:
  void onConnect(NimBLEClient*) override {
    if (gCallbackClient != nullptr) {
      gCallbackClient->onLinkConnected();
    }
  }
  void onConnectFail(NimBLEClient*, int) override {
    if (gCallbackClient != nullptr) {
      gCallbackClient->onConnectFailed();
    }
  }
  void onDisconnect(NimBLEClient*, int) override {
    if (gCallbackClient != nullptr) {
      gCallbackClient->onLinkDisconnected();
    }
  }
};

void dataNotifyTrampoline(NimBLERemoteCharacteristic*, uint8_t* data,
                          size_t length, bool) {
  if (gCallbackClient != nullptr) {
    gCallbackClient->onDataBytes(data, length);
  }
}

void sessionNotifyTrampoline(NimBLERemoteCharacteristic*, uint8_t* data,
                             size_t length, bool) {
  if (gCallbackClient != nullptr && data != nullptr && length > 0) {
    gCallbackClient->onSessionByte(data[length - 1]);
  }
}

ScanCallbacks gScanCallbacks;
ClientCallbacks gClientCallbacks;

}  // namespace

void TascamX8Client::begin() {
  if (initialized_) {
    return;
  }
  notifyStream_ = xStreamBufferCreate(2048, 1);
  NimBLEDevice::init("");
  NimBLEDevice::setMTU(247);
  gCallbackClient = this;
  initialized_ = true;
}

void TascamX8Client::activate(const char* address, uint8_t addressType,
                              const char* name, bool paired) {
  begin();
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
  connectFails_ = 0;
  retryAtMs_ = 0;
  if (haveTarget_) {
    beginConnect();
  } else {
    beginScan();
  }
}

void TascamX8Client::deactivate() {
  connectRequested_ = false;
  if (initialized_ && scanActive_) {
    NimBLEDevice::getScan()->stop();
    scanActive_ = false;
  }
  teardownConnection();
  scanHit_ = false;
  connectedFlag_ = false;
  connectFailedFlag_ = false;
  disconnectedFlag_ = false;
  startRequested_ = false;
  stopRequested_ = false;
  setupPending_ = false;
  sessionOpening_ = false;
  scanner_.reset();
  resetTransientState(state_);
  state_.link = Link::Disconnected;
}

void TascamX8Client::loop() {
  if (connectFailedFlag_) {
    connectFailedFlag_ = false;
    ++connectFails_;
    state_.link = Link::Disconnected;
    scheduleRetry(1500u * (connectFails_ < 4 ? connectFails_ : 4));
  }
  if (disconnectedFlag_) {
    disconnectedFlag_ = false;
    handleDisconnect();
  }
  if (connectedFlag_) {
    connectedFlag_ = false;
    setupPending_ = true;
    setupAtMs_ = millis() + 100;
  }

  drainNotifications();

  if (!connectRequested_) {
    return;
  }

  if (scanHit_) {
    NimBLEDevice::getScan()->stop();
    scanActive_ = false;
    std::strncpy(targetAddr_, scanHitAddr_, sizeof(targetAddr_) - 1);
    targetAddr_[sizeof(targetAddr_) - 1] = '\0';
    targetAddrType_ = scanHitType_;
    std::strncpy(targetName_, scanHitName_, sizeof(targetName_) - 1);
    targetName_[sizeof(targetName_) - 1] = '\0';
    haveTarget_ = true;
    scanHit_ = false;
    beginConnect();
  }

  const uint32_t now = millis();
  if (setupPending_ && static_cast<int32_t>(now - setupAtMs_) >= 0) {
    setupPending_ = false;
    if (!connectRequested_ || client_ == nullptr || !client_->isConnected() ||
        !completeConnect()) {
      if (client_ != nullptr && client_->isConnected()) {
        client_->disconnect();
      }
      return;
    }
  }
  if (state_.link == Link::Connecting &&
      sessionByte_ == kSessionOpenResponse) {
    sessionByte_ = 0;
    sessionOpening_ = false;
    state_.link = Link::Connected;
    state_.hasSavedDevice = true;
    std::strncpy(state_.deviceName, targetName_,
                 sizeof(state_.deviceName) - 1);
    state_.deviceName[sizeof(state_.deviceName) - 1] = '\0';
    haveTarget_ = true;
    pairingChanged_ = true;
    connectFails_ = 0;
    keepaliveAtMs_ = now + 4000;
    if (!sendData(buildInitialization())) {
      client_->disconnect();
      return;
    }
  } else if (sessionOpening_ && state_.link == Link::Connecting &&
             static_cast<int32_t>(now - sessionDeadlineMs_) >= 0) {
    client_->disconnect();
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
      if (client_ != nullptr) {
        client_->disconnect();
      }
    } else {
      keepaliveAtMs_ = now + kSessionKeepaliveMs;
    }
  }

  if (state_.link == Link::Disconnected &&
      static_cast<int32_t>(now - retryAtMs_) >= 0) {
    if (haveTarget_ && connectFails_ < 2) {
      beginConnect();
    } else {
      beginScan();
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
  startRequested_ = true;
  return true;
}

bool TascamX8Client::stopRecording() {
  if (!connected() || state_.commandPending ||
      (state_.recordingConfirmed &&
       state_.recording == State::Recording::Stopped)) {
    return false;
  }
  markCommandQueued(state_, false);
  stopRequested_ = true;
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
  if (scanActive_) {
    state_.link = Link::Scanning;
    return;
  }
  NimBLEScan* scan = NimBLEDevice::getScan();
  scan->setScanCallbacks(&gScanCallbacks, false);
  scan->setActiveScan(true);
  scan->setInterval(100);
  scan->setWindow(80);
  scan->clearResults();
  scanHit_ = false;
  scan->start(0, false, true);
  scanActive_ = true;
  state_.link = Link::Scanning;
}

void TascamX8Client::beginConnect() {
  if (client_ == nullptr) {
    client_ = NimBLEDevice::createClient();
    client_->setClientCallbacks(&gClientCallbacks, false);
    client_->setConnectTimeout(4000);
  }
  state_.link = Link::Connecting;
  NimBLEAddress address(std::string(targetAddr_), targetAddrType_);
  connectedFlag_ = false;
  connectFailedFlag_ = false;
  setupPending_ = false;
  sessionOpening_ = false;
  if (client_->connect(address, true, true, true)) {
    return;
  }
  ++connectFails_;
  state_.link = Link::Disconnected;
  scheduleRetry(1500u * (connectFails_ < 4 ? connectFails_ : 4));
}

bool TascamX8Client::completeConnect() {
  NimBLERemoteService* service = client_->getService(kPrimaryService);
  if (service == nullptr) {
    return false;
  }
  dataChar_ = service->getCharacteristic(kDataCharacteristic);
  sessionChar_ = service->getCharacteristic(kSessionCharacteristic);
  if (dataChar_ == nullptr || sessionChar_ == nullptr ||
      !sessionChar_->subscribe(true, sessionNotifyTrampoline, true) ||
      !dataChar_->subscribe(true, dataNotifyTrampoline, true)) {
    dataChar_ = nullptr;
    sessionChar_ = nullptr;
    return false;
  }

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
  if (client_ != nullptr && client_->isConnected()) {
    client_->disconnect();
  } else if (client_ != nullptr && state_.link == Link::Connecting) {
    client_->cancelConnect();
  }
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
  scheduleRetry(1500);
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

void TascamX8Client::scheduleRetry(uint32_t delayMs) {
  retryAtMs_ = millis() + delayMs;
}

void TascamX8Client::onScanMatch(
    const NimBLEAdvertisedDevice* device) {
  if (device == nullptr || scanHit_) {
    return;
  }
  const std::string address = device->getAddress().toString();
  const std::string name = device->getName();
  std::strncpy(scanHitAddr_, address.c_str(), sizeof(scanHitAddr_) - 1);
  scanHitAddr_[sizeof(scanHitAddr_) - 1] = '\0';
  scanHitType_ = device->getAddress().getType();
  std::strncpy(scanHitName_, name.c_str(), sizeof(scanHitName_) - 1);
  scanHitName_[sizeof(scanHitName_) - 1] = '\0';
  scanHit_ = true;
}

void TascamX8Client::onLinkConnected() {
  connectedFlag_ = true;
}

void TascamX8Client::onConnectFailed() {
  connectFailedFlag_ = true;
}

void TascamX8Client::onLinkDisconnected() {
  disconnectedFlag_ = true;
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
