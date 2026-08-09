#include "core/ble/ble_nimble_backend.h"

#include <NimBLEDevice.h>

#include <algorithm>
#include <cstring>
#include <new>
#include <string>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

namespace studio::ble {

namespace {

size_t slotIndex(LinkHandle link) {
  return link == kInvalidLinkHandle ? CONFIG_MAX_ACTIVE_LINKS : link - 1;
}

}  // namespace

struct BleNimbleBackend::Impl {
  struct ControlEvent {
    EventType type = EventType::ScanEnded;
    LinkHandle link = kInvalidLinkHandle;
    int reason = 0;
    bool succeeded = false;
    uint32_t generation = 0;
  };

  NimBLEClient* clientFor(LinkHandle link) const;
  uint32_t generationForClient(LinkHandle link, NimBLEClient* client) const;

  struct ScanCallbacks final : NimBLEScanCallbacks {
    explicit ScanCallbacks(Impl& owner) : owner(owner) {}

    void onResult(const NimBLEAdvertisedDevice* device) override {
      if (device == nullptr) {
        return;
      }
      Event event;
      event.type = EventType::Advertisement;
      const std::string address = device->getAddress().toString();
      std::strncpy(event.advertisement.address.value, address.c_str(),
                   sizeof(event.advertisement.address.value) - 1);
      event.advertisement.address.type = device->getAddressType();
      event.advertisement.rssi = device->getRSSI();
      const std::vector<uint8_t>& payload = device->getPayload();
      const size_t length =
          std::min(payload.size(), sizeof(event.advertisement.payload));
      if (length > 0) {
        std::memcpy(event.advertisement.payload, payload.data(), length);
      }
      event.advertisement.payloadLength = static_cast<uint8_t>(length);
      owner.enqueue(event);
    }

    void onScanEnd(const NimBLEScanResults&, int reason) override {
      Event event;
      event.type = EventType::ScanEnded;
      event.reason = reason;
      owner.enqueue(event);
    }

    Impl& owner;
  };

  struct ClientCallbacks final : NimBLEClientCallbacks {
    ClientCallbacks(Impl& owner, LinkHandle link) : owner(owner), link(link) {}

    void onConnect(NimBLEClient* client) override {
      emit(client, EventType::Connected, 0, true);
    }

    void onConnectFail(NimBLEClient* client, int reason) override {
      emit(client, EventType::ConnectFailed, reason, false);
    }

    void onDisconnect(NimBLEClient* client, int reason) override {
      emit(client, EventType::Disconnected, reason, false);
    }

    void onAuthenticationComplete(NimBLEConnInfo& info) override {
      NimBLEClient* client = owner.clientFor(link);
      // This callback does not receive its NimBLEClient. Match the physical
      // connection too, so a late authentication result from an evicted
      // client cannot be attributed to a replacement in the same slot.
      if (client != nullptr && client->isConnected() &&
          client->getConnHandle() == info.getConnHandle()) {
        emit(client, EventType::SecurityComplete, 0, info.isEncrypted());
      }
    }

    void emit(NimBLEClient* client, EventType type, int reason,
              bool succeeded) {
      // destroyLink detaches the client before requesting asynchronous
      // deletion. Ignore the old client's final callback if this logical slot
      // has already been released or reacquired with a replacement client.
      const uint32_t generation = owner.generationForClient(link, client);
      if (generation == 0) {
        return;
      }
      Event event;
      event.type = type;
      event.link = link;
      event.reason = reason;
      event.succeeded = succeeded;
      owner.enqueue(event, generation);
    }

    Impl& owner;
    LinkHandle link;
  };

  struct Slot {
    NimBLEClient* client = nullptr;
    ClientCallbacks* callbacks = nullptr;
    SecurityPolicy security = SecurityPolicy::None;
    Address pendingAddress;
    uint16_t connectTimeoutMs = 0;
    bool linkCreated = false;
    bool connectPending = false;
    uint32_t generation = 0;
  };

  Impl() : scanCallbacks(*this) {}

