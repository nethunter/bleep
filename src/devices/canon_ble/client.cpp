#include "devices/canon_ble/client.h"

#include <Arduino.h>
#include <NimBLEDevice.h>

#include <cstring>
#include <string>

#include "devices/canon_ble/protocol.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

namespace canon_ble {

namespace {

constexpr uint8_t kPairingNotification = 1;
constexpr uint8_t kPairingInfoNotification = 2;
constexpr uint8_t kModeNotification = 3;
constexpr uint8_t kShootingNotification = 4;

struct Notification {
  uint8_t kind = 0;
  uint8_t len = 0;
  uint8_t data[20] = {};
};

void buildStableControllerId(uint8_t id[16]) {
  const uint64_t chipId = ESP.getEfuseMac();
  for (size_t i = 0; i < 8; ++i) {
    id[i] = static_cast<uint8_t>(chipId >> (i * 8));
    id[i + 8] = static_cast<uint8_t>(id[i] ^ (0xA5u + i));
  }
}

const NimBLEUUID kHandshakeService(kHandshakeServiceUuid);
const NimBLEUUID kPairingCommandCharacteristic(
    kPairingCommandCharacteristicUuid);
const NimBLEUUID kPairingDataCharacteristic(kPairingDataCharacteristicUuid);
const NimBLEUUID kPairingInfoCharacteristic(kPairingInfoCharacteristicUuid);
const NimBLEUUID kCoreService(kCoreServiceUuid);
const NimBLEUUID kModeCommandCharacteristic(kModeCommandCharacteristicUuid);
const NimBLEUUID kModeResultCharacteristic(kModeResultCharacteristicUuid);
const NimBLEUUID kShootingCommandCharacteristic(
    kShootingCommandCharacteristicUuid);
const NimBLEUUID kShootingStateCharacteristic(
    kShootingStateCharacteristicUuid);
CanonBleClient* gCallbackClient = nullptr;

class ScanCallbacks : public NimBLEScanCallbacks {
 public:
  void onResult(const NimBLEAdvertisedDevice* device) override {
    if (device == nullptr || gCallbackClient == nullptr) {
      return;
    }
    const std::string name = device->getName();
    const bool canonCameraName =
        name.rfind("EOS", 0) == 0 || name.rfind("PowerShot", 0) == 0;
    if (device->isAdvertisingService(kHandshakeService) || canonCameraName) {
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
  void onAuthenticationComplete(NimBLEConnInfo& info) override {
    if (gCallbackClient != nullptr) {
      gCallbackClient->onSecurityComplete(info.isEncrypted());
    }
  }
};

void pairingNotifyTrampoline(NimBLERemoteCharacteristic*, uint8_t* data,
                             size_t length, bool) {
  if (gCallbackClient != nullptr) {
    gCallbackClient->onPairingNotification(data, length);
  }
}

void pairingInfoNotifyTrampoline(NimBLERemoteCharacteristic*, uint8_t* data,
                                 size_t length, bool) {
  if (gCallbackClient != nullptr) {
    gCallbackClient->onPairingInfoNotification(data, length);
  }
}

void modeNotifyTrampoline(NimBLERemoteCharacteristic*, uint8_t* data,
                          size_t length, bool) {
  if (gCallbackClient != nullptr) {
    gCallbackClient->onModeNotification(data, length);
  }
}

void shootingNotifyTrampoline(NimBLERemoteCharacteristic*, uint8_t* data,
                              size_t length, bool) {
  if (gCallbackClient != nullptr) {
    gCallbackClient->onShootingNotification(data, length);
  }
}

ScanCallbacks gScanCallbacks;
ClientCallbacks gClientCallbacks;

}  // namespace

void CanonBleClient::begin() {
  if (initialized_) {
    return;
  }
  notifyQueue_ = xQueueCreate(8, sizeof(Notification));
  NimBLEDevice::init("StudioRemote");
  NimBLEDevice::setMTU(247);
  NimBLEDevice::setSecurityAuth(true, false, false);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
  gCallbackClient = this;
  initialized_ = true;
}

void CanonBleClient::activate(const char* address, uint8_t addressType,
                              const char* name, bool paired) {
  begin();
  connectRequested_ = true;
  haveTarget_ = paired && address != nullptr && address[0] != '\0';
  newHandshake_ = !haveTarget_;
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
  resetTransientState(state_);
  state_.hasSavedDevice = haveTarget_;
  std::strncpy(state_.deviceName, targetName_, sizeof(state_.deviceName) - 1);
  state_.deviceName[sizeof(state_.deviceName) - 1] = '\0';
  connectFails_ = 0;
  securityFails_ = 0;
  bondRecoveryPending_ = false;
  retryAtMs_ = 0;
  if (haveTarget_) {
    beginConnect();
  } else {
    beginScan();
  }
}

void CanonBleClient::deactivate() {
  connectRequested_ = false;
  if (initialized_ && scanActive_) {
    NimBLEDevice::getScan()->stop();
    scanActive_ = false;
  }
  teardownConnection();
  scanHit_ = false;
  disconnectedFlag_ = false;
  connectedFlag_ = false;
  connectFailedFlag_ = false;
  securityCompleteFlag_ = false;
  startRequested_ = false;
  stopRequested_ = false;
  powerOffRequested_ = false;
  setupPending_ = false;
  bondRecoveryPending_ = false;
  postPairStep_ = 0;
  if (notifyQueue_ != nullptr) {
    xQueueReset(static_cast<QueueHandle_t>(notifyQueue_));
  }
  resetTransientState(state_);
  state_.link = Link::Disconnected;
}

void CanonBleClient::loop() {
  if (connectFailedFlag_) {
    connectFailedFlag_ = false;
    ++connectFails_;
    resetTransientState(state_);
    state_.link = Link::Disconnected;
    scheduleRetry(1500u * (connectFails_ < 4 ? connectFails_ : 4));
  }
  if (disconnectedFlag_) {
    disconnectedFlag_ = false;
    handleDisconnect();
  }
  if (connectedFlag_) {
    connectedFlag_ = false;
    if (client_ != nullptr && client_->getConnInfo().isEncrypted()) {
      securityFails_ = 0;
      setupPending_ = true;
      setupAtMs_ = millis() + 100;
    } else if (client_ == nullptr || !client_->secureConnection(true)) {
      handleSecurityFailure();
    }
  }
  if (securityCompleteFlag_) {
    securityCompleteFlag_ = false;
    if (!securitySucceeded_) {
      handleSecurityFailure();
    } else {
      securityFails_ = 0;
      setupPending_ = true;
      setupAtMs_ = millis() + 100;
    }
  }

  drainNotifications();

  if (connectRequested_ && scanHit_) {
    NimBLEDevice::getScan()->stop();
    scanActive_ = false;
    std::strncpy(targetAddr_, scanHitAddr_, sizeof(targetAddr_) - 1);
    targetAddr_[sizeof(targetAddr_) - 1] = '\0';
    targetAddrType_ = scanHitType_;
    std::strncpy(targetName_, scanHitName_, sizeof(targetName_) - 1);
    targetName_[sizeof(targetName_) - 1] = '\0';
    haveTarget_ = true;
    newHandshake_ = true;
    scanHit_ = false;
    beginConnect();
  }

  const uint32_t now = millis();
  if (connectRequested_ && setupPending_ &&
      static_cast<int32_t>(now - setupAtMs_) >= 0) {
    setupPending_ = false;
    if (client_ == nullptr || !client_->isConnected() || !completeConnect()) {
      if (client_ != nullptr && client_->isConnected()) {
        client_->disconnect();
      }
      return;
    }
  }

  if (connectRequested_ &&
      (state_.phase == State::Phase::AwaitingConfirmation ||
       state_.phase == State::Phase::PostPairSetup ||
       state_.phase == State::Phase::OpeningSession) &&
      static_cast<int32_t>(now - phaseDeadlineMs_) >= 0) {
    if (client_ != nullptr) {
      client_->disconnect();
    }
    return;
  }

  if (powerOffRequested_ && state_.link == Link::Connected &&
      state_.phase == State::Phase::PoweringOff) {
    powerOffRequested_ = false;
    if (!writeCommand(modeCommandChar_, buildModeCommand(kPowerOffMode))) {
      state_.phase = State::Phase::Ready;
      state_.powerOffFailed = true;
    } else {
      connectRequested_ = false;
      phaseDeadlineMs_ = now + 1500;
    }
  }
  if (state_.phase == State::Phase::PoweringOff &&
      static_cast<int32_t>(now - phaseDeadlineMs_) >= 0) {
    connectRequested_ = true;
    state_.phase = State::Phase::Ready;
    state_.powerOffFailed = true;
  }

  if (connected() && startRequested_) {
    startRequested_ = false;
    if (!writeCommand(shootingCommandChar_, buildRecordCommand(true))) {
      markCommandWriteFailed(state_);
    } else {
      commandDeadlineMs_ = now + 6000;
    }
  }
  if (connected() && stopRequested_) {
    stopRequested_ = false;
    if (!writeCommand(shootingCommandChar_, buildRecordCommand(false))) {
      markCommandWriteFailed(state_);
    } else {
      commandDeadlineMs_ = now + 6000;
    }
  }
  if (connected() && state_.commandPending &&
      static_cast<int32_t>(now - commandDeadlineMs_) >= 0) {
    markCommandWriteFailed(state_);
  }

  if (!connectRequested_) {
    return;
  }
  if (state_.link != Link::Disconnected) {
    return;
  }
  if (static_cast<int32_t>(now - retryAtMs_) >= 0) {
    if (haveTarget_ && connectFails_ < 2) {
      beginConnect();
    } else {
      beginScan();
    }
  }
}

void CanonBleClient::startScan() {
  begin();
  connectRequested_ = true;
  teardownConnection();
  haveTarget_ = false;
  newHandshake_ = true;
  state_.hasSavedDevice = false;
  resetTransientState(state_);
  beginScan();
}

void CanonBleClient::forgetDevice() {
  forgetBond(targetAddr_, targetAddrType_);
  haveTarget_ = false;
  targetAddr_[0] = '\0';
  targetName_[0] = '\0';
  state_.hasSavedDevice = false;
  pairingChanged_ = true;
  startScan();
}

void CanonBleClient::forgetBond(const char* address, uint8_t addressType) {
  if (address == nullptr || address[0] == '\0') {
    return;
  }
  begin();
  NimBLEAddress peer(std::string(address), addressType);
  NimBLEDevice::deleteBond(peer);
}

bool CanonBleClient::startRecording() {
  if (!connected() || state_.commandPending ||
      (state_.recordingConfirmed &&
       state_.recording == State::Recording::Recording)) {
    return false;
  }
  markCommandQueued(state_, true);
  startRequested_ = true;
  return true;
}

bool CanonBleClient::stopRecording() {
  if (!connected() || state_.commandPending ||
      (state_.recordingConfirmed &&
       state_.recording == State::Recording::Stopped)) {
    return false;
  }
  markCommandQueued(state_, false);
  stopRequested_ = true;
  return true;
}

bool CanonBleClient::powerOn() {
  if (state_.link != Link::Disconnected ||
      state_.phase != State::Phase::PoweredOff) {
    return false;
  }
  connectRequested_ = true;
  connectFails_ = 0;
  securityFails_ = 0;
  resetTransientState(state_);
  state_.hasSavedDevice = haveTarget_;
  if (haveTarget_) {
    beginConnect();
  } else {
    beginScan();
  }
  return true;
}

bool CanonBleClient::powerOff() {
  if (!connected() || state_.commandPending ||
      (state_.recordingConfirmed &&
       state_.recording == State::Recording::Recording)) {
    return false;
  }
  state_.phase = State::Phase::PoweringOff;
  state_.powerOffFailed = false;
  powerOffRequested_ = true;
  return true;
}

bool CanonBleClient::consumePairingUpdate(
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

void CanonBleClient::beginScan() {
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

void CanonBleClient::beginConnect() {
  if (client_ == nullptr) {
    client_ = NimBLEDevice::createClient();
    client_->setClientCallbacks(&gClientCallbacks, false);
    client_->setConnectTimeout(4000);
  }
  resetTransientState(state_);
  state_.link = Link::Connecting;
  state_.phase = State::Phase::Bonding;
  NimBLEAddress address(std::string(targetAddr_), targetAddrType_);
  connectedFlag_ = false;
  connectFailedFlag_ = false;
  securityCompleteFlag_ = false;
  setupPending_ = false;
  powerOffRequested_ = false;
  postPairStep_ = 0;
  if (client_->connect(address, true, true, true)) {
    return;
  }
  ++connectFails_;
  state_.link = Link::Disconnected;
  scheduleRetry(1500u * (connectFails_ < 4 ? connectFails_ : 4));
}

bool CanonBleClient::completeConnect() {
  NimBLERemoteService* handshake =
      client_ != nullptr ? client_->getService(kHandshakeService) : nullptr;
  if (handshake == nullptr) {
    return false;
  }
  pairingCommandChar_ =
      handshake->getCharacteristic(kPairingCommandCharacteristic);
  pairingDataChar_ = handshake->getCharacteristic(kPairingDataCharacteristic);
  pairingInfoChar_ = handshake->getCharacteristic(kPairingInfoCharacteristic);
  if (pairingCommandChar_ == nullptr || pairingDataChar_ == nullptr ||
      pairingInfoChar_ == nullptr) {
    return false;
  }

  if (!newHandshake_) {
    if (!subscribePairingInfo() ||
        !writeCommand(pairingDataChar_, buildHandshakeFinish())) {
      return false;
    }
    return beginPostPairSetup();
  }

  if (!writeCommand(pairingCommandChar_,
                    buildHandshakeRequest("StudioRemote"))) {
    return false;
  }
  const bool pairingSubscribed =
      pairingCommandChar_->canIndicate()
          ? pairingCommandChar_->subscribe(false, pairingNotifyTrampoline, true)
          : pairingCommandChar_->subscribe(true, pairingNotifyTrampoline, true);
  if (!pairingSubscribed) {
    return false;
  }
  state_.phase = State::Phase::AwaitingConfirmation;
  phaseDeadlineMs_ = millis() + 30000;
  return true;
}

bool CanonBleClient::sendNewHandshakeIdentity() {
  uint8_t controllerId[16];
  buildStableControllerId(controllerId);
  return writeCommand(pairingDataChar_, buildControllerId(controllerId)) &&
         writeCommand(pairingDataChar_, buildDeviceName("StudioRemote")) &&
         writeCommand(pairingDataChar_, buildAndroidDeviceType());
}

bool CanonBleClient::finishAcceptedHandshake() {
  if (!subscribePairingInfo() || !sendNewHandshakeIdentity() ||
      !writeCommand(pairingDataChar_, buildHandshakeFinish())) {
    return false;
  }
  newHandshake_ = false;
  return beginPostPairSetup();
}

bool CanonBleClient::subscribePairingInfo() {
  if (pairingInfoChar_ == nullptr) {
    return false;
  }
  return pairingInfoChar_->canIndicate()
             ? pairingInfoChar_->subscribe(false, pairingInfoNotifyTrampoline,
                                           true)
             : pairingInfoChar_->subscribe(true, pairingInfoNotifyTrampoline,
                                           true);
}

bool CanonBleClient::beginPostPairSetup() {
  state_.phase = State::Phase::PostPairSetup;
  postPairStep_ = 0;
  return sendPostPairCommand();
}

bool CanonBleClient::sendPostPairCommand() {
  if (postPairStep_ >= sizeof(kPostPairCommands)) {
    return openCoreSession();
  }
  if (!writeCommand(pairingDataChar_,
                    buildPostPairCommand(kPostPairCommands[postPairStep_]))) {
    return false;
  }
  phaseDeadlineMs_ = millis() + 1500;
  return true;
}

bool CanonBleClient::openCoreSession() {
  NimBLERemoteService* core =
      client_ != nullptr ? client_->getService(kCoreService) : nullptr;
  if (core == nullptr) {
    return false;
  }
  modeCommandChar_ = core->getCharacteristic(kModeCommandCharacteristic);
  modeResultChar_ = core->getCharacteristic(kModeResultCharacteristic);
  shootingCommandChar_ =
      core->getCharacteristic(kShootingCommandCharacteristic);
  shootingStateChar_ =
      core->getCharacteristic(kShootingStateCharacteristic);
  if (modeCommandChar_ == nullptr || modeResultChar_ == nullptr ||
      shootingCommandChar_ == nullptr || shootingStateChar_ == nullptr ||
      !modeResultChar_->subscribe(true, modeNotifyTrampoline, true) ||
      !shootingStateChar_->subscribe(true, shootingNotifyTrampoline, true)) {
    return false;
  }

  resetTransientState(state_);
  state_.link = Link::Connected;
  state_.phase = State::Phase::OpeningSession;
  state_.hasSavedDevice = true;
  std::strncpy(state_.deviceName, targetName_, sizeof(state_.deviceName) - 1);
  state_.deviceName[sizeof(state_.deviceName) - 1] = '\0';
  haveTarget_ = true;
  pairingChanged_ = true;
  connectFails_ = 0;
  phaseDeadlineMs_ = millis() + 3000;
  if (!writeCommand(modeCommandChar_, buildModeCommand(kWakeMode))) {
    return false;
  }
  return true;
}

void CanonBleClient::teardownConnection() {
  if (client_ != nullptr && client_->isConnected()) {
    client_->disconnect();
  } else if (client_ != nullptr && state_.link == Link::Connecting) {
    client_->cancelConnect();
  }
  setupPending_ = false;
  pairingCommandChar_ = nullptr;
  pairingDataChar_ = nullptr;
  pairingInfoChar_ = nullptr;
  modeCommandChar_ = nullptr;
  modeResultChar_ = nullptr;
  shootingCommandChar_ = nullptr;
  shootingStateChar_ = nullptr;
}

void CanonBleClient::handleDisconnect() {
  const bool pairingRejected = state_.pairingRejected;
  const bool poweredOff =
      state_.phase == State::Phase::PoweringOff && !connectRequested_;
  const bool recoverPairing = bondRecoveryPending_;
  bondRecoveryPending_ = false;
  if (recoverPairing) {
    forgetBond(targetAddr_, targetAddrType_);
    haveTarget_ = false;
    newHandshake_ = true;
    pairingChanged_ = true;
  }
  pairingCommandChar_ = nullptr;
  pairingDataChar_ = nullptr;
  pairingInfoChar_ = nullptr;
  modeCommandChar_ = nullptr;
  modeResultChar_ = nullptr;
  shootingCommandChar_ = nullptr;
  shootingStateChar_ = nullptr;
  startRequested_ = false;
  stopRequested_ = false;
  powerOffRequested_ = false;
  setupPending_ = false;
  postPairStep_ = 0;
  resetTransientState(state_);
  state_.pairingRejected = pairingRejected;
  state_.hasSavedDevice = haveTarget_;
  state_.link = Link::Disconnected;
  if (poweredOff) {
    state_.phase = State::Phase::PoweredOff;
  }
  if (connectRequested_) {
    scheduleRetry(1500);
  }
}

void CanonBleClient::handleSecurityFailure() {
  ++securityFails_;
  bondRecoveryPending_ = haveTarget_ && securityFails_ >= 2;
  if (client_ != nullptr) {
    client_->disconnect();
  }
}

void CanonBleClient::readInitialRecordingState() {
  if (shootingStateChar_ == nullptr || !shootingStateChar_->canRead()) {
    return;
  }
  const NimBLEAttValue value = shootingStateChar_->readValue();
  reduceRecordNotification(state_, value.data(), value.size());
}

void CanonBleClient::drainNotifications() {
  if (notifyQueue_ == nullptr) {
    return;
  }
  Notification notification;
  QueueHandle_t queue = static_cast<QueueHandle_t>(notifyQueue_);
  while (xQueueReceive(queue, &notification, 0) == pdTRUE) {
    if (notification.kind == kPairingNotification &&
        state_.phase == State::Phase::AwaitingConfirmation) {
      const PairingResponse response =
          parsePairingResponse(notification.data, notification.len);
      if (response == PairingResponse::Confirmed) {
        state_.phase = State::Phase::Handshaking;
        if (!finishAcceptedHandshake() && client_ != nullptr) {
          client_->disconnect();
        }
      } else if (response == PairingResponse::Rejected) {
        state_.pairingRejected = true;
        connectRequested_ = false;
        if (client_ != nullptr) {
          client_->disconnect();
        }
      }
    } else if (notification.kind == kPairingInfoNotification &&
               state_.phase == State::Phase::PostPairSetup) {
      if (postPairStep_ < sizeof(kPostPairCommands) &&
          isPostPairResponse(kPostPairCommands[postPairStep_],
                             notification.data, notification.len)) {
        ++postPairStep_;
        if (!sendPostPairCommand() && client_ != nullptr) {
          client_->disconnect();
        }
      }
    } else if (notification.kind == kModeNotification) {
      const ModeEvent event =
          parseModeEvent(notification.data, notification.len);
      if (state_.phase == State::Phase::OpeningSession &&
          event == ModeEvent::SessionReady) {
        readInitialRecordingState();
        markReady();
      } else if (state_.phase == State::Phase::PoweringOff &&
                 event == ModeEvent::Acknowledged) {
        phaseDeadlineMs_ = millis() + 1000;
      }
    } else if (notification.kind == kShootingNotification) {
      reduceRecordNotification(state_, notification.data, notification.len);
    }
  }
}

void CanonBleClient::queueNotification(uint8_t kind, const uint8_t* data,
                                       size_t len) {
  if (notifyQueue_ == nullptr || data == nullptr || len == 0) {
    return;
  }
  Notification notification;
  notification.kind = kind;
  notification.len = static_cast<uint8_t>(
      len < sizeof(notification.data) ? len : sizeof(notification.data));
  std::memcpy(notification.data, data, notification.len);
  xQueueSend(static_cast<QueueHandle_t>(notifyQueue_), &notification, 0);
}

bool CanonBleClient::writeCommand(
    NimBLERemoteCharacteristic* characteristic,
    const CommandBytes& command) {
  return characteristic != nullptr && command.len > 0 &&
         characteristic->writeValue(
             command.bytes, command.len,
             !characteristic->canWriteNoResponse());
}

void CanonBleClient::markReady() {
  if (state_.link == Link::Connected) {
    state_.phase = State::Phase::Ready;
  }
}

void CanonBleClient::scheduleRetry(uint32_t delayMs) {
  retryAtMs_ = millis() + delayMs;
}

void CanonBleClient::onScanMatch(const NimBLEAdvertisedDevice* device) {
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

void CanonBleClient::onLinkConnected() { connectedFlag_ = true; }

void CanonBleClient::onConnectFailed() { connectFailedFlag_ = true; }

void CanonBleClient::onLinkDisconnected() { disconnectedFlag_ = true; }

void CanonBleClient::onSecurityComplete(bool succeeded) {
  securitySucceeded_ = succeeded;
  securityCompleteFlag_ = true;
}

void CanonBleClient::onPairingNotification(const uint8_t* data, size_t len) {
  queueNotification(kPairingNotification, data, len);
}

void CanonBleClient::onPairingInfoNotification(const uint8_t* data,
                                               size_t len) {
  queueNotification(kPairingInfoNotification, data, len);
}

void CanonBleClient::onModeNotification(const uint8_t* data, size_t len) {
  queueNotification(kModeNotification, data, len);
}

void CanonBleClient::onShootingNotification(const uint8_t* data, size_t len) {
  queueNotification(kShootingNotification, data, len);
}

}  // namespace canon_ble
