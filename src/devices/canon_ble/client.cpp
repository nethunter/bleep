#include "devices/canon_ble/client.h"

#include <Arduino.h>
#include <NimBLEDevice.h>

#include <cstring>
#include <string>

#include "devices/canon_ble/protocol.h"

namespace canon_ble {

namespace {

const NimBLEUUID kPrimaryService(kPrimaryServiceUuid);
const NimBLEUUID kPairingCharacteristic(kPairingCharacteristicUuid);
const NimBLEUUID kControlCharacteristic(kControlCharacteristicUuid);
CanonBleClient* gCallbackClient = nullptr;

class ScanCallbacks : public NimBLEScanCallbacks {
 public:
  void onResult(const NimBLEAdvertisedDevice* device) override {
    if (device != nullptr && device->isAdvertisingService(kPrimaryService) &&
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
  void onAuthenticationComplete(NimBLEConnInfo& info) override {
    if (gCallbackClient != nullptr) {
      gCallbackClient->onSecurityComplete(info.isEncrypted());
    }
  }
};

ScanCallbacks gScanCallbacks;
ClientCallbacks gClientCallbacks;

}  // namespace

void CanonBleClient::begin() {
  if (initialized_) {
    return;
  }
  NimBLEDevice::init("StudioRemote");
  NimBLEDevice::setSecurityAuth(true, true, true);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
  gCallbackClient = this;
  initialized_ = true;
}

void CanonBleClient::activate(const char* address, uint8_t addressType,
                              const char* name, bool paired) {
  begin();
  connectRequested_ = true;
  haveTarget_ = paired && address != nullptr && address[0] != '\0';
  targetAddr_[0] = '\0';
  targetName_[0] = '\0';
  targetAddrType_ = addressType;
  if (haveTarget_) {
    std::strncpy(targetAddr_, address, sizeof(targetAddr_) - 1);
  }
  if (name != nullptr) {
    std::strncpy(targetName_, name, sizeof(targetName_) - 1);
  }
  state_.hasSavedDevice = haveTarget_;
  std::strncpy(state_.deviceName, targetName_, sizeof(state_.deviceName) - 1);
  connectFails_ = 0;
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
  triggerRequested_ = false;
  triggerReleasePending_ = false;
  resetTransientState(state_);
  state_.link = Link::Disconnected;
}

void CanonBleClient::loop() {
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
    if (client_ != nullptr && client_->getConnInfo().isEncrypted()) {
      if (!completeConnect()) {
        client_->disconnect();
      }
    } else if (client_ == nullptr || !client_->secureConnection(true)) {
      if (client_ != nullptr) {
        client_->disconnect();
      }
    }
  }
  if (securityCompleteFlag_) {
    securityCompleteFlag_ = false;
    if (!securitySucceeded_ || !completeConnect()) {
      if (client_ != nullptr) {
        client_->disconnect();
      }
    }
  }

  if (scanHit_) {
    NimBLEDevice::getScan()->stop();
    scanActive_ = false;
    std::strncpy(targetAddr_, scanHitAddr_, sizeof(targetAddr_) - 1);
    targetAddrType_ = scanHitType_;
    std::strncpy(targetName_, scanHitName_, sizeof(targetName_) - 1);
    haveTarget_ = true;
    scanHit_ = false;
    beginConnect();
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

  if (!connectRequested_ || state_.link != Link::Disconnected) {
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
  state_.hasSavedDevice = false;
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

bool CanonBleClient::triggerRecord() {
  if (!connected() || state_.triggerPending) {
    return false;
  }
  markTriggerQueued(state_);
  triggerRequested_ = true;
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
  state_.link = Link::Connecting;
  NimBLEAddress address(std::string(targetAddr_), targetAddrType_);
  connectedFlag_ = false;
  connectFailedFlag_ = false;
  securityCompleteFlag_ = false;
  if (client_->connect(address, true, true, true)) {
    return;
  }
  ++connectFails_;
  state_.link = Link::Disconnected;
  scheduleRetry(1500u * (connectFails_ < 4 ? connectFails_ : 4));
}

bool CanonBleClient::completeConnect() {
  NimBLERemoteService* service = client_->getService(kPrimaryService);
  if (service == nullptr) {
    return false;
  }
  pairingChar_ = service->getCharacteristic(kPairingCharacteristic);
  controlChar_ = service->getCharacteristic(kControlCharacteristic);
  if (pairingChar_ == nullptr || controlChar_ == nullptr) {
    return false;
  }

  const PairingName identity = buildPairingName("StudioRemote");
  const bool pairingResponse = !pairingChar_->canWriteNoResponse();
  if (!pairingChar_->writeValue(identity.bytes, identity.len,
                                pairingResponse)) {
    return false;
  }

  state_.link = Link::Connected;
  state_.hasSavedDevice = true;
  std::strncpy(state_.deviceName, targetName_, sizeof(state_.deviceName) - 1);
  haveTarget_ = true;
  pairingChanged_ = true;
  connectFails_ = 0;
  return true;
}

void CanonBleClient::teardownConnection() {
  if (client_ != nullptr && client_->isConnected()) {
    client_->disconnect();
  }
  pairingChar_ = nullptr;
  controlChar_ = nullptr;
}

void CanonBleClient::handleDisconnect() {
  pairingChar_ = nullptr;
  controlChar_ = nullptr;
  triggerRequested_ = false;
  triggerReleasePending_ = false;
  resetTransientState(state_);
  state_.link = Link::Disconnected;
  scheduleRetry(1500);
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
  scanHitType_ = device->getAddress().getType();
  std::strncpy(scanHitName_, name.c_str(), sizeof(scanHitName_) - 1);
  scanHit_ = true;
}

void CanonBleClient::onLinkConnected() { connectedFlag_ = true; }

void CanonBleClient::onConnectFailed() { connectFailedFlag_ = true; }

void CanonBleClient::onLinkDisconnected() { disconnectedFlag_ = true; }

void CanonBleClient::onSecurityComplete(bool succeeded) {
  securitySucceeded_ = succeeded;
  securityCompleteFlag_ = true;
}

}  // namespace canon_ble
