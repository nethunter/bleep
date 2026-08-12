#include "devices/insta360/driver.h"

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <cstring>
#include <new>
#include <vector>

#include "core/ble/ble_runtime.h"
#include "core/ble/peripheral_dispatcher.h"
#include "devices/camera_peripheral/gatt.h"
#include "devices/insta360/protocol.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

namespace studio {
namespace {

constexpr uint16_t kNoConnection = 0xffff;
constexpr uint32_t kCommandTimeoutMs = 10000;
constexpr uint32_t kWakeTimeoutMs = 60000;
constexpr uint32_t kInitialSyncTimeoutMs = 15000;
constexpr size_t kMaxCameraWriteLength = 20;
constexpr uint8_t kInitialDiagnosticWriteLimit = 16;

bool deadlineReached(uint32_t now, uint32_t deadline) {
  return deadline != 0 && static_cast<int32_t>(now - deadline) >= 0;
}

Print& diagnosticOutput() {
#if ARDUINO_USB_CDC_ON_BOOT
  return Serial0;
#else
  return Serial;
#endif
}

const char* captureModeName(insta360::CaptureMode mode) {
  return mode == insta360::CaptureMode::Video ? "video" : "photo";
}

const char* capturePhaseName(insta360::CapturePhase phase) {
  switch (phase) {
    case insta360::CapturePhase::Idle: return "idle";
    case insta360::CapturePhase::Starting: return "starting";
    case insta360::CapturePhase::Active: return "active";
    case insta360::CapturePhase::Stopping: return "stopping";
    case insta360::CapturePhase::Saving: return "saving";
    default: return "unknown";
  }
}

}  // namespace

class Insta360Driver::Runtime : public ble::BleCentralDelegate,
                                public ble::PeripheralListener,
                                private NimBLECharacteristicCallbacks {
 public:
  enum class EventType : uint8_t {
    Connected,
    Disconnected,
    CameraWrite,
    Subscription,
  };
  struct Event {
    EventType type = EventType::Disconnected;
    uint16_t handle = kNoConnection;
    uint8_t addressType = 0;
    uint8_t length = 0;
    uint16_t actualLength = 0;
    uint16_t subscriptionValue = 0;
    uint8_t data[kMaxCameraWriteLength] = {};
    char address[kBleAddressCapacity] = "";
    char name[kBleNameCapacity] = "";
  };

  bool begin() {
    queue_ = xQueueCreate(24, sizeof(Event));
    if (queue_ == nullptr) return false;
    ble::ConnectPolicy policy;
    policy.diagnosticTag = "insta360_remote";
    radio_ = ble::bleCentral().acquire(*this, policy);
    if (radio_ == ble::kInvalidLinkHandle) return fail();
    server_ = NimBLEDevice::createServer();
    if (server_ == nullptr || !ble::registerPeripheralListener(server_, this)) {
      return fail();
    }
    camera_peripheral::GattServices services;
    if (!camera_peripheral::ensureGattServices(server_, services)) return fail();
    write_ = services.instaWrite;
    notify_ = services.instaNotify;
    if (write_ == nullptr || notify_ == nullptr) return fail();
    write_->setCallbacks(this);
    notify_->setCallbacks(this);
    initialized_ = true;
    return true;
  }

  bool startCameraScan() {
    return initialized_ && ble::bleCentral().requestScan(radio_, true);
  }

  bool advertiseNormal() {
    if (!initialized_ || shutdown_) return false;
    if (advertisingMode_ != AdvertisingMode::Normal) {
      ble::releasePeripheralAdvertising(this, millis());
      advertisingMode_ = AdvertisingMode::Normal;
    }
    return advertiseGpsIdentity();
  }

  bool startWake(const char* cameraName) {
    if (!initialized_ || shutdown_) return false;
    if (!insta360::buildWakeAdvertisementData(
            cameraName, wakeAdvertisementData_, wakeScanResponseData_)) {
      return false;
    }
    wakeDeadlineMs_ = millis() + kWakeTimeoutMs;
    wakeTimedOut_ = false;
    if (advertisingMode_ != AdvertisingMode::Wake) {
      ble::releasePeripheralAdvertising(this, millis());
      advertisingMode_ = AdvertisingMode::Wake;
    }
    return maintainWake();
  }

  bool maintainWake() {
    if (advertisingMode_ != AdvertisingMode::Wake) return false;
    if (deadlineReached(millis(), wakeDeadlineMs_)) {
      wakeTimedOut_ = true;
      stopAdvertising();
      return false;
    }
    return advertiseWakeIdentity();
  }

  bool consumeWakeTimeout() {
    const bool timedOut = wakeTimedOut_;
    wakeTimedOut_ = false;
    return timedOut;
  }

  void cancelWake() {
    if (advertisingMode_ == AdvertisingMode::Wake) stopAdvertising();
    wakeTimedOut_ = false;
  }

  void stopAdvertising() {
    ble::releasePeripheralAdvertising(this, millis());
    advertisingMode_ = AdvertisingMode::None;
    wakeDeadlineMs_ = 0;
  }

  bool notifyCommand(uint16_t handle, const uint8_t* data, size_t len) {
    return notify_ != nullptr && handle != kNoConnection &&
           notify_->notify(data, len, handle);
  }

  bool pop(Event& event) {
    return queue_ != nullptr && xQueueReceive(queue_, &event, 0) == pdTRUE;
  }

  void shutdown() {
    if (shutdown_) return;
    shutdown_ = true;
    stopAdvertising();
    if (server_ != nullptr) {
      for (uint16_t handle : server_->getPeerDevices()) {
        server_->disconnect(handle);
      }
    }
  }

  bool finishShutdown() {
    if (!shutdown_ || (server_ != nullptr && server_->getConnectedCount())) {
      return false;
    }
    if (write_ != nullptr) write_->setCallbacks(nullptr);
    if (notify_ != nullptr) notify_->setCallbacks(nullptr);
    ble::unregisterPeripheralListener(this);
    server_ = nullptr;
    write_ = nullptr;
    notify_ = nullptr;
    ble::bleCentral().release(radio_);
    radio_ = ble::kInvalidLinkHandle;
    if (queue_ != nullptr) vQueueDelete(queue_);
    queue_ = nullptr;
    initialized_ = false;
    shutdown_ = false;
    return true;
  }

  void onBleAdvertisement(ble::LinkHandle link,
                          const ble::Advertisement& advertisement) override {
    if (link != radio_) return;
    char name[kBleNameCapacity] = "";
    if (!ble::advertisementName(advertisement, name, sizeof(name)) ||
        !insta360::matchesCameraName(name)) {
      return;
    }
    rememberCandidate(advertisement.address.value,
                      advertisement.address.type, name);
  }

  void rememberCandidate(const char* address, uint8_t type,
                         const char* name) {
    if (address == nullptr || address[0] == '\0') return;
    Candidate* candidate = nullptr;
    for (Candidate& item : candidates_) {
      if (std::strcmp(item.address, address) == 0 ||
          item.address[0] == '\0') {
        candidate = &item;
        break;
      }
    }
    if (candidate == nullptr) return;
    std::strncpy(candidate->address, address,
                 sizeof(candidate->address) - 1);
    candidate->type = type;
    if (name != nullptr) {
      std::strncpy(candidate->name, name, sizeof(candidate->name) - 1);
    }
  }

  void onBleEvent(ble::LinkHandle, const ble::Event&) override {}

  bool acceptsPeripheralPeer(const char* address) const override {
    // ORBIT is serial-addressed and only active for one explicit wake request.
    // Claim its returning central even if the X5 presents a new unresolved
    // address; otherwise the generic Phone Camera fallback can own the link.
    if (advertisingMode_ == AdvertisingMode::Wake) return true;
    for (const Candidate& candidate : candidates_) {
      if (candidate.address[0] != '\0' &&
          std::strcmp(candidate.address, address) == 0) {
        return true;
      }
    }
    return false;
  }

  void onPeripheralConnected(NimBLEConnInfo& info) override {
    pushConnection(EventType::Connected, info);
  }

  void onPeripheralDisconnected(NimBLEConnInfo& info) override {
    pushConnection(EventType::Disconnected, info);
  }

  void onPeripheralAuthentication(NimBLEConnInfo&) override {}

 private:
  enum class AdvertisingMode : uint8_t { None, Normal, Wake };
  struct Candidate {
    char address[kBleAddressCapacity] = "";
    uint8_t type = 0;
    char name[kBleNameCapacity] = "";
  };

  bool advertiseGpsIdentity() {
    ble::PeripheralAdvertisement advertisement;
    advertisement.diagnosticTag = "insta360";
    advertisement.rawAdvertisementData = insta360::kAdvertisementData;
    advertisement.rawAdvertisementDataLength =
        sizeof(insta360::kAdvertisementData);
    advertisement.rawScanResponseData = insta360::kScanResponseData;
    advertisement.rawScanResponseDataLength =
        sizeof(insta360::kScanResponseData);
    return ble::requestPeripheralAdvertising(this, advertisement, millis());
  }

  bool advertiseWakeIdentity() {
    ble::PeripheralAdvertisement advertisement;
    advertisement.diagnosticTag = "insta360_wake";
    advertisement.rawAdvertisementData = wakeAdvertisementData_;
    advertisement.rawAdvertisementDataLength = sizeof(wakeAdvertisementData_);
    advertisement.rawScanResponseData = wakeScanResponseData_;
    advertisement.rawScanResponseDataLength = sizeof(wakeScanResponseData_);
    return ble::requestPeripheralAdvertising(this, advertisement, millis());
  }

  bool fail() {
    shutdown_ = true;
    finishShutdown();
    return false;
  }

  void pushConnection(EventType type, NimBLEConnInfo& info) {
    if (queue_ == nullptr) return;
    Event event;
    event.type = type;
    event.handle = info.getConnHandle();
    NimBLEAddress address = info.getIdAddress();
    if (type == EventType::Connected) {
      const NimBLEAddress currentAddress = info.getAddress();
      address = acceptsPeripheralPeer(currentAddress.toString().c_str())
                    ? currentAddress
                    : info.getIdAddress();
    }
    event.addressType = address.getType();
    std::strncpy(event.address, address.toString().c_str(),
                 sizeof(event.address) - 1);
    for (const Candidate& candidate : candidates_) {
      if (std::strcmp(candidate.address, event.address) == 0) {
        std::strncpy(event.name, candidate.name, sizeof(event.name) - 1);
        break;
      }
    }
    xQueueSend(queue_, &event, 0);
  }

  void onWrite(NimBLECharacteristic* characteristic,
               NimBLEConnInfo& info) override {
    if (queue_ == nullptr || characteristic != write_) return;
    const NimBLEAttValue& value = characteristic->getValue();
    Event event;
    event.type = EventType::CameraWrite;
    event.handle = info.getConnHandle();
    event.actualLength = static_cast<uint16_t>(value.size());
    event.length = value.size() <= kMaxCameraWriteLength
                       ? static_cast<uint8_t>(value.size())
                       : 0;
    if (event.length != 0) {
      std::memcpy(event.data, value.data(), event.length);
    }
    xQueueSend(queue_, &event, 0);
  }

  void onSubscribe(NimBLECharacteristic* characteristic,
                   NimBLEConnInfo& info, uint16_t subValue) override {
    if (queue_ == nullptr || characteristic != notify_) return;
    Event event;
    event.type = EventType::Subscription;
    event.handle = info.getConnHandle();
    event.subscriptionValue = subValue;
    xQueueSend(queue_, &event, 0);
  }

  QueueHandle_t queue_ = nullptr;
  NimBLEServer* server_ = nullptr;
  NimBLECharacteristic* write_ = nullptr;
  NimBLECharacteristic* notify_ = nullptr;
  ble::LinkHandle radio_ = ble::kInvalidLinkHandle;
  Candidate candidates_[8] = {};
  uint8_t wakeAdvertisementData_[insta360::kWakeAdvertisementDataLength] = {};
  uint8_t wakeScanResponseData_[insta360::kWakeScanResponseDataLength] = {};
  uint32_t wakeDeadlineMs_ = 0;
  AdvertisingMode advertisingMode_ = AdvertisingMode::None;
  bool initialized_ = false;
  bool shutdown_ = false;
  bool wakeTimedOut_ = false;
};

Insta360Driver::Session* Insta360Driver::sessionFor(InstanceId id) {
  for (Session* session : sessions_) {
    if (session != nullptr && session->instanceId == id) return session;
  }
  return nullptr;
}

const Insta360Driver::Session* Insta360Driver::sessionFor(InstanceId id) const {
  for (const Session* session : sessions_) {
    if (session != nullptr && session->instanceId == id) return session;
  }
  return nullptr;
}

Insta360Driver::Session* Insta360Driver::sessionForAddress(const char* address) {
  for (Session* session : sessions_) {
    if (session != nullptr && session->paired &&
        std::strcmp(session->address, address) == 0) {
      return session;
    }
  }
  return nullptr;
}

Insta360Driver::Session* Insta360Driver::sessionForHandle(uint16_t handle) {
  for (Session* session : sessions_) {
    if (session != nullptr && session->connHandle == handle) return session;
  }
  return nullptr;
}

Insta360Driver::Session* Insta360Driver::firstAwaiting() {
  for (Session* session : sessions_) {
    if (session != nullptr && !session->paired) return session;
  }
  return nullptr;
}

Insta360Driver::Session* Insta360Driver::firstPoweringOn() {
  for (Session* session : sessions_) {
    if (session != nullptr &&
        session->state.power == insta360::State::Power::PoweringOn) {
      return session;
    }
  }
  return nullptr;
}

bool Insta360Driver::activate(const DeviceRecord& record) {
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
    std::strncpy(session->address, record.bleAddress,
                 sizeof(session->address) - 1);
    std::strncpy(session->state.deviceName,
                 record.bleName[0] != '\0' ? record.bleName
                                            : record.displayName,
                 sizeof(session->state.deviceName) - 1);
    session->state.goUltraExperimental =
        insta360::isGoUltra(session->state.deviceName);
    session->state.link = insta360::State::Link::Scanning;
    if (session->paired) {
      runtime_->rememberCandidate(session->address, session->addressType,
                                  session->state.deviceName);
    }
    updateAdvertising();
    runtime_->startCameraScan();
    return true;
  }
  return false;
}

