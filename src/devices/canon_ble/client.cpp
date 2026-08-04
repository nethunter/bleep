#include "devices/canon_ble/client.h"

#include <Arduino.h>
#include <NimBLEDevice.h>

#include <cstring>
#include <string>

#include "devices/canon_ble/protocol.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#if ARDUINO_USB_CDC_ON_BOOT
#define CANON_LOG Serial0
#else
#define CANON_LOG Serial
#endif

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

bool hasCanonManufacturerData(const NimBLEAdvertisedDevice* device) {
  if (device == nullptr || !device->haveManufacturerData()) {
    return false;
  }
  const uint8_t count = device->getManufacturerDataCount();
  for (uint8_t i = 0; i < count; ++i) {
    const std::string data = device->getManufacturerData(i);
    if (data.size() < 2) {
      continue;
    }
    const uint16_t companyId = static_cast<uint8_t>(data[0]) |
                               (static_cast<uint16_t>(static_cast<uint8_t>(data[1]))
                                << 8);
    // Bluetooth SIG company ID 0x01A9 = Canon Inc.
    if (companyId == 0x01A9u) {
      return true;
    }
  }
  return false;
}

bool isCanonAdvertisement(const NimBLEAdvertisedDevice* device) {
  if (device == nullptr) {
    return false;
  }
  // Require Camera Connect advertising evidence. Name-only matches latch onto
  // nearby bodies that are merely discoverable (e.g. another EOS advertising
  // while a second body is in Add-a-device mode).
  if (device->isAdvertisingService(kHandshakeService)) {
    return true;
  }
  return hasCanonManufacturerData(device);
}

class ScanCallbacks : public NimBLEScanCallbacks {
 public:
  void onResult(const NimBLEAdvertisedDevice* device) override {
    if (device == nullptr || gCallbackClient == nullptr) {
      return;
    }
    if (!isCanonAdvertisement(device)) {
      return;
    }
    gCallbackClient->onScanMatch(device);
  }
};

