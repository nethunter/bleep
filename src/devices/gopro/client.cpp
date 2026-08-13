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
      client->onResponse(characteristic, data, length);
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
  queryChar_ = nullptr;
  queryResponseChar_ = nullptr;
  setupPending_ = false;
  pairingResponsePending_ = false;
  readinessActive_ = false;
  hardwareResponsePending_ = false;
  encodingResponsePending_ = false;
  encodingQueryPending_ = false;
  commandRequested_ = false;
  sleepRequested_ = false;
  sleepResponseAccepted_ = false;
  sleepDisconnectObserved_ = false;
  resetTransientState(state_);
  state_.link = Link::Disconnected;
  if (responseQueue_ != nullptr) {
    vQueueDelete(static_cast<QueueHandle_t>(responseQueue_));
    responseQueue_ = nullptr;
  }
}

void GoProClient::loop() {
  drainResponses();
  if (!connectRequested_ && state_.power != State::Power::Sleeping) return;
  const uint32_t now = millis();
  if (setupPending_) {
    setupPending_ = false;
    if (client_ == nullptr || !client_->isConnected() || !completeConnect()) {
      studio::ble::bleCentral().markProtocolFailed(linkHandle_);
      return;
    }
  }
  if (pairingResponsePending_ &&
      static_cast<int32_t>(now - responseDeadlineMs_) >= 0) {
    pairingResponsePending_ = false;
    studio::ble::bleCentral().markProtocolFailed(linkHandle_);
  }
  if (readinessActive_ &&
      static_cast<int32_t>(now - readinessDeadlineMs_) >= 0) {
    readinessActive_ = false;
    studio::ble::bleCentral().markProtocolFailed(linkHandle_);
  } else if (readinessActive_ && hardwareResponsePending_ &&
             static_cast<int32_t>(now - responseDeadlineMs_) >= 0) {
    hardwareResponsePending_ = false;
    nextReadinessPollMs_ = now + 750;
  } else if (readinessActive_ && encodingResponsePending_ &&
             static_cast<int32_t>(now - responseDeadlineMs_) >= 0) {
    encodingResponsePending_ = false;
    if (!triedTwoByteStatus_) {
      triedTwoByteStatus_ = true;
      if (!requestEncodingRegistration(true))
        studio::ble::bleCentral().markProtocolFailed(linkHandle_);
    } else {
      studio::ble::bleCentral().markProtocolFailed(linkHandle_);
    }
  } else if (readinessActive_ && !hardwareResponsePending_ &&
             !encodingResponsePending_ &&
             static_cast<int32_t>(now - nextReadinessPollMs_) >= 0) {
    if (!requestHardwareInfo())
      studio::ble::bleCentral().markProtocolFailed(linkHandle_);
  }
  if (state_.commandPending &&
      static_cast<int32_t>(now - commandDeadlineMs_) >= 0) {
    markCommandTimeout(state_);
    encodingQueryPending_ = false;
  } else if (state_.commandPending && encodingQueryPending_ &&
             static_cast<int32_t>(now - responseDeadlineMs_) >= 0) {
    encodingQueryPending_ = false;
    nextEncodingPollMs_ = now + 250;
  } else if (state_.commandPending && !encodingQueryPending_ &&
             static_cast<int32_t>(now - nextEncodingPollMs_) >= 0) {
    if (!requestEncodingQuery()) markCommandTimeout(state_);
  }
  if (commandRequested_) {
    commandRequested_ = false;
    const Packet packet = buildSetShutter(requestedStart_);
    if (!send(packet.bytes, packet.len)) {
      markCommandTimeout(state_);
    } else {
      commandDeadlineMs_ = now + 10000;
      nextEncodingPollMs_ = commandDeadlineMs_;
    }
  }
  if (sleepRequested_) {
    sleepRequested_ = false;
    connectRequested_ = false;
    const Packet packet = buildSleep();
    if (!send(packet.bytes, packet.len)) {
      connectRequested_ = true;
      failSleep();
    } else {
      sleepDeadlineMs_ = now + 5000;
    }
  }
  if (state_.power == State::Power::Sleeping &&
      static_cast<int32_t>(now - sleepDeadlineMs_) >= 0) {
    failSleep();
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
  if (state_.link != Link::Connected || state_.power != State::Power::Awake ||
      state_.commandPending || state_.powerCommandPending) return false;
  if (state_.recordingConfirmed &&
      (state_.recording == GoProState::Recording::Recording) == enabled) {
    state_.lastCommandFailed = false;
    return true;
  }
  requestedStart_ = enabled;
  commandRequested_ = true;
  commandDeadlineMs_ = millis() + 10000;
  nextEncodingPollMs_ = commandDeadlineMs_;
  markCommandQueued(state_, enabled);
  return true;
}

bool GoProClient::powerOn() {
  if (state_.link != Link::Disconnected ||
      (state_.power != State::Power::Asleep &&
       state_.power != State::Power::SleepFailed)) {
    return false;
  }
  connectRequested_ = true;
  state_.power = State::Power::Waking;
  state_.powerOffFailed = false;
  if (haveTarget_) beginConnect();
  else beginScan();
  return true;
}

bool GoProClient::powerOff() {
  if (state_.link != Link::Connected || !protocolReady() ||
      state_.power != State::Power::Awake || state_.commandPending ||
      state_.powerCommandPending ||
      (state_.recordingConfirmed &&
       state_.recording == State::Recording::Recording)) {
    return false;
  }
  state_.power = State::Power::Sleeping;
  state_.powerCommandPending = true;
  state_.powerOffFailed = false;
  sleepRequested_ = true;
  sleepResponseAccepted_ = false;
  sleepDisconnectObserved_ = false;
  sleepDeadlineMs_ = millis() + 5000;
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
  queryChar_ = service->getCharacteristic(NimBLEUUID(kQueryCharacteristicUuid));
  queryResponseChar_ =
      service->getCharacteristic(NimBLEUUID(kQueryResponseCharacteristicUuid));
  if (commandChar_ == nullptr || responseChar_ == nullptr || queryChar_ == nullptr ||
      queryResponseChar_ == nullptr ||
      !responseChar_->subscribe(true, responseNotifyTrampoline, true) ||
      !queryResponseChar_->subscribe(true, responseNotifyTrampoline, true)) {
    commandChar_ = nullptr;
    responseChar_ = nullptr;
    queryChar_ = nullptr;
    queryResponseChar_ = nullptr;
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
    beginReadiness();
  }
  return true;
}

void GoProClient::beginReadiness() {
  readinessActive_ = true;
  hardwareResponsePending_ = false;
  encodingResponsePending_ = false;
  triedTwoByteStatus_ = false;
  readinessDeadlineMs_ = millis() + 20000;
  nextReadinessPollMs_ = millis();
}

bool GoProClient::requestHardwareInfo() {
  const Packet packet = buildGetHardwareInfo();
  if (!send(packet.bytes, packet.len)) return false;
  hardwareResponsePending_ = true;
  responseDeadlineMs_ = millis() + 2500;
  return true;
}

bool GoProClient::requestEncodingRegistration(bool twoByteIds) {
  const Packet packet = buildRegisterEncoding(twoByteIds);
  if (!sendQuery(packet.bytes, packet.len)) return false;
  encodingResponsePending_ = true;
  responseDeadlineMs_ = millis() + 4000;
  return true;
}

bool GoProClient::requestEncodingQuery() {
  const Packet packet = buildGetEncoding(triedTwoByteStatus_);
  if (!sendQuery(packet.bytes, packet.len)) return false;
  encodingQueryPending_ = true;
  responseDeadlineMs_ = millis() + 1500;
  return true;
}

void GoProClient::applyEncodingStatus(bool encoding) {
  if (state_.commandPending) {
    reducePendingEncodingStatus(state_, encoding, requestedStart_);
  } else {
    reduceEncodingStatus(state_, encoding);
  }
  if (!state_.commandPending) encodingQueryPending_ = false;
}

void GoProClient::markReady() {
  readinessActive_ = false;
  hardwareResponsePending_ = false;
  encodingResponsePending_ = false;
  encodingQueryPending_ = false;
  state_.link = Link::Connected;
  state_.power = State::Power::Awake;
  state_.powerCommandPending = false;
  state_.powerOffFailed = false;
  state_.hasSavedDevice = true;
  haveTarget_ = true;
  pairingChanged_ = true;
  studio::ble::bleCentral().markProtocolReady(linkHandle_);
}

void GoProClient::completeSleepIfReady() {
  if (!sleepResponseAccepted_ || !sleepDisconnectObserved_) return;
  state_.power = State::Power::Asleep;
  state_.powerCommandPending = false;
  state_.powerOffFailed = false;
  resetTransientState(state_);
}

void GoProClient::failSleep() {
  sleepRequested_ = false;
  sleepResponseAccepted_ = false;
  state_.power = state_.link == Link::Disconnected
                     ? State::Power::SleepFailed
                     : State::Power::Awake;
  state_.powerCommandPending = false;
  state_.powerOffFailed = true;
  connectRequested_ = state_.link == Link::Connected;
}

void GoProClient::teardownConnection() {
  studio::ble::bleCentral().disconnect(linkHandle_);
  commandChar_ = nullptr;
  responseChar_ = nullptr;
  queryChar_ = nullptr;
  queryResponseChar_ = nullptr;
}

void GoProClient::handleDisconnect() {
  const bool expectedSleep = state_.power == State::Power::Sleeping &&
                             !connectRequested_;
  commandChar_ = nullptr;
  responseChar_ = nullptr;
  queryChar_ = nullptr;
  queryResponseChar_ = nullptr;
  setupPending_ = false;
  pairingResponsePending_ = false;
  readinessActive_ = false;
  hardwareResponsePending_ = false;
  encodingResponsePending_ = false;
  encodingQueryPending_ = false;
  commandRequested_ = false;
  resetTransientState(state_);
  state_.link = Link::Disconnected;
  if (expectedSleep) {
    sleepDisconnectObserved_ = true;
    completeSleepIfReady();
  } else if (state_.power == State::Power::Waking) {
    state_.power = State::Power::Asleep;
  }
}

void GoProClient::drainResponses() {
  if (responseQueue_ == nullptr) return;
  QueuedResponse queued;
  while (xQueueReceive(static_cast<QueueHandle_t>(responseQueue_), &queued, 0) == pdTRUE) {
    Message message;
    PacketAccumulator& packets = queued.channel == QueuedResponse::Channel::Command
                                     ? commandPackets_
                                     : queryPackets_;
    if (!packets.feed(queued.data, queued.len, message)) continue;
    if (queued.channel == QueuedResponse::Channel::Command) {
      const Response response = parseCommandPayload(message.bytes, message.len);
      if (!response.valid) continue;
      if (pairingResponsePending_ && response.command == kSetPairingStateCommand) {
        pairingResponsePending_ = false;
        if (response.status == kSuccessStatus) beginReadiness();
        else studio::ble::bleCentral().markProtocolFailed(linkHandle_);
      } else if (hardwareResponsePending_ &&
                 response.command == kGetHardwareInfoCommand) {
        hardwareResponsePending_ = false;
        if (response.status == kSuccessStatus) {
          if (!requestEncodingRegistration(false))
            studio::ble::bleCentral().markProtocolFailed(linkHandle_);
        } else {
          nextReadinessPollMs_ = millis() + 750;
        }
      } else if (state_.commandPending &&
                 response.command == kSetShutterCommand) {
        reduceCommandResponse(state_, response.status);
        if (response.status == kSuccessStatus && state_.commandPending) {
          if (!requestEncodingQuery()) markCommandTimeout(state_);
        } else {
          encodingQueryPending_ = false;
        }
      } else if (state_.power == State::Power::Sleeping &&
                 response.command == kSleepCommand) {
        if (response.status == kSuccessStatus) {
          sleepResponseAccepted_ = true;
          completeSleepIfReady();
          // A connected BLE central wakes a sleeping GoPro. End the link as
          // soon as Sleep is accepted so the camera can remain in standby;
          // the disconnect event is still required before publishing Asleep.
          if (!sleepDisconnectObserved_) teardownConnection();
        } else {
          connectRequested_ = true;
          failSleep();
        }
      }
    } else {
      const StatusResponse response =
          parseStatusPayload(message.bytes, message.len);
      if (!response.valid) continue;
      if (encodingResponsePending_ &&
          (response.operation == kRegisterStatusUpdates ||
           response.operation == kRegisterStatusUpdates2Byte)) {
        encodingResponsePending_ = false;
        if (response.status != kSuccessStatus || !response.hasEncoding) {
          if (!triedTwoByteStatus_) {
            triedTwoByteStatus_ = true;
            if (!requestEncodingRegistration(true))
              studio::ble::bleCentral().markProtocolFailed(linkHandle_);
          } else {
            studio::ble::bleCentral().markProtocolFailed(linkHandle_);
          }
          continue;
        }
        applyEncodingStatus(response.encoding);
        markReady();
      } else if (encodingQueryPending_ &&
                 (response.operation == kGetStatusValues ||
                  response.operation == kGetStatusValues2Byte)) {
        encodingQueryPending_ = false;
        if (response.status == kSuccessStatus && response.hasEncoding) {
          applyEncodingStatus(response.encoding);
        }
        if (state_.commandPending) nextEncodingPollMs_ = millis() + 250;
      } else if (response.status == kSuccessStatus && response.hasEncoding &&
                 (response.operation == kNotifyStatusUpdate ||
                  response.operation == kNotifyStatusUpdate2Byte)) {
        applyEncodingStatus(response.encoding);
      }
    }
  }
}

bool GoProClient::send(const uint8_t* data, size_t len) {
  return commandChar_ != nullptr && data != nullptr && len > 0 &&
         commandChar_->writeValue(data, len, !commandChar_->canWriteNoResponse());
}

bool GoProClient::sendQuery(const uint8_t* data, size_t len) {
  return queryChar_ != nullptr && data != nullptr && len > 0 &&
         queryChar_->writeValue(data, len, !queryChar_->canWriteNoResponse());
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
    if (state_.power == State::Power::Waking)
      state_.power = State::Power::Asleep;
  } else if (event.type == studio::ble::EventType::Disconnected) {
    client_ = nullptr;
    handleDisconnect();
  }
}

void GoProClient::onResponse(const void* characteristic, const uint8_t* data,
                             size_t len) {
  if (responseQueue_ == nullptr || data == nullptr || len == 0) return;
  QueuedResponse queued;
  queued.channel = characteristic == queryResponseChar_
                       ? QueuedResponse::Channel::Query
                       : QueuedResponse::Channel::Command;
  queued.len = static_cast<uint8_t>(len < sizeof(queued.data) ? len : sizeof(queued.data));
  std::memcpy(queued.data, data, queued.len);
  xQueueSend(static_cast<QueueHandle_t>(responseQueue_), &queued, 0);
}

bool GoProClient::protocolReady() const {
  return studio::ble::bleCentral().protocolReady(linkHandle_);
}

}  // namespace gopro