bool Insta360Driver::retainWhileDisconnected(InstanceId id) const {
  const Session* session = sessionFor(id);
  return session != nullptr && session->state.power == insta360::State::Power::Off;
}

void Insta360Driver::deactivate(InstanceId id) {
  Session* session = sessionFor(id);
  if (session == nullptr) return;
  for (Session*& item : sessions_) {
    if (item == session) {
      delete item;
      item = nullptr;
      break;
    }
  }
  bool any = false;
  for (Session* item : sessions_) any |= item != nullptr;
  if (!any && runtime_ != nullptr) runtime_->shutdown();
}

void Insta360Driver::loop() {
  if (runtime_ == nullptr) return;
  Runtime::Event event;
  while (runtime_->pop(event)) {
    if (event.type == Runtime::EventType::CameraWrite) {
      Session* session = sessionForHandle(event.handle);
      if (session == nullptr) continue;
      if (session->diagnosticWritesRemaining != 0) {
        --session->diagnosticWritesRemaining;
        ++session->diagnosticWritesSeen;
        const bool stateCandidate = event.length >= 5 &&
                                    event.data[0] == 0xfe &&
                                    event.data[1] == 0xef &&
                                    event.data[2] == 0xfe &&
                                    event.data[3] == 0x10 &&
                                    event.data[4] == 0x80;
        Print& output = diagnosticOutput();
        output.printf("insta360_sync event=ce81_write handle=%u index=%u len=%u",
                      event.handle, session->diagnosticWritesSeen,
                      event.actualLength);
        if (stateCandidate) {
          output.print(" bytes=");
          for (uint8_t i = 0; i < event.length; ++i) {
            output.printf("%02X", event.data[i]);
            if (i + 1 != event.length) output.print(' ');
          }
        }
        output.println();
      }
      insta360::CaptureStatus status;
      if (!insta360::decodeCaptureStatus(event.data, event.length, status)) {
        continue;
      }
      if (!session->syncConfirmed) {
        session->syncConfirmed = true;
        session->syncDeadlineMs = 0;
        diagnosticOutput().printf(
            "insta360_sync event=state handle=%u mode=%s phase=%s elapsed_ms=%lu\n",
            event.handle, captureModeName(status.mode),
            capturePhaseName(status.phase),
            static_cast<unsigned long>(millis() - session->connectedAtMs));
      }
      session->state.power = insta360::State::Power::On;
      session->state.powerCommandPending = false;
      session->state.powerCommandFailed = false;
      if (status.mode == insta360::CaptureMode::Video) {
        session->state.photo = insta360::State::Photo::Unknown;
        switch (status.phase) {
          case insta360::CapturePhase::Idle:
            session->state.recording = insta360::State::Recording::Stopped;
            session->state.recordingConfirmed = true;
            if (session->triggerTarget == Session::TriggerTarget::Stop) {
              session->state.triggerPending = false;
              session->triggerTarget = Session::TriggerTarget::None;
            }
            break;
          case insta360::CapturePhase::Starting:
            session->state.recording = insta360::State::Recording::Starting;
            session->state.recordingConfirmed = false;
            break;
          case insta360::CapturePhase::Active:
            session->state.recording = insta360::State::Recording::Recording;
            session->state.recordingConfirmed = true;
            if (session->triggerTarget == Session::TriggerTarget::Start) {
              session->state.triggerPending = false;
              session->triggerTarget = Session::TriggerTarget::None;
            }
            break;
          case insta360::CapturePhase::Stopping:
            session->state.recording = insta360::State::Recording::Stopping;
            session->state.recordingConfirmed = false;
            break;
          default:
            break;
        }
      } else {
        session->state.recording = insta360::State::Recording::Unknown;
        session->state.recordingConfirmed = false;
        switch (status.phase) {
          case insta360::CapturePhase::Idle:
            session->state.photo = insta360::State::Photo::Idle;
            break;
          case insta360::CapturePhase::Starting:
            session->state.photo = insta360::State::Photo::Starting;
            break;
          case insta360::CapturePhase::Active:
            session->state.photo = insta360::State::Photo::Capturing;
            break;
          case insta360::CapturePhase::Saving:
            session->state.photo = insta360::State::Photo::Saving;
            break;
          default:
            break;
        }
      }
      continue;
    }

    if (event.type == Runtime::EventType::Subscription) {
      Session* session = sessionForHandle(event.handle);
      if (session == nullptr) continue;
      session->subscriptionEnabled = (event.subscriptionValue & 0x0001) != 0;
      diagnosticOutput().printf(
          "insta360_sync event=ce82_subscription handle=%u value=%u elapsed_ms=%lu\n",
          event.handle, event.subscriptionValue,
          static_cast<unsigned long>(millis() - session->connectedAtMs));
      continue;
    }

    Session* session = sessionForAddress(event.address);
    if (event.type == Runtime::EventType::Connected) {
      if (session == nullptr) {
        session = firstPoweringOn();
        if (session != nullptr) {
          diagnosticOutput().printf(
              "insta360_wake event=peer_routed handle=%u reason=wake_owner\n",
              event.handle);
        }
      }
      if (session == nullptr) session = firstAwaiting();
      if (session == nullptr) continue;
      runtime_->cancelWake();
      session->connHandle = event.handle;
      session->paired = true;
      session->pairingChanged = true;
      session->addressType = event.addressType;
      std::strncpy(session->address, event.address,
                   sizeof(session->address) - 1);
      if (event.name[0] != '\0') {
        std::strncpy(session->state.deviceName, event.name,
                     sizeof(session->state.deviceName) - 1);
      }
      session->state.goUltraExperimental =
          insta360::isGoUltra(session->state.deviceName);
      session->state.link = insta360::State::Link::Connected;
      session->state.power = insta360::State::Power::On;
      insta360::assumeVideoIdle(session->state);
      session->state.powerCommandPending = false;
      session->state.powerCommandFailed = false;
      session->connectedAtMs = millis();
      session->syncDeadlineMs = session->connectedAtMs + kInitialSyncTimeoutMs;
      session->diagnosticWritesRemaining = kInitialDiagnosticWriteLimit;
      session->diagnosticWritesSeen = 0;
      session->syncConfirmed = false;
      session->subscriptionEnabled = false;
      diagnosticOutput().printf(
          "insta360_sync event=connected handle=%u timeout_ms=%lu\n",
          event.handle, static_cast<unsigned long>(kInitialSyncTimeoutMs));
    } else {
      session = sessionForHandle(event.handle);
      if (session == nullptr) continue;
      session->connHandle = kNoConnection;
      session->state.link = insta360::State::Link::Scanning;
      session->state.recording = insta360::State::Recording::Unknown;
      session->state.recordingConfirmed = false;
      session->state.triggerPending = false;
      session->triggerTarget = Session::TriggerTarget::None;
      session->syncDeadlineMs = 0;
      session->diagnosticWritesRemaining = 0;
      session->syncConfirmed = false;
      session->subscriptionEnabled = false;
      if (session->state.power == insta360::State::Power::PoweringOff) {
        session->state.power = insta360::State::Power::Off;
        session->state.link = insta360::State::Link::Disconnected;
        session->state.powerCommandPending = false;
        session->state.powerCommandFailed = false;
      }
    }
  }

  const uint32_t now = millis();
  for (Session* session : sessions_) {
    if (session == nullptr) continue;
    if (!session->syncConfirmed &&
        deadlineReached(now, session->syncDeadlineMs)) {
      diagnosticOutput().printf(
          "insta360_sync event=timeout handle=%u subscription=%u ce81_writes=%u\n",
          session->connHandle, session->subscriptionEnabled ? 1 : 0,
          session->diagnosticWritesSeen);
      session->syncDeadlineMs = 0;
    }
    if (session->triggerRequested) {
      session->triggerRequested = false;
      const bool ok = runtime_->notifyCommand(
          session->connHandle, insta360::kShutterCommand,
          sizeof(insta360::kShutterCommand));
      session->state.lastTriggerFailed = !ok;
      if (ok) {
        ++session->state.triggerCount;
        session->commandDeadlineMs = now + kCommandTimeoutMs;
      } else {
        session->state.triggerPending = false;
        session->triggerTarget = Session::TriggerTarget::None;
      }
    }
    if (session->powerOffRequested) {
      session->powerOffRequested = false;
      const bool ok = runtime_->notifyCommand(
          session->connHandle, insta360::kPowerOffCommand,
          sizeof(insta360::kPowerOffCommand));
      if (ok) {
        session->state.power = insta360::State::Power::PoweringOff;
        session->commandDeadlineMs = now + kCommandTimeoutMs;
      } else {
        session->state.powerCommandPending = false;
        session->state.powerCommandFailed = true;
      }
    }
    if (session->powerOnRequested) {
      session->powerOnRequested = false;
      const bool ok = runtime_->startWake(session->state.deviceName);
      if (ok) {
        session->state.power = insta360::State::Power::PoweringOn;
        session->commandDeadlineMs = now + kWakeTimeoutMs;
      } else {
        diagnosticOutput().println(
            "insta360_wake event=rejected reason=invalid_camera_serial");
        session->state.power = insta360::State::Power::Off;
        session->state.powerCommandPending = false;
        session->state.powerCommandFailed = true;
      }
    }
    if ((session->state.triggerPending ||
         session->state.powerCommandPending) &&
        deadlineReached(now, session->commandDeadlineMs)) {
      if (session->state.triggerPending) {
        session->state.triggerPending = false;
        session->state.lastTriggerFailed = true;
        session->triggerTarget = Session::TriggerTarget::None;
      }
      if (session->state.powerCommandPending) {
        session->state.powerCommandPending = false;
        session->state.powerCommandFailed = true;
        if (session->state.power == insta360::State::Power::PoweringOn) {
          session->state.power = insta360::State::Power::Off;
        } else if (session->state.power ==
                   insta360::State::Power::PoweringOff) {
          session->state.power = insta360::State::Power::On;
        }
      }
      session->commandDeadlineMs = 0;
    }
  }
  runtime_->maintainWake();
  runtime_->consumeWakeTimeout();
  updateAdvertising();
  bool any = false;
  for (Session* session : sessions_) any |= session != nullptr;
  if (!any && runtime_->finishShutdown()) {
    delete runtime_;
    runtime_ = nullptr;
  }
}

