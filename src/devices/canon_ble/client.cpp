#include "devices/canon_ble/client.h"

#include <Arduino.h>
#include <NimBLEDevice.h>

#include <cstring>
#include <string>

#include "core/ble/ble_runtime.h"
#include "core/ble/ble_timing.h"
#include "devices/canon_ble/ble_match.h"
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

constexpr size_t kMaxNotifyClients = 3;
CanonBleClient* gNotifyClients[kMaxNotifyClients] = {};

bool registerNotifyClient(CanonBleClient* client) {
  for (CanonBleClient*& slot : gNotifyClients) {
    if (slot == client) return true;
    if (slot == nullptr) {
      slot = client;
      return true;
    }
  }
  return false;
}

void unregisterNotifyClient(CanonBleClient* client) {
  for (CanonBleClient*& slot : gNotifyClients) {
    if (slot == client) slot = nullptr;
  }
}

CanonBleClient* notifyClientFor(
    NimBLERemoteCharacteristic* characteristic) {
  for (CanonBleClient* client : gNotifyClients) {
    if (client != nullptr &&
        client->ownsNotifyCharacteristic(characteristic)) {
      return client;
    }
  }
  return nullptr;
}

void pairingNotifyTrampoline(NimBLERemoteCharacteristic* characteristic,
                             uint8_t* data, size_t length, bool) {
  CanonBleClient* client = notifyClientFor(characteristic);
  if (client != nullptr) {
    client->onPairingNotification(data, length);
  }
}

void pairingInfoNotifyTrampoline(NimBLERemoteCharacteristic* characteristic,
                                 uint8_t* data, size_t length, bool) {
  CanonBleClient* client = notifyClientFor(characteristic);
  if (client != nullptr) {
    client->onPairingInfoNotification(data, length);
  }
}

void modeNotifyTrampoline(NimBLERemoteCharacteristic* characteristic,
                          uint8_t* data, size_t length, bool) {
  CanonBleClient* client = notifyClientFor(characteristic);
  if (client != nullptr) {
    client->onModeNotification(data, length);
  }
}

void shootingNotifyTrampoline(NimBLERemoteCharacteristic* characteristic,
                              uint8_t* data, size_t length, bool) {
  CanonBleClient* client = notifyClientFor(characteristic);
  if (client != nullptr) {
    client->onShootingNotification(data, length);
  }
}

}  // namespace

namespace {
uint16_t gRetryBackoffCapMs = 1500;
}  // namespace

void setRetryBackoffCapForDebug(uint16_t capMs) { gRetryBackoffCapMs = capMs; }

bool CanonBleClient::begin() {
  if (initialized_) {
    return true;
  }
  if (notifyQueue_ == nullptr) {
    notifyQueue_ = xQueueCreate(8, sizeof(Notification));
  }
  if (notifyQueue_ == nullptr) return false;
  studio::ble::ConnectPolicy policy;
  policy.security = studio::ble::SecurityPolicy::BondNoMitm;
  // Every successful establishment in the captures completed within 2.1 s
  // and every wake failure surfaced within 2.8 s; a body that stays silent
  // for 4 s is off or asleep, and while the initiator waits no other link can
  // connect (ADR-021), so the recorder queued behind it paid the full 4 s.
  policy.connectTimeoutMs = 2500;
  policy.connectWatchdogMs = 4000;
  policy.directAttemptsBeforeScan = 1;
  policy.alwaysDirect = lockedAddr_[0] != '\0';
  // A just-woken body answers the first pokes with HCI 0x3E and then stays
  // silent for a fixed 7-9 s regardless of how long the panel waits, so the
  // growing backoff only delays the attempt that finally succeeds.
  policy.retryBackoffCapMs = gRetryBackoffCapMs;
  policy.diagnosticTag = "canon_smart";
  linkHandle_ = studio::ble::bleCentral().acquire(*this, policy);
  initialized_ = linkHandle_ != studio::ble::kInvalidLinkHandle;
  if (initialized_ && registerNotifyClient(this)) {
    return true;
  }
  if (initialized_) {
    studio::ble::bleCentral().release(linkHandle_);
    linkHandle_ = studio::ble::kInvalidLinkHandle;
    initialized_ = false;
  }
  vQueueDelete(static_cast<QueueHandle_t>(notifyQueue_));
  notifyQueue_ = nullptr;
  return false;
}

