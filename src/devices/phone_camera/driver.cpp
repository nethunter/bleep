#include "devices/phone_camera/driver.h"

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <cstring>
#include <new>
#include <vector>

#include "core/ble/ble_runtime.h"
#include "core/ble/peripheral_dispatcher.h"
#include "devices/camera_peripheral/gatt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

namespace studio {
namespace {

constexpr uint16_t kInvalidConnHandle = 0xffff;
constexpr uint32_t kReleaseDelayMs = 120;
}  // namespace

class PhoneCameraDriver::Runtime : public ble::BleCentralDelegate,
                                   public ble::PeripheralListener {
 public:
  enum class EventType : uint8_t { Connected, Authenticated, Disconnected };
  struct Event {
    EventType type = EventType::Disconnected;
    uint16_t connHandle = kInvalidConnHandle;
    bool succeeded = false;
    uint8_t addressType = 0;
    char address[kBleAddressCapacity] = "";
  };

  bool begin() {
    if (initialized_) return true;
    queue_ = xQueueCreate(12, sizeof(Event));
    if (queue_ == nullptr) return false;
    ble::ConnectPolicy policy;
    policy.diagnosticTag = "phone_hid_radio";
    radioHandle_ = ble::bleCentral().acquire(*this, policy);
    if (radioHandle_ == ble::kInvalidLinkHandle) {
      vQueueDelete(queue_);
      queue_ = nullptr;
      return false;
    }
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
    NimBLEDevice::setSecurityAuth(true, false, true);
    server_ = NimBLEDevice::createServer();
    if (server_ == nullptr) return failBegin();
    if (!ble::registerPeripheralListener(server_, this)) return failBegin();
    camera_peripheral::GattServices services;
    if (!camera_peripheral::ensureGattServices(server_, services)) {
      return failBegin();
    }
    input_ = services.phoneInput;
    initialized_ = input_ != nullptr;
    return initialized_;
  }

  bool advertise(const void* owner, const char* peerAddress,
                 uint8_t peerAddressType) {
    if (!initialized_ || shutdownPending_) return false;
    const ble::PeripheralAdvertisement advertisement{
        "phone_camera", "Ble(e)p Shutter", "1812", 0x03c0, 10,
        peerAddress, peerAddressType};
    return ble::requestPeripheralAdvertising(owner, advertisement, millis());
  }

  void stopAdvertising(const void* owner) {
    ble::releasePeripheralAdvertising(owner, millis());
  }

  bool startSecurity(uint16_t connHandle) {
    return NimBLEDevice::startSecurity(connHandle);
  }

  bool sendVolumeUp(uint16_t connHandle, bool pressed) {
    const uint8_t report = pressed ? 0x01 : 0x00;
    return initialized_ && input_ != nullptr &&
           input_->notify(&report, 1, connHandle);
  }

  bool pop(Event& event) {
    return queue_ != nullptr && xQueueReceive(queue_, &event, 0) == pdTRUE;
  }

  void shutdown() {
    if (shutdownPending_) return;
    shutdownPending_ = true;
    if (server_ != nullptr) {
      const std::vector<uint16_t> peers = server_->getPeerDevices();
      for (uint16_t handle : peers) server_->disconnect(handle);
    }
  }

  bool finishShutdown() {
    if (!shutdownPending_ || (server_ != nullptr && server_->getConnectedCount() != 0)) {
      return false;
    }
    ble::unregisterPeripheralListener(this);
    input_ = nullptr;
    server_ = nullptr;
    ble::bleCentral().release(radioHandle_);
    radioHandle_ = ble::kInvalidLinkHandle;
    if (queue_ != nullptr) vQueueDelete(queue_);
    queue_ = nullptr;
    initialized_ = false;
    shutdownPending_ = false;
    return true;
  }

  void onBleAdvertisement(ble::LinkHandle,
                          const ble::Advertisement&) override {}
  void onBleEvent(ble::LinkHandle, const ble::Event&) override {}
  bool acceptsPeripheralPeer(const char*) const override { return false; }
  bool defaultPeripheralPeer() const override { return true; }
  void onPeripheralConnected(NimBLEConnInfo& info) override {
    push(EventType::Connected, info, true);
  }
  void onPeripheralDisconnected(NimBLEConnInfo& info) override {
    push(EventType::Disconnected, info, true);
  }
  void onPeripheralAuthentication(NimBLEConnInfo& info) override {
    push(EventType::Authenticated, info, info.isEncrypted() && info.isBonded());
  }

 private:
  bool failBegin() {
    shutdownPending_ = true;
    finishShutdown();
    return false;
  }

  void push(EventType type, NimBLEConnInfo& info, bool succeeded) {
    if (queue_ == nullptr) return;
    Event event;
    event.type = type;
    event.connHandle = info.getConnHandle();
    event.succeeded = succeeded;
    const NimBLEAddress address =
        type == EventType::Connected ? info.getAddress() : info.getIdAddress();
    event.addressType = address.getType();
    std::strncpy(event.address, address.toString().c_str(),
                 sizeof(event.address) - 1);
    xQueueSend(queue_, &event, 0);
  }

  QueueHandle_t queue_ = nullptr;
  NimBLEServer* server_ = nullptr;
  NimBLECharacteristic* input_ = nullptr;
  ble::LinkHandle radioHandle_ = ble::kInvalidLinkHandle;
  bool initialized_ = false;
  bool shutdownPending_ = false;
};

PhoneCameraDriver::Session* PhoneCameraDriver::sessionFor(InstanceId id) {
  for (Session* session : sessions_)
    if (session != nullptr && session->instanceId == id) return session;
  return nullptr;
}

const PhoneCameraDriver::Session* PhoneCameraDriver::sessionFor(InstanceId id) const {
  for (const Session* session : sessions_)
    if (session != nullptr && session->instanceId == id) return session;
  return nullptr;
}

PhoneCameraDriver::Session* PhoneCameraDriver::sessionForAddress(const char* address) {
  for (Session* session : sessions_)
    if (session != nullptr && session->paired &&
        std::strcmp(session->address, address) == 0) return session;
  return nullptr;
}

PhoneCameraDriver::Session* PhoneCameraDriver::firstAwaitingSession() {
  for (Session* session : sessions_)
    if (session != nullptr && !session->paired) return session;
  return nullptr;
}

bool PhoneCameraDriver::activate(const DeviceRecord& record) {
  if (sessionFor(record.instanceId) != nullptr) return true;
  if (runtime_ == nullptr) {
    runtime_ = new (std::nothrow) Runtime;
    if (runtime_ == nullptr || !runtime_->begin()) {
      delete runtime_;
      runtime_ = nullptr;
      return false;
    }
  }
  for (Session*& session : sessions_) {
    if (session != nullptr) continue;
    session = new (std::nothrow) Session;
    if (session == nullptr) return false;
    session->instanceId = record.instanceId;
    session->paired = record.paired && record.bleAddress[0] != '\0';
    session->addressType = record.bleAddressType;
    std::strncpy(session->address, record.bleAddress, sizeof(session->address) - 1);
    std::strncpy(session->state.deviceName,
                 record.bleName[0] != '\0' ? record.bleName : record.displayName,
                 sizeof(session->state.deviceName) - 1);
    session->state.link = phone_camera::PhoneCameraState::Link::Advertising;
    startAdvertisingIfNeeded();
    return true;
  }
  return false;
}

void PhoneCameraDriver::deactivate(InstanceId id) {
  Session* session = sessionFor(id);
  if (session == nullptr) return;
  for (Session*& candidate : sessions_) {
    if (candidate == session) {
      if (runtime_ != nullptr) runtime_->stopAdvertising(candidate);
      delete candidate;
      candidate = nullptr;
      break;
    }
  }
  bool any = false;
  for (Session* candidate : sessions_) any |= candidate != nullptr;
  if (!any && runtime_ != nullptr) runtime_->shutdown();
}

void PhoneCameraDriver::loop() {
  if (runtime_ == nullptr) return;
  Runtime::Event event;
  while (runtime_->pop(event)) {
    if (event.type == Runtime::EventType::Connected) {
      runtime_->startSecurity(event.connHandle);
      continue;
    }
    Session* session = sessionForAddress(event.address);
    if (event.type == Runtime::EventType::Authenticated) {
      if (session == nullptr) session = firstAwaitingSession();
      if (session == nullptr || !event.succeeded) continue;
      session->connHandle = event.connHandle;
      session->paired = true;
      session->pairingChanged = true;
      session->addressType = event.addressType;
      std::strncpy(session->address, event.address, sizeof(session->address) - 1);
      session->state.link = phone_camera::PhoneCameraState::Link::Connected;
    } else if (event.type == Runtime::EventType::Disconnected) {
      for (Session* candidate : sessions_) {
        if (candidate == nullptr || candidate->connHandle != event.connHandle) continue;
        candidate->connHandle = kInvalidConnHandle;
        candidate->triggerRequested = false;
        candidate->releasePending = false;
        candidate->state.triggerPending = false;
        candidate->state.link = phone_camera::PhoneCameraState::Link::Advertising;
      }
    }
  }
  const uint32_t now = millis();
  for (Session* session : sessions_) {
    if (session == nullptr || session->connHandle == kInvalidConnHandle) continue;
    if (session->triggerRequested) {
      session->triggerRequested = false;
      if (runtime_->sendVolumeUp(session->connHandle, true)) {
        session->releasePending = true;
        session->releaseAtMs = now + kReleaseDelayMs;
      } else {
        session->state.triggerPending = false;
        session->state.lastTriggerSucceeded = false;
        session->state.lastTriggerFailed = true;
      }
    }
    if (session->releasePending &&
        static_cast<int32_t>(now - session->releaseAtMs) >= 0) {
      session->releasePending = false;
      const bool sent = runtime_->sendVolumeUp(session->connHandle, false);
      session->state.triggerPending = false;
      session->state.lastTriggerSucceeded = sent;
      session->state.lastTriggerFailed = !sent;
      if (sent) ++session->state.triggerCount;
    }
  }
  startAdvertisingIfNeeded();
  bool any = false;
  for (Session* session : sessions_) any |= session != nullptr;
  if (!any && runtime_->finishShutdown()) {
    delete runtime_;
    runtime_ = nullptr;
  }
}

void PhoneCameraDriver::startAdvertisingIfNeeded() {
  if (runtime_ == nullptr) return;
  for (Session* session : sessions_) {
    if (session == nullptr) continue;
    if (session->connHandle == kInvalidConnHandle) {
      runtime_->advertise(session,
                          session->paired ? session->address : nullptr,
                          session->addressType);
    } else {
      runtime_->stopAdvertising(session);
    }
  }
}

CommandStatus PhoneCameraDriver::dispatch(const DeviceCommand& command) {
  Session* session = sessionFor(command.instanceId);
  if (session == nullptr) return CommandStatus::Unavailable;
  if (command.type == CommandType::RecordTrigger) {
    if (session->connHandle == kInvalidConnHandle || session->state.triggerPending)
      return CommandStatus::Unavailable;
    session->state.triggerPending = true;
    session->state.lastTriggerSucceeded = false;
    session->state.lastTriggerFailed = false;
    session->triggerRequested = true;
    return CommandStatus::Succeeded;
  }
  if (command.type == CommandType::ForgetPairing) {
    session->paired = false;
    session->pairingChanged = true;
    session->address[0] = '\0';
    return CommandStatus::Succeeded;
  }
  return CommandStatus::Unsupported;
}

DeviceRuntimeState PhoneCameraDriver::runtimeState(InstanceId id) const {
  DeviceRuntimeState runtime;
  const Session* session = sessionFor(id);
  if (session == nullptr) return runtime;
  switch (session->state.link) {
    case phone_camera::PhoneCameraState::Link::Advertising: runtime.link = LinkState::Scanning; break;
    case phone_camera::PhoneCameraState::Link::Connecting: runtime.link = LinkState::Connecting; break;
    case phone_camera::PhoneCameraState::Link::Connected: runtime.link = LinkState::Connected; break;
    case phone_camera::PhoneCameraState::Link::Disconnected: runtime.link = LinkState::Disconnected; break;
  }
  runtime.protocolReady = session->connHandle != kInvalidConnHandle;
  runtime.commandPending = session->state.triggerPending;
  runtime.commandFailed = session->state.lastTriggerFailed;
  return runtime;
}

const void* PhoneCameraDriver::specializedState(InstanceId id) const {
  const Session* session = sessionFor(id);
  return session != nullptr ? &session->state : nullptr;
}

void PhoneCameraDriver::forgetPairing(const DeviceRecord& record) {
  ble::Address peer;
  std::strncpy(peer.value, record.bleAddress, sizeof(peer.value) - 1);
  peer.type = record.bleAddressType;
  ble::bleCentral().deleteBond(peer);
}

bool PhoneCameraDriver::cancelOnboarding(const DeviceRecord& record) {
  forgetPairing(record);
  return true;
}

bool PhoneCameraDriver::consumePairingUpdate(InstanceId id, DeviceRecord& record) {
  Session* session = sessionFor(id);
  if (session == nullptr || !session->pairingChanged) return false;
  session->pairingChanged = false;
  record.paired = session->paired;
  record.bleAddressType = session->addressType;
  std::strncpy(record.bleAddress, session->address, sizeof(record.bleAddress) - 1);
  std::strncpy(record.bleName, "Phone", sizeof(record.bleName) - 1);
  return true;
}

}  // namespace studio