  ~Impl() {
    // NimBLE disconnect/delete is asynchronous. ClientCallbacks must outlive
    // every client and any final GAP disconnect event that still references
    // m_pClientCallbacks.
    for (Slot& slot : slots) {
      delete slot.callbacks;
      slot.callbacks = nullptr;
    }
    if (controlQueue != nullptr) vQueueDelete(controlQueue);
    if (advertisementQueue != nullptr) vQueueDelete(advertisementQueue);
  }

  bool enqueue(const Event& event, uint32_t generation = 0) {
    if (event.type == EventType::Advertisement) {
      if (advertisementQueue == nullptr ||
          xQueueSend(advertisementQueue, &event, 0) != pdTRUE) {
        ++droppedAdvertisements;
        return false;
      }
      return true;
    }
    ControlEvent control;
    control.type = event.type;
    control.link = event.link;
    control.reason = event.reason;
    control.succeeded = event.succeeded;
    control.generation = generation;
    if (controlQueue == nullptr ||
        xQueueSend(controlQueue, &control, 0) != pdTRUE) {
      ++droppedControl;
      return false;
    }
    return true;
  }

  void applySecurity(SecurityPolicy policy) {
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
    switch (policy) {
      case SecurityPolicy::None:
        NimBLEDevice::setSecurityAuth(false, false, false);
        break;
      case SecurityPolicy::BondNoMitm:
        NimBLEDevice::setSecurityAuth(true, false, true);
        break;
      case SecurityPolicy::BondSecure:
        NimBLEDevice::setSecurityAuth(true, true, true);
        break;
    }
  }

  bool startSecurity(LinkHandle link, SecurityPolicy policy) {
    const size_t index = slotIndex(link);
    if (index >= CONFIG_MAX_ACTIVE_LINKS || slots[index].client == nullptr ||
        !slots[index].client->isConnected()) {
      return false;
    }
    applySecurity(policy);
    if (slots[index].client->getConnInfo().isEncrypted()) {
      Event event;
      event.type = EventType::SecurityComplete;
      event.link = link;
      event.succeeded = true;
      return enqueue(event, slots[index].generation);
    }
    if (!slots[index].client->secureConnection(true)) {
      return false;
    }
    activeSecurity = link;
    return true;
  }

  void queueSecurity(LinkHandle link, SecurityPolicy policy) {
    for (size_t i = 0; i < CONFIG_MAX_ACTIVE_LINKS; ++i) {
      if (securityQueue[i] == link) {
        return;
      }
      if (securityQueue[i] == kInvalidLinkHandle) {
        securityQueue[i] = link;
        securityPolicies[i] = policy;
        return;
      }
    }
  }

  void advanceSecurity() {
    if (activeSecurity != kInvalidLinkHandle) {
      return;
    }
    for (size_t i = 0; i < CONFIG_MAX_ACTIVE_LINKS; ++i) {
      if (securityQueue[i] == kInvalidLinkHandle) {
        continue;
      }
      const LinkHandle link = securityQueue[i];
      const SecurityPolicy policy = securityPolicies[i];
      securityQueue[i] = kInvalidLinkHandle;
      if (startSecurity(link, policy)) {
        return;
      }
    }
  }

  bool provisionClient(size_t index) {
    if (!initialized || index >= CONFIG_MAX_ACTIVE_LINKS ||
        !slots[index].linkCreated) {
      return false;
    }
    if (slots[index].client != nullptr) {
      return true;
    }
    NimBLEClient* client = NimBLEDevice::createClient();
    if (client == nullptr) {
      return false;
    }
    ClientCallbacks* callbacks = slots[index].callbacks;
    if (callbacks == nullptr) {
      callbacks =
          new (std::nothrow)
              ClientCallbacks(*this, static_cast<LinkHandle>(index + 1));
      if (callbacks == nullptr) {
        NimBLEDevice::deleteClient(client);
        return false;
      }
      slots[index].callbacks = callbacks;
    }
    client->setClientCallbacks(callbacks, false);
    client->setConnectTimeout(slots[index].connectTimeoutMs);
    client->setConnectRetries(0);
    slots[index].client = client;
    return true;
  }