bool CanonBleClient::activate(const char* address, uint8_t addressType,
                              const char* name, bool paired) {
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
  if (!begin()) return false;
  resetTransientState(state_);
  state_.hasSavedDevice = haveTarget_;
  std::strncpy(state_.deviceName, targetName_, sizeof(state_.deviceName) - 1);
  state_.deviceName[sizeof(state_.deviceName) - 1] = '\0';
  securityFails_ = 0;
  remoteRejections_ = 0;
  setupRetries_ = 0;
  bondRecoveryPending_ = false;
  clearIgnoredAddresses();
  if (haveTarget_) {
    beginConnect();
  } else {
    beginScan();
  }
  return true;
}

void CanonBleClient::deactivate() {
  connectRequested_ = false;
  studio::ble::bleCentral().release(linkHandle_);
  linkHandle_ = studio::ble::kInvalidLinkHandle;
  initialized_ = false;
  client_ = nullptr;
  unregisterNotifyClient(this);
  startRequested_ = false;
  stopRequested_ = false;
  powerOffRequested_ = false;
  setupPending_ = false;
  bondRecoveryPending_ = false;
  postPairStep_ = 0;
  if (notifyQueue_ != nullptr) {
    vQueueDelete(static_cast<QueueHandle_t>(notifyQueue_));
    notifyQueue_ = nullptr;
  }
  resetTransientState(state_);
  state_.link = Link::Disconnected;
}

