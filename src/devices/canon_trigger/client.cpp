#include "devices/canon_trigger/client.h"

#include <Arduino.h>
#include <NimBLEDevice.h>

#include <cstring>
#include <string>

#include "core/ble/ble_runtime.h"
#include "core/ble/ble_timing.h"
#include "devices/canon_trigger/ble_match.h"
#include "devices/canon_trigger/protocol.h"

namespace canon_trigger {

namespace {

}  // namespace

bool CanonTriggerClient::begin() {
  if (initialized_) {
    return true;
  }
  studio::ble::ConnectPolicy policy;
  policy.security = studio::ble::SecurityPolicy::BondSecure;
  policy.connectTimeoutMs = 4000;
  policy.diagnosticTag = "canon_trigger";
  linkHandle_ = studio::ble::bleCentral().acquire(*this, policy);
  initialized_ = linkHandle_ != studio::ble::kInvalidLinkHandle;
  return initialized_;
}

bool CanonTriggerClient::activate(const char* address, uint8_t addressType,
                                  const char* name, bool paired) {
  if (!begin()) return false;
  connectRequested_ = true;
  haveTarget_ = paired && address != nullptr && address[0] != '\0';
  newPairing_ = !haveTarget_;
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
  state_.hasSavedDevice = haveTarget_;
  std::strncpy(state_.deviceName, targetName_, sizeof(state_.deviceName) - 1);
  state_.deviceName[sizeof(state_.deviceName) - 1] = '\0';
  clearIgnoredAddresses();
  if (haveTarget_) {
    beginConnect();
  } else {
    beginScan();
  }
  return true;
}

void CanonTriggerClient::deactivate() {
  connectRequested_ = false;
  studio::ble::bleCentral().release(linkHandle_);
  linkHandle_ = studio::ble::kInvalidLinkHandle;
  initialized_ = false;
  client_ = nullptr;
  triggerRequested_ = false;
  triggerReleasePending_ = false;
  setupPending_ = false;
  resetTransientState(state_);
  state_.link = Link::Disconnected;
}

void CanonTriggerClient::loop() {
  if (setupPending_) {
    setupPending_ = false;
    if (client_ == nullptr || !client_->isConnected() || !completeConnect()) {
      studio::ble::bleCentral().markProtocolFailed(linkHandle_);
      return;
    }
  }
  const uint32_t now = millis();
  if (connected() && triggerRequested_) {
    triggerRequested_ = false;
    const uint8_t value = kRecordTriggerPress;
    const bool sent = controlChar_ != nullptr &&
                      controlChar_->writeValue(
                          &value, 1, !controlChar_->canWriteNoResponse());
    if (sent) {
      triggerReleasePending_ = true;
      triggerReleaseAtMs_ = now + kTriggerHoldMs;
    } else {
      markTriggerComplete(state_, false);
    }
  }
  if (connected() && triggerReleasePending_ &&
      static_cast<int32_t>(now - triggerReleaseAtMs_) >= 0) {
    triggerReleasePending_ = false;
    const uint8_t value = kRecordTriggerRelease;
    const bool sent = controlChar_ != nullptr &&
                      controlChar_->writeValue(
                          &value, 1, !controlChar_->canWriteNoResponse());
    markTriggerComplete(state_, sent);
  }
}

void CanonTriggerClient::retry() {
  connectRequested_ = true;
  resetTransientState(state_);
  if (targetAddr_[0] != '\0') {
    haveTarget_ = true;
    state_.hasSavedDevice = true;
    beginConnect();
  } else {
    beginScan();
  }
}

void CanonTriggerClient::startScan() {
  begin();
  connectRequested_ = true;
  teardownConnection();
  haveTarget_ = false;
  newPairing_ = true;
  lockedAddr_[0] = '\0';
  state_.hasSavedDevice = false;
  beginScan();
}

void CanonTriggerClient::forgetDevice() {
  forgetBond(targetAddr_, targetAddrType_);
  haveTarget_ = false;
  targetAddr_[0] = '\0';
  targetName_[0] = '\0';
  state_.hasSavedDevice = false;
  pairingChanged_ = true;
  startScan();
}

void CanonTriggerClient::forgetBond(const char* address, uint8_t addressType) {
  if (address == nullptr || address[0] == '\0') {
    return;
  }
  studio::ble::Address peer;
  std::strncpy(peer.value, address, sizeof(peer.value) - 1);
  peer.type = addressType;
  studio::ble::bleCentral().deleteBond(peer);
}

bool CanonTriggerClient::triggerRecord() {
  if (!connected() || state_.triggerPending) {
    return false;
  }
  markTriggerQueued(state_);
  triggerRequested_ = true;
  return true;
}

bool CanonTriggerClient::consumePairingUpdate(
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

void CanonTriggerClient::beginScan() {
  studio::ble::bleCentral().requestScan(linkHandle_, !haveTarget_);
  state_.link = Link::Scanning;
}

void CanonTriggerClient::beginConnect() {
  state_.link = Link::Connecting;
  studio::ble::Address address;
  std::strncpy(address.value, targetAddr_, sizeof(address.value) - 1);
  address.type = targetAddrType_;
  studio::ble::bleCentral().requestConnect(linkHandle_, address);
}

bool CanonTriggerClient::completeConnect() {
  const uint32_t discoveryStartedMs = millis();
  NimBLERemoteService* service =
      client_->getService(NimBLEUUID(kPrimaryServiceUuid));
  if (service == nullptr) {
    studio::ble::logTiming(
        "canon_trigger", linkHandle_, "gatt_setup",
        millis() - discoveryStartedMs,
        millis() - studio::ble::bleCentral().timingStartedAt(linkHandle_),
        "failed");
    return false;
  }
  pairingChar_ =
      service->getCharacteristic(NimBLEUUID(kPairingCharacteristicUuid));
  controlChar_ =
      service->getCharacteristic(NimBLEUUID(kControlCharacteristicUuid));
  if (pairingChar_ == nullptr || controlChar_ == nullptr) {
    return false;
  }

  const PairingName identity = buildPairingName("Ble(e)p");
  const bool pairingResponse = !pairingChar_->canWriteNoResponse();
  if (!pairingChar_->writeValue(identity.bytes, identity.len,
                                pairingResponse)) {
    return false;
  }
  studio::ble::logTiming(
      "canon_trigger", linkHandle_, "gatt_setup",
      millis() - discoveryStartedMs,
      millis() - studio::ble::bleCentral().timingStartedAt(linkHandle_), "ok");

  state_.link = Link::Connected;
  state_.hasSavedDevice = true;
  std::strncpy(state_.deviceName, targetName_, sizeof(state_.deviceName) - 1);
  state_.deviceName[sizeof(state_.deviceName) - 1] = '\0';
  haveTarget_ = true;
  newPairing_ = false;
  std::strncpy(lockedAddr_, targetAddr_, sizeof(lockedAddr_) - 1);
  lockedAddr_[sizeof(lockedAddr_) - 1] = '\0';
  pairingChanged_ = true;
  studio::ble::bleCentral().markProtocolReady(linkHandle_);
  return true;
}

void CanonTriggerClient::teardownConnection() {
  studio::ble::bleCentral().disconnect(linkHandle_);
  pairingChar_ = nullptr;
  controlChar_ = nullptr;
}

void CanonTriggerClient::handleDisconnect() {
  pairingChar_ = nullptr;
  controlChar_ = nullptr;
  triggerRequested_ = false;
  triggerReleasePending_ = false;
  setupPending_ = false;
  resetTransientState(state_);
  state_.link = Link::Disconnected;
}

void CanonTriggerClient::clearIgnoredAddresses() {
  ignoredCount_ = 0;
  for (size_t i = 0; i < kMaxIgnoredAddresses; ++i) {
    ignoredAddrs_[i][0] = '\0';
  }
  state_.claimedPeerVisible = false;
}

bool CanonTriggerClient::isIgnoredAddress(const char* address) const {
  if (address == nullptr || address[0] == '\0') return false;
  for (uint8_t i = 0; i < ignoredCount_; ++i) {
    if (std::strcmp(ignoredAddrs_[i], address) == 0) return true;
  }
  return false;
}

void CanonTriggerClient::ignoreAddress(const char* address) {
  if (address == nullptr || address[0] == '\0' || isIgnoredAddress(address)) {
    return;
  }
  if (ignoredCount_ >= kMaxIgnoredAddresses) {
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
}

void CanonTriggerClient::ignorePeerAddress(const char* address) {
  ignoreAddress(address);
}

bool CanonTriggerClient::isBondedAdvertisement(
    const studio::ble::Advertisement& advertisement) const {
  const studio::ble::Address& address = advertisement.address;
  if (address.value[0] == '\0') return false;
  const NimBLEAddress peer(
      std::string(address.value),
      studio::ble::identityAddressType(address.type));
  return NimBLEDevice::isBonded(peer);
}

void CanonTriggerClient::adoptResolvedIdentity() {
  if (client_ == nullptr || !client_->isConnected()) return;
  const NimBLEAddress identity = client_->getConnInfo().getIdAddress();
  if (identity.isNull()) return;
  const std::string value = identity.toString();
  if (value.empty()) return;
  std::strncpy(targetAddr_, value.c_str(), sizeof(targetAddr_) - 1);
  targetAddr_[sizeof(targetAddr_) - 1] = '\0';
  targetAddrType_ = studio::ble::identityAddressType(identity.getType());
}

void CanonTriggerClient::onBleAdvertisement(
    studio::ble::LinkHandle link,
    const studio::ble::Advertisement& advertisement) {
  if (link != linkHandle_ ||
      !matchesAdvertisement(advertisement)) {
    return;
  }
  const char* address = advertisement.address.value;
  if (isIgnoredAddress(address) ||
      (newPairing_ && isBondedAdvertisement(advertisement))) {
    state_.claimedPeerVisible = newPairing_;
    return;
  }
  if (lockedAddr_[0] != '\0' && std::strcmp(address, lockedAddr_) != 0) {
    return;
  }
  state_.claimedPeerVisible = false;
  std::strncpy(targetAddr_, advertisement.address.value,
               sizeof(targetAddr_) - 1);
  targetAddrType_ = advertisement.address.type;
  studio::ble::advertisementName(advertisement, targetName_,
                                 sizeof(targetName_));
  haveTarget_ = true;
  state_.link = Link::Connecting;
  studio::ble::bleCentral().selectAdvertisement(linkHandle_, advertisement);
}

void CanonTriggerClient::onBleEvent(studio::ble::LinkHandle link,
                                    const studio::ble::Event& event) {
  if (link != linkHandle_) {
    return;
  }
  if (event.type == studio::ble::EventType::Connected) {
    client_ = static_cast<NimBLEClient*>(
        studio::ble::bleCentral().nativeClient(linkHandle_));
    if (client_ != nullptr && client_->getConnInfo().isEncrypted()) {
      adoptResolvedIdentity();
      setupPending_ = true;
    } else if (!studio::ble::bleCentral().requestSecurity(linkHandle_)) {
      studio::ble::bleCentral().markProtocolFailed(linkHandle_);
    }
  } else if (event.type == studio::ble::EventType::SecurityComplete) {
    if (!event.succeeded) {
      studio::ble::bleCentral().markProtocolFailed(linkHandle_);
    } else {
      adoptResolvedIdentity();
      setupPending_ = true;
    }
  } else if (event.type == studio::ble::EventType::ConnectFailed) {
    state_.link = Link::Disconnected;
  } else if (event.type == studio::ble::EventType::Disconnected) {
    client_ = nullptr;
    handleDisconnect();
  }
}

bool CanonTriggerClient::protocolReady() const {
  return studio::ble::bleCentral().protocolReady(linkHandle_);
}

}  // namespace canon_trigger
