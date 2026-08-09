#include "devices/dji_osmo/client.h"

#include <Arduino.h>
#include <NimBLEDevice.h>

#include <cstring>

#include "core/ble/ble_runtime.h"
#include "devices/dji_osmo/protocol.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

namespace dji_osmo {
namespace {
Client* gClients[4] = {};
constexpr uint8_t kMaxStatusSubscriptionAttempts = 3;
constexpr uint32_t kInitialStatusSubscriptionDelayMs = 100;
constexpr uint32_t kStatusSubscriptionRetryMs = 1000;

void notifyTrampoline(NimBLERemoteCharacteristic* characteristic, uint8_t* data,
                      size_t length, bool) {
  for (Client* client : gClients)
    if (client != nullptr && client->ownsNotifyCharacteristic(characteristic))
      client->onNotification(data, length);
}

void registerClient(Client* client) {
  for (Client*& slot : gClients) {
    if (slot == client) return;
    if (slot == nullptr) { slot = client; return; }
  }
}

void unregisterClient(Client* client) {
  for (Client*& slot : gClients) if (slot == client) slot = nullptr;
}
}  // namespace

void Client::begin() {
  if (initialized_) return;
  queue_ = xQueueCreate(8, sizeof(Chunk));
  if (queue_ == nullptr) return;
  studio::ble::ConnectPolicy policy;
  policy.security = studio::ble::SecurityPolicy::None;
  policy.connectTimeoutMs = 5000;
  policy.diagnosticTag = "dji_osmo";
  link_ = studio::ble::bleCentral().acquire(*this, policy);
  initialized_ = link_ != studio::ble::kInvalidLinkHandle;
  if (initialized_) registerClient(this);
  else { vQueueDelete(static_cast<QueueHandle_t>(queue_)); queue_ = nullptr; }
}

void Client::activate(const char* address, uint8_t addressType, const char* name,
                      bool paired, uint32_t deviceId, uint32_t cameraNumber) {
  begin();
  deviceId_ = deviceId == 0 ? 0x12345678 : deviceId;
  cameraNumber_ = cameraNumber;
  connectRequested_ = initialized_;
  haveTarget_ = paired && address != nullptr && address[0] != '\0';
  paired_ = paired;
  targetAddressType_ = addressType;
  if (haveTarget_) std::strncpy(targetAddress_, address, sizeof(targetAddress_) - 1);
  if (name != nullptr) std::strncpy(targetName_, name, sizeof(targetName_) - 1);
  std::strncpy(state_.deviceName, targetName_, sizeof(state_.deviceName) - 1);
  if (initialized_) haveTarget_ ? beginConnect() : beginScan();
}

void Client::deactivate() {
  connectRequested_ = false;
  studio::ble::bleCentral().release(link_);
  unregisterClient(this);
  link_ = studio::ble::kInvalidLinkHandle;
  initialized_ = false;
  client_ = nullptr; write_ = nullptr; notify_ = nullptr;
  state_ = {};
  if (queue_ != nullptr) { vQueueDelete(static_cast<QueueHandle_t>(queue_)); queue_ = nullptr; }
}

void Client::loop() {
  drainNotifications();
  if (!connectRequested_) return;
  const uint32_t now = millis();
  if (setupPending_) {
    setupPending_ = false;
    if (!completeGattSetup()) studio::ble::bleCentral().markProtocolFailed(link_);
  }
  if (handshakePending_ && static_cast<int32_t>(now - handshakeDeadlineMs_) >= 0) {
    handshakePending_ = false;
    studio::ble::bleCentral().markProtocolFailed(link_);
  }
  if (statusSubscriptionPending_ &&
      static_cast<int32_t>(now - statusSubscriptionAtMs_) >= 0) {
    ++statusSubscriptionAttempts_;
    if (send(buildStatusSubscription(sequence_++))) {
      if (!protocolReady()) markReady();
      statusSubscriptionPending_ =
          !state_.statusConfirmed &&
          statusSubscriptionAttempts_ < kMaxStatusSubscriptionAttempts;
      statusSubscriptionAtMs_ = now + kStatusSubscriptionRetryMs;
    } else if (statusSubscriptionAttempts_ >=
               kMaxStatusSubscriptionAttempts) {
      statusSubscriptionPending_ = false;
      if (!protocolReady())
        studio::ble::bleCentral().markProtocolFailed(link_);
    } else {
      statusSubscriptionAtMs_ = now + kStatusSubscriptionRetryMs;
    }
  }
  if (commandRequested_) {
    commandRequested_ = false;
    pendingRecordSequence_ = sequence_++;
    if (!send(buildRecordControl(pendingRecordSequence_, requestedRecording_))) {
      state_.commandPending = false;
      state_.lastCommandFailed = true;
      state_.recording = State::Recording::Unknown;
    } else commandDeadlineMs_ = now + 5000;
  }
  // A queued command has no valid deadline until its GATT write succeeds.
  // Process the write first so a stale/zero deadline cannot fail the scene
  // before the camera has received the command.
  if (state_.commandPending &&
      static_cast<int32_t>(now - commandDeadlineMs_) >= 0) {
    state_.commandPending = false;
    state_.lastCommandFailed = true;
    state_.recording = State::Recording::Unknown;
  }
}

void Client::startScan() { begin(); if (initialized_) { connectRequested_ = true; haveTarget_ = false; beginScan(); } }
void Client::beginScan() { state_.link = State::Link::Scanning; studio::ble::bleCentral().requestScan(link_, true); }
void Client::beginConnect() {
  state_.link = State::Link::Connecting;
  studio::ble::Address address; address.type = targetAddressType_;
  std::strncpy(address.value, targetAddress_, sizeof(address.value) - 1);
  studio::ble::bleCentral().requestConnect(link_, address);
}

void Client::forgetDevice() {
  forgetBond(targetAddress_, targetAddressType_);
  haveTarget_ = false; paired_ = false;
  targetAddress_[0] = '\0'; targetName_[0] = '\0';
  pairingChanged_ = true; startScan();
}

void Client::forgetBond(const char* address, uint8_t addressType) {
  if (address == nullptr || address[0] == '\0') return;
  studio::ble::Address peer; peer.type = addressType;
  std::strncpy(peer.value, address, sizeof(peer.value) - 1);
  studio::ble::bleCentral().deleteBond(peer);
}

bool Client::setRecording(bool start) {
  if (!protocolReady() || state_.commandPending) return false;
  requestedRecording_ = start; commandRequested_ = true;
  state_.commandPending = true; state_.lastCommandFailed = false;
  state_.recording = start ? State::Recording::Starting : State::Recording::Stopping;
  return true;
}

bool Client::completeGattSetup() {
  if (client_ == nullptr || !client_->isConnected()) return false;
  NimBLERemoteService* service = client_->getService(NimBLEUUID(kServiceUuid));
  if (service == nullptr) return false;
  notify_ = service->getCharacteristic(NimBLEUUID(kNotifyUuid));
  write_ = service->getCharacteristic(NimBLEUUID(kWriteUuid));
  if (notify_ == nullptr || write_ == nullptr ||
      !notify_->subscribe(true, notifyTrampoline, true)) return false;
  uint8_t localAddress[6] = {};
  const uint8_t* native = NimBLEDevice::getAddress().getVal();
  if (native != nullptr) std::memcpy(localAddress, native, sizeof(localAddress));
  const uint16_t verification = static_cast<uint16_t>(esp_random() % 10000);
  const uint8_t verificationMode = paired_ ? 0 : 1;
  if (!send(buildConnectionRequest(sequence_++, deviceId_, localAddress,
                                   verificationMode, verification)))
    return false;
  state_.verificationCode = verification;
  state_.verificationPending = true;
  handshakePending_ = true;
  handshakeDeadlineMs_ = millis() + 30000;
  return true;
}

void Client::onBleAdvertisement(studio::ble::LinkHandle link,
                                const studio::ble::Advertisement& advertisement) {
  if (link != link_ || !studio::ble::advertisesService(advertisement, kServiceUuid)) return;
  std::strncpy(targetAddress_, advertisement.address.value, sizeof(targetAddress_) - 1);
  targetAddressType_ = advertisement.address.type;
  studio::ble::advertisementName(advertisement, targetName_, sizeof(targetName_));
  haveTarget_ = true; state_.link = State::Link::Connecting;
  studio::ble::bleCentral().selectAdvertisement(link_, advertisement);
}

void Client::onBleEvent(studio::ble::LinkHandle link, const studio::ble::Event& event) {
  if (link != link_) return;
  if (event.type == studio::ble::EventType::Connected) {
    client_ = static_cast<NimBLEClient*>(studio::ble::bleCentral().nativeClient(link_));
    setupPending_ = true;
  } else if (event.type == studio::ble::EventType::ConnectFailed) {
    state_.link = State::Link::Disconnected;
  } else if (event.type == studio::ble::EventType::Disconnected) {
    handleDisconnect();
  }
}

void Client::onNotification(const uint8_t* data, size_t length) {
  if (queue_ == nullptr || data == nullptr || length == 0) return;
  Chunk chunk; chunk.length = static_cast<uint8_t>(length < sizeof(chunk.data) ? length : sizeof(chunk.data));
  std::memcpy(chunk.data, data, chunk.length);
  xQueueSend(static_cast<QueueHandle_t>(queue_), &chunk, 0);
}

void Client::drainNotifications() {
  if (queue_ == nullptr) return;
  Chunk chunk;
  while (xQueueReceive(static_cast<QueueHandle_t>(queue_), &chunk, 0) == pdTRUE)
    consumeBytes(chunk.data, chunk.length);
}

void Client::consumeBytes(const uint8_t* data, size_t length) {
  if (length > sizeof(stream_) - streamLength_) { streamLength_ = 0; }
  if (length > sizeof(stream_)) return;
  std::memcpy(stream_ + streamLength_, data, length); streamLength_ += length;
  while (streamLength_ != 0) {
    size_t start = 0; while (start < streamLength_ && stream_[start] != 0xaa) ++start;
    if (start != 0) { std::memmove(stream_, stream_ + start, streamLength_ - start); streamLength_ -= start; }
    if (streamLength_ < 3) return;
    const size_t frameLength = declaredFrameLength(stream_, streamLength_);
    if (frameLength < 18 || frameLength > sizeof(stream_)) { std::memmove(stream_, stream_ + 1, --streamLength_); continue; }
    if (streamLength_ < frameLength) return;
    handleFrame(stream_, frameLength);
    std::memmove(stream_, stream_ + frameLength, streamLength_ - frameLength);
    streamLength_ -= frameLength;
  }
}

void Client::handleFrame(const uint8_t* data, size_t length) {
  const Frame frame = parseFrame(data, length);
  if (!frame.valid) return;
  bool approved = false;
  if (parseConnectionApproval(frame, approved)) {
    if (!approved) {
      state_.verificationPending = false;
      studio::ble::bleCentral().markProtocolFailed(link_, false);
      return;
    }
    if (send(buildConnectionResponse(frame.sequence, deviceId_, cameraNumber_))) {
      statusSubscriptionAttempts_ = 0;
      statusSubscriptionPending_ = true;
      statusSubscriptionAtMs_ = millis() + kInitialStatusSubscriptionDelayMs;
    } else {
      studio::ble::bleCentral().markProtocolFailed(link_);
    }
    return;
  }
  if (frame.commandSet == kCmdSetCamera && frame.commandId == kCmdRecord &&
      (frame.commandType & 0x20) != 0 && frame.sequence == pendingRecordSequence_ &&
      frame.payloadLength >= 1) {
    state_.commandPending = false;
    state_.lastCommandFailed = frame.payload[0] != 0;
    if (frame.payload[0] != 0) state_.recording = State::Recording::Unknown;
    else if (!state_.statusConfirmed)
      state_.recording = requestedRecording_ ? State::Recording::Recording : State::Recording::Stopped;
    return;
  }
  if (frame.commandSet == kCmdSetCamera && frame.commandId == kCmdStatusPush &&
      frame.payloadLength >= 2) {
    const uint8_t mode = frame.payload[0];
    const uint8_t status = frame.payload[1];
    bool recording = false;
    if (decodeCameraRecordingStatus(mode, status, recording)) {
      state_.statusConfirmed = true;
      state_.recording = recording ? State::Recording::Recording
                                   : State::Recording::Stopped;
      statusSubscriptionPending_ = false;
    }
    if (frame.payloadLength >= 37) state_.battery = frame.payload[frame.payloadLength - 1];
  }
}

bool Client::send(const Packet& packet) {
  return write_ != nullptr && packet.len != 0 &&
         write_->writeValue(packet.bytes, packet.len, false);
}

void Client::markReady() {
  handshakePending_ = false; state_.link = State::Link::Connected;
  state_.verificationPending = false;
  haveTarget_ = true; paired_ = true; pairingChanged_ = true;
  studio::ble::bleCentral().markProtocolReady(link_);
}

void Client::handleDisconnect() {
  client_ = nullptr; write_ = nullptr; notify_ = nullptr; setupPending_ = false;
  handshakePending_ = false; statusSubscriptionPending_ = false;
  statusSubscriptionAttempts_ = 0; commandRequested_ = false; streamLength_ = 0;
  state_.link = State::Link::Disconnected; state_.recording = State::Recording::Unknown;
  state_.commandPending = false; state_.statusConfirmed = false;
  state_.verificationPending = false;
}

bool Client::protocolReady() const { return studio::ble::bleCentral().protocolReady(link_); }

bool Client::consumePairingUpdate(char* address, size_t addressCapacity,
                                  uint8_t& addressType, char* name,
                                  size_t nameCapacity, bool& paired) {
  if (!pairingChanged_) return false;
  pairingChanged_ = false; paired = haveTarget_; addressType = targetAddressType_;
  if (addressCapacity != 0) { std::strncpy(address, targetAddress_, addressCapacity - 1); address[addressCapacity - 1] = '\0'; }
  if (nameCapacity != 0) { std::strncpy(name, targetName_, nameCapacity - 1); name[nameCapacity - 1] = '\0'; }
  return true;
}

}  // namespace dji_osmo