void CanonBleClient::loop() {
  drainNotifications();

  const uint32_t now = millis();
  if (connectRequested_ && state_.link == Link::Disconnected) {
    const studio::ble::LinkPhase phase =
        studio::ble::bleCentral().phase(linkHandle_);
    if (phase == studio::ble::LinkPhase::Scanning) {
      state_.link = Link::Scanning;
    } else if (phase == studio::ble::LinkPhase::Connecting) {
      state_.link = Link::Connecting;
      state_.phase = State::Phase::Bonding;
    }
    // Between wake pokes the link is genuinely down, but the operator is
    // watching a connection in progress, not a disconnect.
    state_.retryPending =
        haveTarget_ && (phase == studio::ble::LinkPhase::WaitingRetry ||
                        phase == studio::ble::LinkPhase::WaitingConnect);
  } else {
    state_.retryPending = false;
  }
  if (connectRequested_ && newHandshake_ && scanCandidatePending_ &&
      static_cast<int32_t>(now - scanDwellUntilMs_) >= 0) {
    scanCandidatePending_ = false;
    std::strncpy(targetAddr_, scanCandidate_.address.value,
                 sizeof(targetAddr_) - 1);
    targetAddr_[sizeof(targetAddr_) - 1] = '\0';
    targetAddrType_ = scanCandidate_.address.type;
    std::strncpy(targetName_, scanCandidateName_, sizeof(targetName_) - 1);
    targetName_[sizeof(targetName_) - 1] = '\0';
    haveTarget_ = true;
    CANON_LOG.printf("canon scan choose addr=%s type=%u name=%s score=%d svc=%d mfg=%d\n",
                     targetAddr_, static_cast<unsigned>(targetAddrType_),
                     targetName_, scanCandidateScore_,
                     scanCandidateHasService_ ? 1 : 0,
                     scanCandidateHasMfg_ ? 1 : 0);
    resetTransientState(state_);
    state_.link = Link::Connecting;
    state_.phase = State::Phase::Bonding;
    setupPending_ = false;
    powerOffRequested_ = false;
    postPairStep_ = 0;
    forgetBond(targetAddr_, targetAddrType_);
    if (!studio::ble::bleCentral().selectAdvertisement(linkHandle_,
                                                        scanCandidate_)) {
      haveTarget_ = false;
      targetAddr_[0] = '\0';
      beginScan();
    }
  }
  if (connectRequested_ && setupPending_ &&
      static_cast<int32_t>(now - setupAtMs_) >= 0) {
    setupPending_ = false;
    if (client_ == nullptr || !client_->isConnected()) {
      CANON_LOG.println("canon setup aborted: not connected");
      return;
    }
    if (!completeConnect()) {
      constexpr uint8_t kSetupRetryLimit = 2;
      if (!newHandshake_ && haveTarget_ && setupRetries_ < kSetupRetryLimit) {
        // Saved body accepted the link and bonded but its GATT server never
        // answered discovery (ATT timeout). Reconnect instead of parking the
        // screen on CONNECTION FAILED with a manual Retry.
        ++setupRetries_;
        CANON_LOG.printf("canon setup retry %u/%u addr=%s\n",
                         static_cast<unsigned>(setupRetries_),
                         static_cast<unsigned>(kSetupRetryLimit), targetAddr_);
        studio::ble::bleCentral().disconnect(linkHandle_, true, 1500);
        return;
      }
      CANON_LOG.printf("canon setup failed newHandshake=%d addr=%s\n",
                    newHandshake_ ? 1 : 0, targetAddr_);
      failProtocol();
      return;
    }
    setupRetries_ = 0;
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
    studio::ble::bleCentral().markProtocolFailed(linkHandle_);
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

}

void CanonBleClient::retry() {
  remoteRejections_ = 0;
  setupRetries_ = 0;
  state_.pairingRejected = false;
  if (newHandshake_ || targetAddr_[0] == '\0') {
    startScan();
    return;
  }
  connectRequested_ = true;
  resetTransientState(state_);
  beginConnect();
}

void CanonBleClient::startScan() {
  connectRequested_ = true;
  if (initialized_) {
    studio::ble::bleCentral().release(linkHandle_);
    linkHandle_ = studio::ble::kInvalidLinkHandle;
    initialized_ = false;
    client_ = nullptr;
  }
  setupPending_ = false;
  pairingCommandChar_ = nullptr;
  pairingDataChar_ = nullptr;
  pairingInfoChar_ = nullptr;
  modeCommandChar_ = nullptr;
  modeResultChar_ = nullptr;
  shootingCommandChar_ = nullptr;
  shootingStateChar_ = nullptr;
  haveTarget_ = false;
  newHandshake_ = true;
  lockedAddr_[0] = '\0';
  begin();
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
  studio::ble::Address peer;
  std::strncpy(peer.value, address, sizeof(peer.value) - 1);
  peer.value[sizeof(peer.value) - 1] = '\0';
  peer.type = addressType;
  studio::ble::bleCentral().deleteBond(peer);
}

bool CanonBleClient::startRecording() {
  if (!connected() || state_.commandPending ||
      (state_.recordingConfirmed &&
       state_.recording == State::Recording::Recording)) {
    return false;
  }
  markCommandQueued(state_, true);
  if (!writeCommand(shootingCommandChar_, buildRecordCommand(true))) {
    markCommandWriteFailed(state_);
    return false;
  }
  commandDeadlineMs_ = millis() + 6000;
  return true;
}

bool CanonBleClient::stopRecording() {
  if (!connected() || state_.commandPending) {
    return false;
  }
  if (completeStopIfAlreadyStopped(state_)) {
    return true;
  }
  markCommandQueued(state_, false);
  if (!writeCommand(shootingCommandChar_, buildRecordCommand(false))) {
    markCommandWriteFailed(state_);
    return false;
  }
  commandDeadlineMs_ = millis() + 6000;
  return true;
}

bool CanonBleClient::powerOn() {
  if (state_.link != Link::Disconnected ||
      state_.phase != State::Phase::PoweredOff) {
    return false;
  }
  connectRequested_ = true;
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
  scanCandidatePending_ = false;
  scanCandidateScore_ = -1;
  scanCandidate_ = {};
  scanCandidateName_[0] = '\0';
  scanDwellUntilMs_ = 0;
  studio::ble::bleCentral().requestScan(linkHandle_, !haveTarget_);
  state_.link = Link::Scanning;
}

void CanonBleClient::beginConnect() {
  resetTransientState(state_);
  state_.link = Link::Connecting;
  state_.phase = State::Phase::Bonding;
  setupPending_ = false;
  powerOffRequested_ = false;
  postPairStep_ = 0;
  CANON_LOG.printf("canon beginConnect addr=%s type=%u name=%s new=%d\n",
                targetAddr_, static_cast<unsigned>(targetAddrType_),
                targetName_, newHandshake_ ? 1 : 0);
  if (newHandshake_ && targetAddr_[0] != '\0') {
    // Drop a stale local bond so the camera can show a fresh confirmation.
    forgetBond(targetAddr_, targetAddrType_);
  }
  studio::ble::Address address;
  std::strncpy(address.value, targetAddr_, sizeof(address.value) - 1);
  address.value[sizeof(address.value) - 1] = '\0';
  address.type = targetAddrType_;
  studio::ble::bleCentral().requestConnect(linkHandle_, address);
}

bool CanonBleClient::completeConnect() {
  if (client_ == nullptr) {
    CANON_LOG.println("canon setup: no client");
    return false;
  }
  const uint32_t discoveryStartedMs = millis();
  NimBLERemoteService* handshake =
      client_->getService(NimBLEUUID(kHandshakeServiceUuid));
  if (handshake == nullptr) {
    CANON_LOG.println("canon setup: handshake service missing");
    studio::ble::logTiming(
        "canon_smart", linkHandle_, "gatt_handshake_discovery",
        millis() - discoveryStartedMs,
        millis() - studio::ble::bleCentral().timingStartedAt(linkHandle_),
        "failed");
    return false;
  }
  pairingCommandChar_ =
      handshake->getCharacteristic(NimBLEUUID(kPairingCommandCharacteristicUuid));
  pairingDataChar_ = handshake->getCharacteristic(
      NimBLEUUID(kPairingDataCharacteristicUuid));
  pairingInfoChar_ = handshake->getCharacteristic(
      NimBLEUUID(kPairingInfoCharacteristicUuid));
  // Pairing-info (0001000c) is present on R6 III Camera Connect captures but
  // absent on EOS R6 Mark II; initial handshake only needs command + data.
  if (pairingCommandChar_ == nullptr || pairingDataChar_ == nullptr) {
    CANON_LOG.printf("canon setup: pairing chars incomplete cmd=%d data=%d info=%d; refreshing\n",
                     pairingCommandChar_ != nullptr ? 1 : 0,
                     pairingDataChar_ != nullptr ? 1 : 0,
                     pairingInfoChar_ != nullptr ? 1 : 0);
    handshake->getCharacteristics(true);
    pairingCommandChar_ =
        handshake->getCharacteristic(
            NimBLEUUID(kPairingCommandCharacteristicUuid));
    pairingDataChar_ =
        handshake->getCharacteristic(NimBLEUUID(kPairingDataCharacteristicUuid));
    pairingInfoChar_ =
        handshake->getCharacteristic(NimBLEUUID(kPairingInfoCharacteristicUuid));
    if (pairingCommandChar_ == nullptr || pairingDataChar_ == nullptr) {
      CANON_LOG.printf("canon setup: pairing chars missing after refresh cmd=%d data=%d info=%d\n",
                       pairingCommandChar_ != nullptr ? 1 : 0,
                       pairingDataChar_ != nullptr ? 1 : 0,
                       pairingInfoChar_ != nullptr ? 1 : 0);
      for (NimBLERemoteCharacteristic* characteristic :
           handshake->getCharacteristics(false)) {
        if (characteristic != nullptr) {
          CANON_LOG.printf("canon handshake char uuid=%s\n",
                           characteristic->getUUID().toString().c_str());
        }
      }
      return false;
    }
  }
  studio::ble::logTiming(
      "canon_smart", linkHandle_, "gatt_handshake_discovery",
      millis() - discoveryStartedMs,
      millis() - studio::ble::bleCentral().timingStartedAt(linkHandle_), "ok");
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
                    buildHandshakeRequest("Ble(e)p"))) {
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
         writeCommand(pairingDataChar_, buildDeviceName("Ble(e)p")) &&
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
  // Targeted lookup performs a filtered discovery when the service was hidden
  // until pairing, without walking every service and descriptor twice.
  const uint32_t discoveryStartedMs = millis();
  NimBLERemoteService* core = client_->getService(NimBLEUUID(kCoreServiceUuid));
  if (core == nullptr) {
    CANON_LOG.println("canon core: service 00030000 missing");
    for (NimBLERemoteService* service : client_->getServices(false)) {
      if (service != nullptr) {
        CANON_LOG.printf("canon service uuid=%s\n",
                         service->getUUID().toString().c_str());
      }
    }
    studio::ble::logTiming(
        "canon_smart", linkHandle_, "gatt_core_discovery",
        millis() - discoveryStartedMs,
        millis() - studio::ble::bleCentral().timingStartedAt(linkHandle_),
        "failed");
    return false;
  }
  modeCommandChar_ =
      core->getCharacteristic(NimBLEUUID(kModeCommandCharacteristicUuid));
  modeResultChar_ =
      core->getCharacteristic(NimBLEUUID(kModeResultCharacteristicUuid));
  shootingCommandChar_ =
      core->getCharacteristic(NimBLEUUID(kShootingCommandCharacteristicUuid));
  shootingStateChar_ =
      core->getCharacteristic(NimBLEUUID(kShootingStateCharacteristicUuid));
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
  studio::ble::logTiming(
      "canon_smart", linkHandle_, "gatt_core_discovery",
      millis() - discoveryStartedMs,
      millis() - studio::ble::bleCentral().timingStartedAt(linkHandle_), "ok");

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

void CanonBleClient::handleDisconnect() {
  const bool pairingRejected = state_.pairingRejected;
  const bool protocolFailed = state_.protocolFailed;
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
  state_.protocolFailed = protocolFailed;
  state_.hasSavedDevice = haveTarget_;
  state_.link = Link::Disconnected;
  if (poweredOff) {
    state_.phase = State::Phase::PoweredOff;
  }
  if (connectRequested_) {
    if (recoverPairing) {
      studio::ble::bleCentral().requestScan(linkHandle_, true);
      state_.link = Link::Scanning;
    }
  }
}

void CanonBleClient::handleSecurityFailure() {
  ++securityFails_;
  bondRecoveryPending_ = haveTarget_ && securityFails_ >= 2;
  studio::ble::bleCentral().markProtocolFailed(linkHandle_);
}

void CanonBleClient::failProtocol() {
  connectRequested_ = false;
  state_.protocolFailed = true;
  studio::ble::bleCentral().markProtocolFailed(linkHandle_, false);
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
        if (!finishAcceptedHandshake()) {
          studio::ble::bleCentral().markProtocolFailed(linkHandle_);
        }
      } else if (response == PairingResponse::Rejected) {
        state_.pairingRejected = true;
        connectRequested_ = false;
        studio::ble::bleCentral().disconnect(linkHandle_);
      }
    } else if (notification.kind == kPairingInfoNotification &&
               state_.phase == State::Phase::PostPairSetup) {
      if (postPairStep_ < sizeof(kPostPairCommands) &&
          isPostPairResponse(kPostPairCommands[postPairStep_],
                             notification.data, notification.len)) {
        ++postPairStep_;
        if (!sendPostPairCommand()) {
          studio::ble::bleCentral().markProtocolFailed(linkHandle_);
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
    studio::ble::bleCentral().markProtocolReady(linkHandle_);
  }
}

bool CanonBleClient::protocolReady() const {
  return studio::ble::bleCentral().protocolReady(linkHandle_);
}

void CanonBleClient::clearIgnoredAddresses() {
  ignoredCount_ = 0;
  studio::ble::bleCentral().clearSkipAddresses(linkHandle_);
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
  studio::ble::Address peer;
  std::strncpy(peer.value, address, sizeof(peer.value) - 1);
  peer.value[sizeof(peer.value) - 1] = '\0';
  peer.type = targetAddrType_;
  studio::ble::bleCentral().addSkipAddress(linkHandle_, peer);
  CANON_LOG.printf("canon ignore addr=%s count=%u\n", address,
                   static_cast<unsigned>(ignoredCount_));
}

bool CanonBleClient::isBondedAdvertisement(
    const studio::ble::Advertisement& advertisement) const {
  const studio::ble::Address& address = advertisement.address;
  if (address.value[0] == '\0') return false;
  const NimBLEAddress peer(
      std::string(address.value),
      studio::ble::identityAddressType(address.type));
  return NimBLEDevice::isBonded(peer);
}

void CanonBleClient::adoptResolvedIdentity() {
  if (client_ == nullptr || !client_->isConnected()) return;
  const NimBLEAddress identity = client_->getConnInfo().getIdAddress();
  if (identity.isNull()) return;
  const std::string value = identity.toString();
  const uint8_t type = studio::ble::identityAddressType(identity.getType());
  if (value.empty() ||
      (std::strcmp(targetAddr_, value.c_str()) == 0 &&
       targetAddrType_ == type)) {
    return;
  }
  std::strncpy(targetAddr_, value.c_str(), sizeof(targetAddr_) - 1);
  targetAddr_[sizeof(targetAddr_) - 1] = '\0';
  targetAddrType_ = type;
  if (!newHandshake_) {
    std::strncpy(lockedAddr_, targetAddr_, sizeof(lockedAddr_) - 1);
    lockedAddr_[sizeof(lockedAddr_) - 1] = '\0';
    pairingChanged_ = true;
  }
  CANON_LOG.printf("canon identity addr=%s type=%u new=%d\n", targetAddr_,
                   static_cast<unsigned>(targetAddrType_),
                   newHandshake_ ? 1 : 0);
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

void CanonBleClient::considerScanCandidate(
    const studio::ble::Advertisement& advertisement, const char* name,
    bool hasService, bool hasMfg) {
  const char* address = advertisement.address.value;
  if (address == nullptr || address[0] == '\0') {
    return;
  }
  const int score = candidateScore(name, hasService, hasMfg);
  CANON_LOG.printf("canon scan candidate addr=%s name=%s score=%d svc=%d mfg=%d\n",
                   address, name != nullptr ? name : "", score,
                   hasService ? 1 : 0, hasMfg ? 1 : 0);
  if (scanCandidatePending_ &&
      std::strcmp(scanCandidate_.address.value, address) == 0) {
    if (name != nullptr && name[0] != '\0') {
      std::strncpy(scanCandidateName_, name, sizeof(scanCandidateName_) - 1);
      scanCandidateName_[sizeof(scanCandidateName_) - 1] = '\0';
    }
    scanCandidateHasService_ = scanCandidateHasService_ || hasService;
    scanCandidateHasMfg_ = scanCandidateHasMfg_ || hasMfg;
    scanCandidateScore_ = candidateScore(
        scanCandidateName_, scanCandidateHasService_, scanCandidateHasMfg_);
    return;
  }
  if (!scanCandidatePending_ || score > scanCandidateScore_) {
    scanCandidate_ = advertisement;
    scanCandidate_.address.value[
        sizeof(scanCandidate_.address.value) - 1] = '\0';
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

void CanonBleClient::onBleAdvertisement(
    studio::ble::LinkHandle link,
    const studio::ble::Advertisement& advertisement) {
  if (link != linkHandle_ || !connectRequested_ ||
      !matchesAdvertisement(advertisement)) {
    return;
  }
  const char* address = advertisement.address.value;
  if (isIgnoredAddress(address) ||
      (newHandshake_ && isBondedAdvertisement(advertisement))) {
    state_.claimedPeerVisible = newHandshake_;
    return;
  }
  if (lockedAddr_[0] != '\0' &&
      std::strcmp(address, lockedAddr_) != 0) {
    return;
  }
  char name[studio::ble::kBleNameCapacity] = "";
  studio::ble::advertisementName(advertisement, name, sizeof(name));
  const bool hasService =
      studio::ble::advertisesService(advertisement, kHandshakeServiceUuid);
  const bool hasMfg =
      studio::ble::manufacturerCompanyId(advertisement) == 0x01A9u;
  if (newHandshake_) {
    state_.claimedPeerVisible = false;
    considerScanCandidate(advertisement, name, hasService, hasMfg);
    return;
  }

  std::strncpy(targetAddr_, address, sizeof(targetAddr_) - 1);
  targetAddr_[sizeof(targetAddr_) - 1] = '\0';
  targetAddrType_ = advertisement.address.type;
  if (name[0] != '\0') {
    std::strncpy(targetName_, name, sizeof(targetName_) - 1);
    targetName_[sizeof(targetName_) - 1] = '\0';
  }
  haveTarget_ = true;
  resetTransientState(state_);
  state_.link = Link::Connecting;
  state_.phase = State::Phase::Bonding;
  setupPending_ = false;
  powerOffRequested_ = false;
  postPairStep_ = 0;
  if (!studio::ble::bleCentral().selectAdvertisement(linkHandle_,
                                                      advertisement)) {
    state_.link = Link::Scanning;
  }
}

void CanonBleClient::onBleEvent(studio::ble::LinkHandle link,
                                const studio::ble::Event& event) {
  if (link != linkHandle_) {
    return;
  }
  if (event.type == studio::ble::EventType::Connected) {
    client_ = static_cast<NimBLEClient*>(
        studio::ble::bleCentral().nativeClient(linkHandle_));
    CANON_LOG.printf("canon connected encrypted=%d addr=%s name=%s\n",
                     client_ != nullptr &&
                             client_->getConnInfo().isEncrypted()
                         ? 1
                         : 0,
                     targetAddr_, targetName_);
    if (!studio::ble::bleCentral().requestSecurity(linkHandle_)) {
      CANON_LOG.println("canon security request failed");
      handleSecurityFailure();
    }
  } else if (event.type == studio::ble::EventType::ConnectFailed) {
    CANON_LOG.printf("canon connect fail reason=%d addr=%s\n", event.reason,
                     targetAddr_);
    resetTransientState(state_);
    state_.link = Link::Disconnected;
  } else if (event.type == studio::ble::EventType::Disconnected) {
    CANON_LOG.printf("canon disconnect reason=%d phase=%d link=%d addr=%s\n",
                     event.reason, static_cast<int>(state_.phase),
                     static_cast<int>(state_.link), targetAddr_);
    if (newHandshake_ && event.reason == 531 && targetAddr_[0] != '\0') {
      ignoreAddress(targetAddr_);
      haveTarget_ = false;
      targetAddr_[0] = '\0';
    } else if (!newHandshake_ && haveTarget_ && event.reason == 531 &&
               connectRequested_ &&
               (state_.phase == State::Phase::Idle ||
                state_.phase == State::Phase::Bonding)) {
      // A saved body that accepts the link and hangs up before bonding has
      // handed its smartphone registration to another remote. Give it a few
      // tries for a wake glitch, then stop and ask for a new pairing instead
      // of burning the whole sequence connect budget.
      constexpr uint8_t kRemoteRejectionLimit = 3;
      if (++remoteRejections_ >= kRemoteRejectionLimit) {
        CANON_LOG.printf("canon pairing rejected by camera addr=%s count=%u\n",
                         targetAddr_,
                         static_cast<unsigned>(remoteRejections_));
        state_.pairingRejected = true;
        connectRequested_ = false;
      }
    }
    const bool expectedPowerOff =
        state_.phase == State::Phase::PoweringOff && !connectRequested_;
    client_ = nullptr;
    handleDisconnect();
    if (expectedPowerOff || !connectRequested_) {
      studio::ble::bleCentral().disconnect(linkHandle_);
    } else if (newHandshake_ && !haveTarget_) {
      beginScan();
    }
  } else if (event.type == studio::ble::EventType::SecurityComplete) {
    CANON_LOG.printf("canon security complete ok=%d\n",
                     event.succeeded ? 1 : 0);
    if (!event.succeeded) {
      handleSecurityFailure();
    } else {
      securityFails_ = 0;
      remoteRejections_ = 0;
      adoptResolvedIdentity();
      setupPending_ = true;
      setupAtMs_ = millis();
    }
  }
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