class ClientCallbacks : public NimBLEClientCallbacks {
 public:
  void onConnect(NimBLEClient*) override {
    if (gCallbackClient != nullptr) {
      gCallbackClient->onLinkConnected();
    }
  }
  void onConnectFail(NimBLEClient*, int reason) override {
    if (gCallbackClient != nullptr) {
      gCallbackClient->onConnectFailed(reason);
    }
  }
  void onDisconnect(NimBLEClient*, int reason) override {
    if (gCallbackClient != nullptr) {
      gCallbackClient->onLinkDisconnected(reason);
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
  lockedAddr_[0] = '\0';
  targetAddrType_ = addressType;
  if (haveTarget_) {
    std::strncpy(targetAddr_, address, sizeof(targetAddr_) - 1);
    targetAddr_[sizeof(targetAddr_) - 1] = '\0';
    std::strncpy(lockedAddr_, address, sizeof(lockedAddr_) - 1);
    lockedAddr_[sizeof(lockedAddr_) - 1] = '\0';
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
  clearIgnoredAddresses();
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
    CANON_LOG.printf("canon connect fail reason=%d fails=%d addr=%s\n",
                  lastConnectFailReason_, connectFails_, targetAddr_);
    resetTransientState(state_);
    state_.link = Link::Disconnected;
    scheduleRetry(1500u * (connectFails_ < 4 ? connectFails_ : 4));
  }
  if (disconnectedFlag_) {
    disconnectedFlag_ = false;
    CANON_LOG.printf("canon disconnect reason=%d phase=%d link=%d addr=%s\n",
                  lastDisconnectReason_, static_cast<int>(state_.phase),
                  static_cast<int>(state_.link), targetAddr_);
    // A drop while still bonding/connecting counts toward scan fallback.
    if (state_.link == Link::Connecting ||
        state_.phase == State::Phase::Bonding ||
        state_.phase == State::Phase::AwaitingConfirmation ||
        state_.phase == State::Phase::Handshaking ||
        state_.phase == State::Phase::PostPairSetup ||
        state_.phase == State::Phase::OpeningSession) {
      ++connectFails_;
      // Remote terminate during bonding (HCI 0x13 → 531): skip that body so a
      // second camera in Add-a-device mode can be found. Keep retrying on local
      // setup failures.
      if (newHandshake_ && lastDisconnectReason_ == 531 &&
          targetAddr_[0] != '\0') {
        ignoreAddress(targetAddr_);
        haveTarget_ = false;
        targetAddr_[0] = '\0';
      }
    }
    handleDisconnect();
  }
  if (connectedFlag_) {
    connectedFlag_ = false;
    CANON_LOG.printf("canon connected encrypted=%d addr=%s name=%s\n",
                  client_ != nullptr && client_->getConnInfo().isEncrypted()
                      ? 1
                      : 0,
                  targetAddr_, targetName_);
    if (client_ != nullptr && client_->getConnInfo().isEncrypted()) {
      securityFails_ = 0;
      setupPending_ = true;
      setupAtMs_ = millis() + 100;
    } else if (client_ == nullptr || !client_->secureConnection(true)) {
      CANON_LOG.println("canon secureConnection start failed");
      handleSecurityFailure();
    } else {
      CANON_LOG.println("canon secureConnection started");
    }
  }
  if (securityCompleteFlag_) {
    securityCompleteFlag_ = false;
    CANON_LOG.printf("canon security complete ok=%d\n", securitySucceeded_ ? 1 : 0);
    if (!securitySucceeded_) {
      handleSecurityFailure();
    } else {
      securityFails_ = 0;
      setupPending_ = true;
      setupAtMs_ = millis() + 100;
    }
  }

  drainNotifications();

  if (claimedPeerSeen_) {
    claimedPeerSeen_ = false;
    if (state_.link == Link::Scanning && newHandshake_) {
      state_.claimedPeerVisible = true;
    }
  }

  if (connectRequested_ && scanHit_) {
    scanHit_ = false;
    if (isIgnoredAddress(scanHitAddr_)) {
      CANON_LOG.printf("canon scan skip ignored addr=%s\n", scanHitAddr_);
    } else if (lockedAddr_[0] != '\0' &&
               std::strcmp(scanHitAddr_, lockedAddr_) != 0) {
      // Bonded instance: never adopt a different nearby Canon body.
      CANON_LOG.printf("canon scan skip non-locked addr=%s locked=%s\n",
                       scanHitAddr_, lockedAddr_);
    } else if (newHandshake_) {
      state_.claimedPeerVisible = false;
      considerScanCandidate(scanHitAddr_, scanHitType_, scanHitName_,
                            scanHitHasService_, scanHitHasMfg_);
    } else {
      NimBLEDevice::getScan()->stop();
      scanActive_ = false;
      std::strncpy(targetAddr_, scanHitAddr_, sizeof(targetAddr_) - 1);
      targetAddr_[sizeof(targetAddr_) - 1] = '\0';
      targetAddrType_ = scanHitType_;
      if (scanHitName_[0] != '\0') {
        std::strncpy(targetName_, scanHitName_, sizeof(targetName_) - 1);
        targetName_[sizeof(targetName_) - 1] = '\0';
      }
      haveTarget_ = true;
      connectFails_ = 0;
      CANON_LOG.printf("canon scan hit addr=%s type=%u name=%s svc=%d mfg=%d\n",
                    targetAddr_, static_cast<unsigned>(targetAddrType_),
                    targetName_, scanHitHasService_ ? 1 : 0,
                    scanHitHasMfg_ ? 1 : 0);
      beginConnect();
    }
  }

  const uint32_t now = millis();
  if (connectRequested_ && newHandshake_ && scanCandidatePending_ &&
      static_cast<int32_t>(now - scanDwellUntilMs_) >= 0) {
    NimBLEDevice::getScan()->stop();
    scanActive_ = false;
    scanCandidatePending_ = false;
    std::strncpy(targetAddr_, scanCandidateAddr_, sizeof(targetAddr_) - 1);
    targetAddr_[sizeof(targetAddr_) - 1] = '\0';
    targetAddrType_ = scanCandidateType_;
    std::strncpy(targetName_, scanCandidateName_, sizeof(targetName_) - 1);
    targetName_[sizeof(targetName_) - 1] = '\0';
    haveTarget_ = true;
    connectFails_ = 0;
    CANON_LOG.printf("canon scan choose addr=%s type=%u name=%s score=%d svc=%d mfg=%d\n",
                     targetAddr_, static_cast<unsigned>(targetAddrType_),
                     targetName_, scanCandidateScore_,
                     scanCandidateHasService_ ? 1 : 0,
                     scanCandidateHasMfg_ ? 1 : 0);
    beginConnect();
  }
  if (connectRequested_ && state_.link == Link::Connecting &&
      connectWatchdogMs_ != 0 &&
      static_cast<int32_t>(now - connectWatchdogMs_) >= 0) {
    CANON_LOG.printf("canon connect watchdog addr=%s\n", targetAddr_);
    if (newHandshake_ && lockedAddr_[0] == '\0' && targetAddr_[0] != '\0') {
      ignoreAddress(targetAddr_);
      haveTarget_ = false;
      targetAddr_[0] = '\0';
    }
    ++connectFails_;
    if (client_ != nullptr) {
      client_->cancelConnect();
      if (client_->isConnected()) {
        client_->disconnect();
      }
    }
    connectWatchdogMs_ = 0;
    resetTransientState(state_);
    state_.link = Link::Disconnected;
    scheduleRetry(500);
  }
  if (connectRequested_ && setupPending_ &&
      static_cast<int32_t>(now - setupAtMs_) >= 0) {
    setupPending_ = false;
    if (client_ == nullptr || !client_->isConnected()) {
      CANON_LOG.println("canon setup aborted: not connected");
      return;
    }
    if (!completeConnect()) {
      CANON_LOG.printf("canon setup failed newHandshake=%d addr=%s\n",
                    newHandshake_ ? 1 : 0, targetAddr_);
      ++connectFails_;
      client_->disconnect();
      return;
    }
    CANON_LOG.printf("canon setup ok phase=%d\n", static_cast<int>(state_.phase));
  }

  if (connectRequested_ &&
      (state_.phase == State::Phase::AwaitingConfirmation ||
       state_.phase == State::Phase::PostPairSetup ||
       state_.phase == State::Phase::OpeningSession) &&
      static_cast<int32_t>(now - phaseDeadlineMs_) >= 0) {
    if (state_.phase == State::Phase::OpeningSession &&
        modeCommandChar_ != nullptr && !openingShootRequested_) {
      // Some bodies answer wake 03 with silence until shooting mode 02.
      openingShootRequested_ = true;
      phaseDeadlineMs_ = now + 5000;
      CANON_LOG.println("canon core: wake timeout; trying shooting mode 02");
      writeCommand(modeCommandChar_, buildModeCommand(0x02));
      return;
    }
    CANON_LOG.printf("canon phase timeout phase=%d\n",
                     static_cast<int>(state_.phase));
    // Prevent the deadline from re-firing every loop tick before disconnect
    // is observed.
    phaseDeadlineMs_ = now + 60000;
    if (state_.phase == State::Phase::AwaitingConfirmation && newHandshake_ &&
        targetAddr_[0] != '\0') {
      ignoreAddress(targetAddr_);
      haveTarget_ = false;
      targetAddr_[0] = '\0';
      CANON_LOG.println("canon confirmation timeout; trying next camera");
    }
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
    if (lockedAddr_[0] != '\0') {
      // Per-instance bonded reconnect: never open-scan other Canon bodies.
      if (targetAddr_[0] == '\0') {
        std::strncpy(targetAddr_, lockedAddr_, sizeof(targetAddr_) - 1);
        targetAddr_[sizeof(targetAddr_) - 1] = '\0';
      }
      haveTarget_ = true;
      beginConnect();
    } else if (haveTarget_ && connectFails_ < 1) {
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
  lockedAddr_[0] = '\0';
  clearIgnoredAddresses();
  state_.hasSavedDevice = false;
  resetTransientState(state_);
  beginScan();
}

void CanonBleClient::forgetDevice() {
  forgetBond(targetAddr_, targetAddrType_);
  haveTarget_ = false;
  targetAddr_[0] = '\0';
  targetName_[0] = '\0';
  lockedAddr_[0] = '\0';
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
  scanCandidatePending_ = false;
  scanCandidateScore_ = -1;
  scanCandidateAddr_[0] = '\0';
  scanCandidateName_[0] = '\0';
  scanDwellUntilMs_ = 0;
  connectWatchdogMs_ = 0;
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
  connectWatchdogMs_ = millis() + 6000;
  CANON_LOG.printf("canon beginConnect addr=%s type=%u name=%s new=%d fails=%d\n",
                targetAddr_, static_cast<unsigned>(targetAddrType_),
                targetName_, newHandshake_ ? 1 : 0, connectFails_);
  if (newHandshake_ && targetAddr_[0] != '\0') {
    // Drop a stale local bond so the camera can show a fresh confirmation.
    forgetBond(targetAddr_, targetAddrType_);
  }
  if (client_->connect(address, true, true, true)) {
    return;
  }
  connectWatchdogMs_ = 0;
  ++connectFails_;
  CANON_LOG.printf("canon beginConnect sync fail fails=%d\n", connectFails_);
  state_.link = Link::Disconnected;
  scheduleRetry(1500u * (connectFails_ < 4 ? connectFails_ : 4));
}

bool CanonBleClient::completeConnect() {
  if (client_ == nullptr) {
    CANON_LOG.println("canon setup: no client");
    return false;
  }
  if (!client_->discoverAttributes()) {
    CANON_LOG.println("canon setup: discoverAttributes failed");
    return false;
  }
  NimBLERemoteService* handshake = client_->getService(kHandshakeService);
  if (handshake == nullptr) {
    CANON_LOG.println("canon setup: handshake service missing");
    return false;
  }
  pairingCommandChar_ =
      handshake->getCharacteristic(kPairingCommandCharacteristic);
  pairingDataChar_ = handshake->getCharacteristic(kPairingDataCharacteristic);
  pairingInfoChar_ = handshake->getCharacteristic(kPairingInfoCharacteristic);
  // Pairing-info (0001000c) is present on R6 III Camera Connect captures but
  // absent on EOS R6 Mark II; initial handshake only needs command + data.
  if (pairingCommandChar_ == nullptr || pairingDataChar_ == nullptr) {
    CANON_LOG.printf("canon setup: pairing chars missing cmd=%d data=%d info=%d\n",
                     pairingCommandChar_ != nullptr ? 1 : 0,
                     pairingDataChar_ != nullptr ? 1 : 0,
                     pairingInfoChar_ != nullptr ? 1 : 0);
    return false;
  }
  if (pairingInfoChar_ == nullptr) {
    CANON_LOG.println("canon setup: pairing-info char absent; skipping post-pair");
    for (NimBLERemoteCharacteristic* characteristic :
         handshake->getCharacteristics(false)) {
      if (characteristic == nullptr) {
        continue;
      }
      CANON_LOG.printf("canon handshake char uuid=%s\n",
                       characteristic->getUUID().toString().c_str());
    }
  }

  if (!newHandshake_) {
    if (!subscribePairingInfo()) {
      CANON_LOG.println("canon setup: pairing-info subscribe failed");
      return false;
    }
    if (!writeCommand(pairingDataChar_, buildHandshakeFinish())) {
      CANON_LOG.println("canon setup: bonded finish write failed");
      return false;
    }
    return beginPostPairSetup();
  }

  if (!writeCommand(pairingCommandChar_,
                    buildHandshakeRequest("StudioRemote"))) {
    CANON_LOG.println("canon setup: handshake request write failed");
    return false;
  }
  const bool pairingSubscribed =
      pairingCommandChar_->canIndicate()
          ? pairingCommandChar_->subscribe(false, pairingNotifyTrampoline, true)
          : pairingCommandChar_->subscribe(true, pairingNotifyTrampoline, true);
  if (!pairingSubscribed) {
    CANON_LOG.printf("canon setup: pairing subscribe failed indicate=%d notify=%d\n",
                     pairingCommandChar_->canIndicate() ? 1 : 0,
                     pairingCommandChar_->canNotify() ? 1 : 0);
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
    CANON_LOG.println("canon finish handshake failed");
    return false;
  }
  newHandshake_ = false;
  std::strncpy(lockedAddr_, targetAddr_, sizeof(lockedAddr_) - 1);
  lockedAddr_[sizeof(lockedAddr_) - 1] = '\0';
  pairingChanged_ = true;
  return beginPostPairSetup();
}

bool CanonBleClient::subscribePairingInfo() {
  if (pairingInfoChar_ == nullptr) {
    return true;
  }
  return pairingInfoChar_->canIndicate()
             ? pairingInfoChar_->subscribe(false, pairingInfoNotifyTrampoline,
                                           true)
             : pairingInfoChar_->subscribe(true, pairingInfoNotifyTrampoline,
                                           true);
}

bool CanonBleClient::beginPostPairSetup() {
  if (pairingInfoChar_ == nullptr) {
    CANON_LOG.println("canon post-pair skipped; opening core session");
    return openCoreSession();
  }
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
  if (client_ == nullptr) {
    CANON_LOG.println("canon core: no client");
    return false;
  }
  // Re-discover after handshake; some bodies expose 00030000 only once paired.
  if (!client_->discoverAttributes()) {
    CANON_LOG.println("canon core: rediscover failed");
    return false;
  }
  NimBLERemoteService* core = client_->getService(kCoreService);
  if (core == nullptr) {
    CANON_LOG.println("canon core: service 00030000 missing");
    for (NimBLERemoteService* service : client_->getServices(false)) {
      if (service != nullptr) {
        CANON_LOG.printf("canon service uuid=%s\n",
                         service->getUUID().toString().c_str());
      }
    }
    return false;
  }
  modeCommandChar_ = core->getCharacteristic(kModeCommandCharacteristic);
  modeResultChar_ = core->getCharacteristic(kModeResultCharacteristic);
  shootingCommandChar_ =
      core->getCharacteristic(kShootingCommandCharacteristic);
  shootingStateChar_ =
      core->getCharacteristic(kShootingStateCharacteristic);
  if (modeCommandChar_ == nullptr || modeResultChar_ == nullptr ||
      shootingCommandChar_ == nullptr || shootingStateChar_ == nullptr) {
    CANON_LOG.printf(
        "canon core: chars missing modeCmd=%d modeRes=%d shootCmd=%d shootSt=%d\n",
        modeCommandChar_ != nullptr ? 1 : 0, modeResultChar_ != nullptr ? 1 : 0,
        shootingCommandChar_ != nullptr ? 1 : 0,
        shootingStateChar_ != nullptr ? 1 : 0);
    for (NimBLERemoteCharacteristic* characteristic :
         core->getCharacteristics(false)) {
      if (characteristic != nullptr) {
        CANON_LOG.printf("canon core char uuid=%s\n",
                         characteristic->getUUID().toString().c_str());
      }
    }
    return false;
  }
  const bool modeSubscribed =
      modeResultChar_->canIndicate()
          ? modeResultChar_->subscribe(false, modeNotifyTrampoline, true)
          : modeResultChar_->subscribe(true, modeNotifyTrampoline, true);
  const bool shootingSubscribed =
      shootingStateChar_->canIndicate()
          ? shootingStateChar_->subscribe(false, shootingNotifyTrampoline, true)
          : shootingStateChar_->subscribe(true, shootingNotifyTrampoline, true);
  if (!modeSubscribed || !shootingSubscribed) {
    CANON_LOG.printf(
        "canon core: subscribe failed mode=%d(ind=%d ntf=%d) shoot=%d(ind=%d ntf=%d)\n",
        modeSubscribed ? 1 : 0, modeResultChar_->canIndicate() ? 1 : 0,
        modeResultChar_->canNotify() ? 1 : 0, shootingSubscribed ? 1 : 0,
        shootingStateChar_->canIndicate() ? 1 : 0,
        shootingStateChar_->canNotify() ? 1 : 0);
    return false;
  }

  resetTransientState(state_);
  state_.link = Link::Connected;
  state_.phase = State::Phase::OpeningSession;
  state_.hasSavedDevice = true;
  openingShootRequested_ = false;
  std::strncpy(state_.deviceName, targetName_, sizeof(state_.deviceName) - 1);
  state_.deviceName[sizeof(state_.deviceName) - 1] = '\0';
  haveTarget_ = true;
  if (lockedAddr_[0] == '\0' && targetAddr_[0] != '\0') {
    std::strncpy(lockedAddr_, targetAddr_, sizeof(lockedAddr_) - 1);
    lockedAddr_[sizeof(lockedAddr_) - 1] = '\0';
    pairingChanged_ = true;
  }
  connectFails_ = 0;
  // R6 II can be slower to emit the shooting-ready mode result after wake.
  phaseDeadlineMs_ = millis() + 8000;
  CANON_LOG.printf("canon core: wake 03 addr=%s locked=%s\n", targetAddr_,
                   lockedAddr_);
  if (!writeCommand(modeCommandChar_, buildModeCommand(kWakeMode))) {
    CANON_LOG.println("canon core: wake write failed");
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
    // Keep lockedAddr_ so rediscovery cannot attach a sibling body.
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
        CANON_LOG.printf("canon pairing confirmed addr=%s\n", targetAddr_);
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
      CANON_LOG.printf("canon mode notify len=%u byte0=%02x phase=%d\n",
                       static_cast<unsigned>(notification.len),
                       notification.len > 0 ? notification.data[0] : 0,
                       static_cast<int>(state_.phase));
      const ModeEvent event =
          parseModeEvent(notification.data, notification.len);
      if (state_.phase == State::Phase::OpeningSession &&
          event == ModeEvent::SessionReady) {
        CANON_LOG.println("canon session ready");
        readInitialRecordingState();
        markReady();
      } else if (state_.phase == State::Phase::OpeningSession &&
                 event == ModeEvent::None && notification.len == 1 &&
                 notification.data[0] == 0x03) {
        // Playback-ready on some bodies: request shooting mode explicitly.
        CANON_LOG.println("canon mode 03 (playback); requesting shooting 02");
        writeCommand(modeCommandChar_, buildModeCommand(0x02));
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

void CanonBleClient::clearIgnoredAddresses() {
  ignoredCount_ = 0;
  for (size_t i = 0; i < kMaxIgnoredAddresses; ++i) {
    ignoredAddrs_[i][0] = '\0';
  }
}

bool CanonBleClient::isIgnoredAddress(const char* address) const {
  if (address == nullptr || address[0] == '\0') {
    return false;
  }
  for (uint8_t i = 0; i < ignoredCount_; ++i) {
    if (std::strcmp(ignoredAddrs_[i], address) == 0) {
      return true;
    }
  }
  return false;
}

void CanonBleClient::ignoreAddress(const char* address) {
  if (address == nullptr || address[0] == '\0' || isIgnoredAddress(address)) {
    return;
  }
  if (ignoredCount_ >= kMaxIgnoredAddresses) {
    // Drop the oldest entry so a newly rejecting body can still be skipped.
    for (size_t i = 1; i < kMaxIgnoredAddresses; ++i) {
      std::strncpy(ignoredAddrs_[i - 1], ignoredAddrs_[i],
                   sizeof(ignoredAddrs_[0]) - 1);
      ignoredAddrs_[i - 1][sizeof(ignoredAddrs_[0]) - 1] = '\0';
    }
    ignoredCount_ = kMaxIgnoredAddresses - 1;
  }
  std::strncpy(ignoredAddrs_[ignoredCount_], address,
               sizeof(ignoredAddrs_[0]) - 1);
  ignoredAddrs_[ignoredCount_][sizeof(ignoredAddrs_[0]) - 1] = '\0';
  ++ignoredCount_;
  CANON_LOG.printf("canon ignore addr=%s count=%u\n", address,
                   static_cast<unsigned>(ignoredCount_));
}

int CanonBleClient::candidateScore(const char* name, bool hasService,
                                   bool hasMfg) const {
  int score = 0;
  if (hasService) {
    score += 40;
  }
  if (hasMfg) {
    score += 20;
  }
  if (name != nullptr && name[0] != '\0') {
    score += 30;
  }
  return score;
}

void CanonBleClient::ignorePeerAddress(const char* address) {
  ignoreAddress(address);
}

void CanonBleClient::considerScanCandidate(const char* address,
                                           uint8_t addressType,
                                           const char* name, bool hasService,
                                           bool hasMfg) {
  if (address == nullptr || address[0] == '\0') {
    return;
  }
  const int score = candidateScore(name, hasService, hasMfg);
  CANON_LOG.printf("canon scan candidate addr=%s name=%s score=%d svc=%d mfg=%d\n",
                   address, name != nullptr ? name : "", score,
                   hasService ? 1 : 0, hasMfg ? 1 : 0);
  if (!scanCandidatePending_ || score > scanCandidateScore_) {
    std::strncpy(scanCandidateAddr_, address, sizeof(scanCandidateAddr_) - 1);
    scanCandidateAddr_[sizeof(scanCandidateAddr_) - 1] = '\0';
    scanCandidateType_ = addressType;
    if (name != nullptr) {
      std::strncpy(scanCandidateName_, name, sizeof(scanCandidateName_) - 1);
      scanCandidateName_[sizeof(scanCandidateName_) - 1] = '\0';
    } else {
      scanCandidateName_[0] = '\0';
    }
    scanCandidateHasService_ = hasService;
    scanCandidateHasMfg_ = hasMfg;
    scanCandidateScore_ = score;
  }
  if (!scanCandidatePending_) {
    scanCandidatePending_ = true;
    scanDwellUntilMs_ = millis() + 3000;
  }
}

void CanonBleClient::onScanMatch(const NimBLEAdvertisedDevice* device) {
  if (device == nullptr || scanHit_) {
    return;
  }
  const std::string address = device->getAddress().toString();
  if (isIgnoredAddress(address.c_str())) {
    claimedPeerSeen_ = true;
    return;
  }
  if (lockedAddr_[0] != '\0' && address != lockedAddr_) {
    return;
  }
  const std::string name = device->getName();
  std::strncpy(scanHitAddr_, address.c_str(), sizeof(scanHitAddr_) - 1);
  scanHitAddr_[sizeof(scanHitAddr_) - 1] = '\0';
  scanHitType_ = device->getAddress().getType();
  std::strncpy(scanHitName_, name.c_str(), sizeof(scanHitName_) - 1);
  scanHitName_[sizeof(scanHitName_) - 1] = '\0';
  scanHitHasService_ = device->isAdvertisingService(kHandshakeService);
  scanHitHasMfg_ = hasCanonManufacturerData(device);
  scanHit_ = true;
}

void CanonBleClient::onLinkConnected() {
  connectWatchdogMs_ = 0;
  connectedFlag_ = true;
}

void CanonBleClient::onConnectFailed(int reason) {
  connectWatchdogMs_ = 0;
  lastConnectFailReason_ = reason;
  connectFailedFlag_ = true;
}

void CanonBleClient::onLinkDisconnected(int reason) {
  connectWatchdogMs_ = 0;
  lastDisconnectReason_ = reason;
  disconnectedFlag_ = true;
}

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