  void advancePendingClients() {
    for (size_t index = 0; index < CONFIG_MAX_ACTIVE_LINKS; ++index) {
      Slot& slot = slots[index];
      if (!slot.linkCreated || slot.client != nullptr ||
          !provisionClient(index) || !slot.connectPending) {
        continue;
      }
      slot.connectPending = false;
      NimBLEAddress peer(std::string(slot.pendingAddress.value),
                         slot.pendingAddress.type);
      if (!slot.client->connect(peer, true, true, true)) {
        Event event;
        event.type = EventType::ConnectFailed;
        event.link = static_cast<LinkHandle>(index + 1);
        event.reason = -1;
        enqueue(event, slot.generation);
      }
    }
  }

  void finishShutdownIfPossible() {
    if (!shutdownPending || !initialized ||
        NimBLEDevice::getCreatedClientCount() != 0) {
      return;
    }
    NimBLEDevice::deinit(false);
    initialized = false;
    shutdownPending = false;
    if (controlQueue != nullptr) {
      vQueueDelete(controlQueue);
      controlQueue = nullptr;
    }
    if (advertisementQueue != nullptr) {
      vQueueDelete(advertisementQueue);
      advertisementQueue = nullptr;
    }
  }

  Slot slots[CONFIG_MAX_ACTIVE_LINKS] = {};
  LinkHandle securityQueue[CONFIG_MAX_ACTIVE_LINKS] = {};
  SecurityPolicy securityPolicies[CONFIG_MAX_ACTIVE_LINKS] = {};
  ScanCallbacks scanCallbacks;
  QueueHandle_t controlQueue = nullptr;
  QueueHandle_t advertisementQueue = nullptr;
  LinkHandle activeSecurity = kInvalidLinkHandle;
  uint32_t droppedControl = 0;
  uint32_t droppedAdvertisements = 0;
  bool initialized = false;
  bool shutdownPending = false;
};

NimBLEClient* BleNimbleBackend::Impl::clientFor(LinkHandle link) const {
  const size_t index = slotIndex(link);
  return index < CONFIG_MAX_ACTIVE_LINKS ? slots[index].client : nullptr;
}

uint32_t BleNimbleBackend::Impl::generationForClient(
    LinkHandle link, NimBLEClient* client) const {
  const size_t index = slotIndex(link);
  return index < CONFIG_MAX_ACTIVE_LINKS && slots[index].client == client
             ? slots[index].generation
             : 0;
}

BleNimbleBackend::BleNimbleBackend() : impl_(nullptr) {}

BleNimbleBackend::~BleNimbleBackend() {
  shutdown();
  delete impl_;
}

void BleNimbleBackend::releaseImplIfIdle() {
  if (impl_ != nullptr && !impl_->initialized && !impl_->shutdownPending) {
    delete impl_;
    impl_ = nullptr;
  }
}

bool BleNimbleBackend::begin() {
  if (impl_ == nullptr) {
    impl_ = new (std::nothrow) Impl;
    if (impl_ == nullptr) return false;
  }
  if (impl_->initialized) {
    impl_->shutdownPending = false;
    return true;
  }
  if (impl_->controlQueue == nullptr) {
    impl_->controlQueue = xQueueCreate(CONFIG_BLE_EVENT_QUEUE_SIZE,
                                      sizeof(Impl::ControlEvent));
    if (impl_->controlQueue == nullptr) {
      releaseImplIfIdle();
      return false;
    }
  }
  if (impl_->advertisementQueue == nullptr) {
    impl_->advertisementQueue =
        xQueueCreate(CONFIG_BLE_ADV_QUEUE_SIZE, sizeof(Event));
    if (impl_->advertisementQueue == nullptr) {
      vQueueDelete(impl_->controlQueue);
      impl_->controlQueue = nullptr;
      releaseImplIfIdle();
      return false;
    }
  }
  if (!NimBLEDevice::init("Ble(e)p")) {
    vQueueDelete(impl_->controlQueue);
    vQueueDelete(impl_->advertisementQueue);
    impl_->controlQueue = nullptr;
    impl_->advertisementQueue = nullptr;
    releaseImplIfIdle();
    return false;
  }
  // Keep transmit power configurable for installations that need a different
  // range/current trade-off than the firmware profile default.
  NimBLEDevice::setPower(CONFIG_BLE_TX_POWER_DBM, NimBLETxPowerType::All);
  NimBLEDevice::setMTU(247);
  NimBLEScan* scan = NimBLEDevice::getScan();
  scan->setScanCallbacks(&impl_->scanCallbacks, true);
  scan->setActiveScan(true);
  scan->setInterval(CONFIG_BLE_SCAN_INTERVAL_UNITS);
  scan->setWindow(CONFIG_BLE_SCAN_WINDOW_UNITS);
  scan->setMaxResults(0);
  impl_->initialized = true;
  impl_->shutdownPending = false;
  return true;
}

void BleNimbleBackend::shutdown() {
  if (impl_ == nullptr) {
    return;
  }
  if (!impl_->initialized) {
    releaseImplIfIdle();
    return;
  }
  stopScan();
  for (size_t i = 0; i < CONFIG_MAX_ACTIVE_LINKS; ++i) {
    destroyLink(static_cast<LinkHandle>(i + 1));
  }
  impl_->activeSecurity = kInvalidLinkHandle;
  for (LinkHandle& link : impl_->securityQueue) {
    link = kInvalidLinkHandle;
  }
  // Connected and connecting NimBLE clients delete themselves only after a
  // final GAP callback. Do not stop the host underneath that callback. pump()
  // completes deinit once the global client list is empty.
  impl_->shutdownPending = true;
  impl_->finishShutdownIfPossible();
  releaseImplIfIdle();
}

void BleNimbleBackend::pump() {
  if (impl_ == nullptr) {
    return;
  }
  impl_->finishShutdownIfPossible();
  releaseImplIfIdle();
  if (impl_ == nullptr) {
    return;
  }
  if (!impl_->initialized) {
    return;
  }
  impl_->advancePendingClients();
  impl_->advanceSecurity();
}

bool BleNimbleBackend::startScan() {
  if (!impl_->initialized) {
    return false;
  }
  NimBLEScan* scan = NimBLEDevice::getScan();
  if (scan->isScanning()) {
    return true;
  }
  scan->clearResults();
  return scan->start(0, false, true);
}

void BleNimbleBackend::stopScan() {
  if (impl_->initialized && NimBLEDevice::getScan()->isScanning()) {
    NimBLEDevice::getScan()->stop();
  }
}

bool BleNimbleBackend::scanRunning() const {
  return impl_->initialized && NimBLEDevice::getScan()->isScanning();
}

bool BleNimbleBackend::createLink(LinkHandle link,
                                  uint16_t connectTimeoutMs) {
  const size_t index = slotIndex(link);
  if (!impl_->initialized || index >= CONFIG_MAX_ACTIVE_LINKS ||
      impl_->slots[index].linkCreated) {
    return false;
  }
  impl_->slots[index].linkCreated = true;
  impl_->slots[index].connectTimeoutMs = connectTimeoutMs;
  if (++impl_->slots[index].generation == 0) {
    ++impl_->slots[index].generation;
  }
  // Failure here can simply mean the previous client for this slot is still
  // completing asynchronous deletion. pump() provisions the replacement as
  // soon as NimBLE releases capacity.
  impl_->provisionClient(index);
  return true;
}

void BleNimbleBackend::destroyLink(LinkHandle link) {
  const size_t index = slotIndex(link);
  if (index >= CONFIG_MAX_ACTIVE_LINKS ||
      !impl_->slots[index].linkCreated) {
    return;
  }
  NimBLEClient* client = impl_->slots[index].client;
  impl_->slots[index].client = nullptr;
  impl_->slots[index].linkCreated = false;
  impl_->slots[index].connectPending = false;
  impl_->slots[index].pendingAddress = {};
  // deleteClient schedules self-deletion after an async disconnect when
  // necessary. Keep our callbacks allocated in the slot until Impl teardown.
  if (client != nullptr) {
    NimBLEDevice::deleteClient(client);
  }
  impl_->slots[index].security = SecurityPolicy::None;
  if (impl_->activeSecurity == link) {
    impl_->activeSecurity = kInvalidLinkHandle;
  }
  for (LinkHandle& queued : impl_->securityQueue) {
    if (queued == link) {
      queued = kInvalidLinkHandle;
    }
  }
}

bool BleNimbleBackend::connect(LinkHandle link, const Address& address,
                               SecurityPolicy security) {
  const size_t index = slotIndex(link);
  if (index >= CONFIG_MAX_ACTIVE_LINKS ||
      !impl_->slots[index].linkCreated) {
    return false;
  }
  impl_->slots[index].security = security;
  if (impl_->slots[index].client == nullptr) {
    impl_->slots[index].pendingAddress = address;
    impl_->slots[index].connectPending = true;
    return true;
  }
  NimBLEAddress peer(std::string(address.value), address.type);
  return impl_->slots[index].client->connect(peer, true, true, true);
}

void BleNimbleBackend::disconnect(LinkHandle link) {
  const size_t index = slotIndex(link);
  if (index >= CONFIG_MAX_ACTIVE_LINKS ||
      impl_->slots[index].client == nullptr) {
    return;
  }
  if (impl_->slots[index].client->isConnected()) {
    impl_->slots[index].client->disconnect();
  } else {
    impl_->slots[index].client->cancelConnect();
  }
}

bool BleNimbleBackend::secure(LinkHandle link, SecurityPolicy security) {
  if (security == SecurityPolicy::None) {
    Event event;
    event.type = EventType::SecurityComplete;
    event.link = link;
    event.succeeded = true;
    const size_t index = slotIndex(link);
    return index < CONFIG_MAX_ACTIVE_LINKS &&
           impl_->enqueue(event, impl_->slots[index].generation);
  }
  if (impl_->activeSecurity != kInvalidLinkHandle) {
    impl_->queueSecurity(link, security);
    return true;
  }
  return impl_->startSecurity(link, security);
}

bool BleNimbleBackend::updateConnectionParameters(
    LinkHandle link, const ConnectionParameters& parameters) {
  const size_t index = slotIndex(link);
  if (!parameters.configured() || index >= CONFIG_MAX_ACTIVE_LINKS ||
      impl_->slots[index].client == nullptr ||
      !impl_->slots[index].client->isConnected()) {
    return false;
  }
  return impl_->slots[index].client->updateConnParams(
      parameters.minInterval, parameters.maxInterval, parameters.latency,
      parameters.supervisionTimeout);
}

bool BleNimbleBackend::deleteBond(const Address& address) {
  return impl_->initialized &&
         NimBLEDevice::deleteBond(
             NimBLEAddress(std::string(address.value), address.type));
}

void* BleNimbleBackend::nativeClient(LinkHandle link) {
  const size_t index = slotIndex(link);
  return index < CONFIG_MAX_ACTIVE_LINKS ? impl_->slots[index].client
                                         : nullptr;
}

bool BleNimbleBackend::popEvent(Event& event) {
  Impl::ControlEvent control;
  bool haveControl = false;
  while (impl_->controlQueue != nullptr &&
         xQueueReceive(impl_->controlQueue, &control, 0) == pdTRUE) {
    const size_t index = slotIndex(control.link);
    if (control.generation != 0 &&
        (index >= CONFIG_MAX_ACTIVE_LINKS ||
         !impl_->slots[index].linkCreated ||
         impl_->slots[index].generation != control.generation)) {
      continue;
    }
    haveControl = true;
    break;
  }
  if (haveControl) {
    event = {};
    event.type = control.type;
    event.link = control.link;
    event.reason = control.reason;
    event.succeeded = control.succeeded;
  } else if (impl_->advertisementQueue == nullptr ||
             xQueueReceive(impl_->advertisementQueue, &event, 0) != pdTRUE) {
    return false;
  }
  if (event.type == EventType::SecurityComplete &&
      event.link == impl_->activeSecurity) {
    impl_->activeSecurity = kInvalidLinkHandle;
  }
  return true;
}

uint32_t BleNimbleBackend::droppedEvents() const {
  return impl_ != nullptr
             ? impl_->droppedControl + impl_->droppedAdvertisements
             : 0;
}

}  // namespace studio::ble
