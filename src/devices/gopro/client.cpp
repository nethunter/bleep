#include "devices/gopro/client.h"

#include <Arduino.h>
#include <NimBLEDevice.h>

#include <cstring>

#include "core/ble/ble_runtime.h"
#include "core/ble/ble_timing.h"
#include "devices/gopro/ble_match.h"
#include "devices/gopro/protocol.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

namespace gopro {
namespace {

GoProClient* gClients[4] = {};

void responseNotifyTrampoline(NimBLERemoteCharacteristic* characteristic,
                              uint8_t* data, size_t length, bool) {
  for (GoProClient* client : gClients) {
    if (client != nullptr && client->ownsResponseCharacteristic(characteristic)) {
      client->onResponse(data, length);
    }
  }
}

void registerClient(GoProClient* client) {
  for (GoProClient*& slot : gClients) {
    if (slot == client) return;
    if (slot == nullptr) {
      slot = client;
      return;
    }
  }
}

void unregisterClient(GoProClient* client) {
  for (GoProClient*& slot : gClients) {
    if (slot == client) slot = nullptr;
  }
}

}  // namespace

void GoProClient::begin() {
  if (initialized_) return;
  if (responseQueue_ == nullptr) {
    responseQueue_ = xQueueCreate(8, sizeof(QueuedResponse));
  }
  if (responseQueue_ == nullptr) return;
  studio::ble::ConnectPolicy policy;
  policy.security = studio::ble::SecurityPolicy::BondNoMitm;
  policy.connectTimeoutMs = 5000;
  policy.diagnosticTag = "gopro";
  linkHandle_ = studio::ble::bleCentral().acquire(*this, policy);
  initialized_ = linkHandle_ != studio::ble::kInvalidLinkHandle;
  if (initialized_) registerClient(this);
}

void GoProClient::activate(const char* address, uint8_t addressType,
                           const char* name, bool paired) {
  begin();
  connectRequested_ = initialized_;
  haveTarget_ = paired && address != nullptr && address[0] != '\0';
  targetAddr_[0] = '\0';
  targetName_[0] = '\0';
  targetAddrType_ = addressType;
  if (haveTarget_) std::strncpy(targetAddr_, address, sizeof(targetAddr_) - 1);
  if (name != nullptr) std::strncpy(targetName_, name, sizeof(targetName_) - 1);
  state_.hasSavedDevice = haveTarget_;
  std::strncpy(state_.deviceName, targetName_, sizeof(state_.deviceName) - 1);
  if (!initialized_) return;
  haveTarget_ ? beginConnect() : beginScan();
}

void GoProClient::deactivate() {
  connectRequested_ = false;
  studio::ble::bleCentral().release(linkHandle_);
  linkHandle_ = studio::ble::kInvalidLinkHandle;
  unregisterClient(this);
  initialized_ = false;
  client_ = nullptr;
  commandChar_ = nullptr;
  responseChar_ = nullptr;
  setupPending_ = false;
  pairingResponsePending_ = false;
  commandRequested_ = false;
  resetTransientState(state_);
  state_.link = Link::Disconnected;
  if (responseQueue_ != nullptr) {
    vQueueDelete(static_cast<QueueHandle_t>(responseQueue_));
    responseQueue_ = nullptr;
  }
}

void GoProClient::loop() {
  drainResponses();
  if (!connectRequested_) return;
  const uint32_t now = millis();
  if (setupPending_) {
    setupPending_ = false;
    if (client_ == nullptr || !client_->isConnected() || !completeConnect()) {
      studio::ble::bleCentral().markProtocolFailed(linkHandle_);
      return;
    }
  }
  if ((pairingResponsePending_ || state_.commandPending) &&
      static_cast<int32_t>(now - responseDeadlineMs_) >= 0) {
    if (pairingResponsePending_) {
      pairingResponsePending_ = false;
      studio::ble::bleCentral().markProtocolFailed(linkHandle_);
    } else {
      reduceCommandResponse(state_, requestedStart_, 0xff);
    }
  }
  if (commandRequested_) {
    commandRequested_ = false;
    const Packet packet = buildSetShutter(requestedStart_);
    if (!send(packet.bytes, packet.len)) {
      reduceCommandResponse(state_, requestedStart_, 0xff);
    } else {
      responseDeadlineMs_ = now + 4000;
    }
  }
}

void GoProClient::startScan() {
  begin();
  if (!initialized_) return;
  connectRequested_ = true;
  teardownConnection();
  haveTarget_ = false;
  state_.hasSavedDevice = false;
  beginScan();
}

void GoProClient::forgetDevice() {
  forgetBond(targetAddr_, targetAddrType_);
  haveTarget_ = false;
  targetAddr_[0] = '\0';
  targetName_[0] = '\0';
  state_.hasSavedDevice = false;
  pairingChanged_ = true;
  startScan();
}

void GoProClient::forgetBond(const char* address, uint8_t addressType) {
  if (address == nullptr || address[0] == '\0') return;
  studio::ble::Address peer;
  std::strncpy(peer.value, address, sizeof(peer.value) - 1);
  peer.type = addressType;
  studio::ble::bleCentral().deleteBond(peer);
}

bool GoProClient::setShutter(bool enabled) {
  if (state_.link != Link::Connected || state_.commandPending) return false;
  requestedStart_ = enabled;
  commandRequested_ = true;
  markCommandQueued(state_, enabled);
  return true;
}

bool GoProClient::consumePairingUpdate(char* address, size_t addressCapacity,
                                       uint8_t& addressType, char* name,
                                       size_t nameCapacity, bool& paired) {
  if (!pairingChanged_) return false;
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

void GoProClient::beginScan() {
  studio::ble::bleCentral().requestScan(linkHandle_, !haveTarget_);
  state_.link = Link::Scanning;
}

void GoProClient::beginConnect() {
  state_.link = Link::Connecting;
  studio::ble::Address address;
  std::strncpy(address.value, targetAddr_, sizeof(address.value) - 1);
  address.type = targetAddrType_;
  studio::ble::bleCentral().requestConnect(linkHandle_, address);
}

bool GoProClient::completeConnect() {
  const uint32_t started = millis();
  NimBLERemoteService* service =
      client_->getService(NimBLEUUID(kControlServiceUuid));
  if (service == nullptr) return false;
  commandChar_ = service->getCharacteristic(NimBLEUUID(kCommandCharacteristicUuid));
  responseChar_ = service->getCharacteristic(NimBLEUUID(kResponseCharacteristicUuid));
  if (commandChar_ == nullptr || responseChar_ == nullptr ||
      !responseChar_->subscribe(true, responseNotifyTrampoline, true)) {
    commandChar_ = nullptr;
    responseChar_ = nullptr;
    return false;
  }
  studio::ble::logTiming("gopro", linkHandle_, "gatt_setup", millis() - started,
                         millis() - studio::ble::bleCentral().timingStartedAt(linkHandle_),
                         "ok");
  if (!state_.hasSavedDevice) {
    const Packet packet = buildSetPairingState();
    if (!send(packet.bytes, packet.len)) return false;
    pairingResponsePending_ = true;
    responseDeadlineMs_ = millis() + 4000;
  } else {
    markReady();
  }
  return true;
}

void GoProClient::markReady() {
  state_.link = Link::Connected;
  state_.hasSavedDevice = true;
  haveTarget_ = true;
  pairingChanged_ = true;
  studio::ble::bleCentral().markProtocolReady(linkHandle_);
}

void GoProClient::teardownConnection() {
  studio::ble::bleCentral().disconnect(linkHandle_);
  commandChar_ = nullptr;
  responseChar_ = nullptr;
}

void GoProClient::handleDisconnect() {
  commandChar_ = nullptr;
  responseChar_ = nullptr;
  setupPending_ = false;
  pairingResponsePending_ = false;
  commandRequested_ = false;
  resetTransientState(state_);
  state_.link = Link::Disconnected;
}

void GoProClient::drainResponses() {
  if (responseQueue_ == nullptr) return;
  QueuedResponse queued;
  while (xQueueReceive(static_cast<QueueHandle_t>(responseQueue_), &queued, 0) == pdTRUE) {
    const Response response = parseResponse(queued.data, queued.len);
    if (!response.valid) continue;
    if (pairingResponsePending_ && response.command == kSetPairingStateCommand) {
      pairingResponsePending_ = false;
      if (response.status == kSuccessStatus) markReady();
      else studio::ble::bleCentral().markProtocolFailed(linkHandle_);
    } else if (state_.commandPending && response.command == kSetShutterCommand) {
      reduceCommandResponse(state_, requestedStart_, response.status);
    }
  }
}

bool GoProClient::send(const uint8_t* data, size_t len) {
  return commandChar_ != nullptr && data != nullptr && len > 0 &&
         commandChar_->writeValue(data, len, !commandChar_->canWriteNoResponse());
}

void GoProClient::onBleAdvertisement(
    studio::ble::LinkHandle link,
    const studio::ble::Advertisement& advertisement) {
  if (link != linkHandle_ || !matchesAdvertisement(advertisement)) return;
  std::strncpy(targetAddr_, advertisement.address.value, sizeof(targetAddr_) - 1);
  targetAddrType_ = advertisement.address.type;
  studio::ble::advertisementName(advertisement, targetName_, sizeof(targetName_));
  haveTarget_ = true;
  state_.link = Link::Connecting;
  studio::ble::bleCentral().selectAdvertisement(linkHandle_, advertisement);
}

void GoProClient::onBleEvent(studio::ble::LinkHandle link,
                             const studio::ble::Event& event) {
  if (link != linkHandle_) return;
  if (event.type == studio::ble::EventType::Connected) {
    client_ = static_cast<NimBLEClient*>(studio::ble::bleCentral().nativeClient(linkHandle_));
    if (client_ != nullptr && client_->getConnInfo().isEncrypted()) setupPending_ = true;
    else if (!studio::ble::bleCentral().requestSecurity(linkHandle_))
      studio::ble::bleCentral().markProtocolFailed(linkHandle_);
  } else if (event.type == studio::ble::EventType::SecurityComplete) {
    if (event.succeeded) setupPending_ = true;
    else studio::ble::bleCentral().markProtocolFailed(linkHandle_);
  } else if (event.type == studio::ble::EventType::ConnectFailed) {
    state_.link = Link::Disconnected;
  } else if (event.type == studio::ble::EventType::Disconnected) {
    client_ = nullptr;
    handleDisconnect();
  }
}

void GoProClient::onResponse(const uint8_t* data, size_t len) {
  if (responseQueue_ == nullptr || data == nullptr || len == 0) return;
  QueuedResponse queued;
  queued.len = static_cast<uint8_t>(len < sizeof(queued.data) ? len : sizeof(queued.data));
  std::memcpy(queued.data, data, queued.len);
  xQueueSend(static_cast<QueueHandle_t>(responseQueue_), &queued, 0);
}

bool GoProClient::protocolReady() const {
  return studio::ble::bleCentral().protocolReady(linkHandle_);
}

}  // namespace gopro