void Insta360Driver::updateAdvertising() {
  if (runtime_ == nullptr) return;
  for (Session* session : sessions_) {
    if (session != nullptr &&
        session->state.power == insta360::State::Power::PoweringOn) {
      runtime_->maintainWake();
      return;
    }
  }
  bool needed = false;
  for (Session* session : sessions_) {
    needed |= session != nullptr && session->connHandle == kNoConnection &&
              session->state.power != insta360::State::Power::Off;
  }
  if (needed) {
    runtime_->advertiseNormal();
  } else {
    runtime_->stopAdvertising();
  }
}

CommandStatus Insta360Driver::dispatch(const DeviceCommand& command) {
  Session* session = sessionFor(command.instanceId);
  if (session == nullptr) return CommandStatus::Unavailable;

  if (command.type == CommandType::RecordStart ||
      command.type == CommandType::RecordStop) {
    if (session->connHandle == kNoConnection || session->state.triggerPending ||
        session->state.powerCommandPending) {
      return CommandStatus::Unavailable;
    }
    Session::TriggerTarget target = Session::TriggerTarget::None;
    if (command.type == CommandType::RecordStart) {
      if (insta360::canStopRecording(session->state)) {
        return CommandStatus::Succeeded;
      }
      if (!insta360::canStartRecording(session->state)) {
        return CommandStatus::Unavailable;
      }
      target = Session::TriggerTarget::Start;
    } else {
      if (session->state.recordingConfirmed &&
          insta360::canStartRecording(session->state)) {
        return CommandStatus::Succeeded;
      }
      if (!insta360::canStopRecording(session->state)) {
        return CommandStatus::Unavailable;
      }
      target = Session::TriggerTarget::Stop;
    }
    session->state.triggerPending = true;
    session->state.lastTriggerFailed = false;
    session->triggerRequested = true;
    session->triggerTarget = target;
    return CommandStatus::Succeeded;
  }

  if (command.type == CommandType::CameraPowerOff) {
    const bool photoBusy = session->state.photo != insta360::State::Photo::Unknown &&
                           session->state.photo != insta360::State::Photo::Idle;
    if (session->connHandle == kNoConnection ||
        session->state.powerCommandPending || session->state.triggerPending ||
        photoBusy ||
        (session->state.recordingConfirmed &&
         session->state.recording == insta360::State::Recording::Recording)) {
      return CommandStatus::Unavailable;
    }
    session->state.powerCommandPending = true;
    session->state.powerCommandFailed = false;
    session->powerOffRequested = true;
    return CommandStatus::Succeeded;
  }

  if (command.type == CommandType::CameraPowerOn) {
    if (session->connHandle != kNoConnection ||
        session->state.powerCommandPending) {
      return CommandStatus::Unavailable;
    }
    session->state.powerCommandPending = true;
    session->state.powerCommandFailed = false;
    session->powerOnRequested = true;
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

DeviceRuntimeState Insta360Driver::runtimeState(InstanceId id) const {
  DeviceRuntimeState runtime;
  const Session* session = sessionFor(id);
  if (session == nullptr) return runtime;
  if (session->state.link == insta360::State::Link::Connected) {
    runtime.link = LinkState::Connected;
  } else if (session->state.link == insta360::State::Link::Scanning) {
    runtime.link = LinkState::Scanning;
  } else {
    runtime.link = LinkState::Disconnected;
  }
  runtime.protocolReady = session->connHandle != kNoConnection;
  runtime.commandPending = session->state.triggerPending ||
                           session->state.powerCommandPending;
  runtime.commandFailed = session->state.lastTriggerFailed ||
                          session->state.powerCommandFailed;
  runtime.recordingConfirmed = session->state.recordingConfirmed;
  runtime.recording = session->state.recording ==
                      insta360::State::Recording::Recording;
  if (session->state.recordingConfirmed) {
    runtime.quality = StateQuality::Confirmed;
  } else if (session->connHandle != kNoConnection &&
             session->state.recording != insta360::State::Recording::Unknown) {
    runtime.quality = StateQuality::Optimistic;
  }
  return runtime;
}

const void* Insta360Driver::specializedState(InstanceId id) const {
  const Session* session = sessionFor(id);
  return session != nullptr ? &session->state : nullptr;
}

void Insta360Driver::forgetPairing(const DeviceRecord&) {}
void Insta360Driver::cancelOnboarding(const DeviceRecord&) {}

bool Insta360Driver::consumePairingUpdate(InstanceId id, DeviceRecord& record) {
  Session* session = sessionFor(id);
  if (session == nullptr || !session->pairingChanged) return false;
  session->pairingChanged = false;
  record.paired = session->paired;
  record.bleAddressType = session->addressType;
  std::strncpy(record.bleAddress, session->address,
               sizeof(record.bleAddress) - 1);
  std::strncpy(record.bleName, session->state.deviceName,
               sizeof(record.bleName) - 1);
  return true;
}

}  // namespace studio
