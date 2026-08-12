#include "core/ble/peripheral_dispatcher.h"

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <cstring>

#include "core/ble/ble_runtime.h"
#include "driver_config.h"

namespace studio::ble {
namespace {
PeripheralListener* gListeners[3] = {};
struct Owner { uint16_t handle = 0xffff; PeripheralListener* listener = nullptr; };
Owner gOwners[CONFIG_BT_NIMBLE_MAX_CONNECTIONS] = {};
constexpr uint32_t kAdvertisingSliceMs = 12000;
constexpr uint32_t kDirectedAdvertisingMs = 3000;
struct Advertiser {
  const void* owner = nullptr;
  PeripheralAdvertisement advertisement;
  bool wanted = false;
  bool directedActive = false;
  uint32_t lastFailureLogMs = 0;
};
Advertiser gAdvertisers[CONFIG_MAX_ACTIVE_INSTANCES] = {};
int8_t gActiveAdvertiser = -1;
uint32_t gAdvertisingStartedMs = 0;

Print& diagnosticOutput() {
#if ARDUINO_USB_CDC_ON_BOOT
  return Serial0;
#else
  return Serial;
#endif
}

int8_t advertiserFor(const void* owner) {
  for (uint8_t i = 0; i < CONFIG_MAX_ACTIVE_INSTANCES; ++i) {
    if (gAdvertisers[i].owner == owner) return static_cast<int8_t>(i);
  }
  return -1;
}

int8_t nextWantedAdvertiser(int8_t after) {
  for (uint8_t offset = 1; offset <= CONFIG_MAX_ACTIVE_INSTANCES; ++offset) {
    const uint8_t index = static_cast<uint8_t>(
        (after + offset) % CONFIG_MAX_ACTIVE_INSTANCES);
    if (gAdvertisers[index].owner != nullptr && gAdvertisers[index].wanted) {
      return static_cast<int8_t>(index);
    }
  }
  return -1;
}

bool activateAdvertiser(int8_t index, uint32_t nowMs,
                        bool allowDirected = true) {
  if (index < 0 || index >= CONFIG_MAX_ACTIVE_INSTANCES) return false;
  Advertiser& slot = gAdvertisers[index];
  const bool hasGattIdentity = slot.advertisement.name != nullptr &&
                               slot.advertisement.serviceUuid != nullptr;
  const bool hasManufacturerData =
      slot.advertisement.manufacturerData != nullptr &&
      slot.advertisement.manufacturerDataLength != 0;
  const bool hasRawAdvertisement =
      slot.advertisement.rawAdvertisementData != nullptr &&
      slot.advertisement.rawAdvertisementDataLength != 0;
  if (slot.owner == nullptr || !slot.wanted ||
      (!hasGattIdentity && !hasManufacturerData && !hasRawAdvertisement)) {
    return false;
  }
  NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
  if (advertising == nullptr) return false;
  if (advertising->isAdvertising()) advertising->stop();
  advertising->reset();
  if (hasRawAdvertisement) {
    NimBLEAdvertisementData primary;
    if (!primary.addData(slot.advertisement.rawAdvertisementData,
                         slot.advertisement.rawAdvertisementDataLength) ||
        !advertising->setAdvertisementData(primary)) {
      return false;
    }
    const bool hasRawScanResponse =
        slot.advertisement.rawScanResponseData != nullptr &&
        slot.advertisement.rawScanResponseDataLength != 0;
    advertising->enableScanResponse(hasRawScanResponse);
    if (hasRawScanResponse) {
      NimBLEAdvertisementData scanResponse;
      if (!scanResponse.addData(slot.advertisement.rawScanResponseData,
                                slot.advertisement.rawScanResponseDataLength) ||
          !advertising->setScanResponseData(scanResponse)) {
        return false;
      }
    }
  } else {
    advertising->enableScanResponse(hasGattIdentity);
    if (slot.advertisement.name != nullptr) {
      advertising->setName(slot.advertisement.name);
    }
    if (slot.advertisement.appearance != 0) {
      advertising->setAppearance(slot.advertisement.appearance);
    }
    if (slot.advertisement.serviceUuid != nullptr) {
      advertising->addServiceUUID(slot.advertisement.serviceUuid);
    }
    if (hasManufacturerData &&
        !advertising->setManufacturerData(
            slot.advertisement.manufacturerData,
            slot.advertisement.manufacturerDataLength)) {
      return false;
    }
  }
  NimBLEAddress directedPeer;
  const NimBLEAddress* directedPeerPointer = nullptr;
  if (allowDirected && slot.advertisement.peerAddress != nullptr &&
      slot.advertisement.peerAddress[0] != '\0') {
    directedPeer = NimBLEAddress(std::string(slot.advertisement.peerAddress),
                                 slot.advertisement.peerAddressType);
    advertising->setConnectableMode(BLE_GAP_CONN_MODE_DIR);
    directedPeerPointer = &directedPeer;
  }
  if (!advertising->start(0, directedPeerPointer)) {
    if (slot.lastFailureLogMs == 0 ||
        static_cast<int32_t>(nowMs - slot.lastFailureLogMs) >= 2000) {
      diagnosticOutput().printf(
          "ble_peripheral_advertising_failed owner=%s adv=%u scan=%u connect=%u\n",
          slot.advertisement.diagnosticTag != nullptr
              ? slot.advertisement.diagnosticTag
              : "unknown",
          ble_gap_adv_active() ? 1u : 0u, ble_gap_disc_active() ? 1u : 0u,
          ble_gap_conn_active() ? 1u : 0u);
      slot.lastFailureLogMs = nowMs;
    }
    return false;
  }
  gActiveAdvertiser = index;
  gAdvertisingStartedMs = nowMs;
  slot.directedActive = directedPeerPointer != nullptr;
  slot.lastFailureLogMs = 0;
  diagnosticOutput().printf(
      "ble_peripheral_advertising owner=%s directed=%u\n",
      slot.advertisement.diagnosticTag != nullptr
          ? slot.advertisement.diagnosticTag
          : "unknown",
      directedPeerPointer != nullptr ? 1u : 0u);
  return true;
}

PeripheralListener* choose(NimBLEConnInfo& info) {
  const std::string address = info.getAddress().toString();
  const std::string identityAddress = info.getIdAddress().toString();
  for (PeripheralListener* listener : gListeners)
    if (listener != nullptr &&
        (listener->acceptsPeripheralPeer(address.c_str()) ||
         (identityAddress != address &&
          listener->acceptsPeripheralPeer(identityAddress.c_str())))) {
      return listener;
    }
  for (PeripheralListener* listener : gListeners)
    if (listener != nullptr && listener->defaultPeripheralPeer()) return listener;
  return nullptr;
}

PeripheralListener* owner(uint16_t handle) {
  for (const Owner& item : gOwners) if (item.handle == handle) return item.listener;
  return nullptr;
}

void saveOwner(uint16_t handle, PeripheralListener* listener) {
  for (Owner& item : gOwners) if (item.handle == handle || item.listener == nullptr) { item.handle = handle; item.listener = listener; return; }
}

void clearOwner(uint16_t handle) {
  for (Owner& item : gOwners) if (item.handle == handle) item = {};
}

class Callbacks : public NimBLEServerCallbacks {
 public:
  void onConnect(NimBLEServer*, NimBLEConnInfo& info) override {
    PeripheralListener* listener = choose(info);
    if (listener != nullptr) { saveOwner(info.getConnHandle(), listener); listener->onPeripheralConnected(info); }
  }
  void onDisconnect(NimBLEServer*, NimBLEConnInfo& info, int) override {
    PeripheralListener* listener = owner(info.getConnHandle());
    if (listener != nullptr) listener->onPeripheralDisconnected(info);
    clearOwner(info.getConnHandle());
  }
  void onAuthenticationComplete(NimBLEConnInfo& info) override {
    PeripheralListener* listener = owner(info.getConnHandle());
    if (listener != nullptr) listener->onPeripheralAuthentication(info);
  }
} gCallbacks;
}  // namespace

bool registerPeripheralListener(NimBLEServer* server, PeripheralListener* listener) {
  if (server == nullptr || listener == nullptr) return false;
  for (PeripheralListener*& slot : gListeners) {
    if (slot == listener) return true;
    if (slot == nullptr) { slot = listener; server->setCallbacks(&gCallbacks, false); return true; }
  }
  return false;
}

void unregisterPeripheralListener(PeripheralListener* listener) {
  for (PeripheralListener*& slot : gListeners) if (slot == listener) slot = nullptr;
  for (Owner& item : gOwners) if (item.listener == listener) item = {};
}

bool preparePeripheralGattMutation(NimBLEServer* server) {
  if (server == nullptr || server->getConnectedCount() != 0 ||
      !NimBLEDevice::getConnectedClients().empty()) {
    return false;
  }
  NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
  if (advertising != nullptr && advertising->isAdvertising()) {
    advertising->stop();
  }
  bleCentral().pauseScanForGattMutation();
  return !ble_gap_adv_active() && !ble_gap_disc_active() &&
         !ble_gap_conn_active() && server->getConnectedCount() == 0 &&
         NimBLEDevice::getConnectedClients().empty();
}

bool requestPeripheralAdvertising(
    const void* owner, const PeripheralAdvertisement& advertisement,
    uint32_t nowMs) {
  if (owner == nullptr) return false;
  int8_t index = advertiserFor(owner);
  bool newlyRegistered = false;
  if (index < 0) {
    for (uint8_t i = 0; i < CONFIG_MAX_ACTIVE_INSTANCES; ++i) {
      if (gAdvertisers[i].owner == nullptr) {
        index = static_cast<int8_t>(i);
        gAdvertisers[i].owner = owner;
        newlyRegistered = true;
        break;
      }
    }
  }
  if (index < 0) return false;
  gAdvertisers[index].advertisement = advertisement;
  gAdvertisers[index].wanted = true;

  NimBLEAdvertising* active = NimBLEDevice::getAdvertising();
  if (gActiveAdvertiser < 0 || active == nullptr ||
      !active->isAdvertising()) {
    return activateAdvertiser(index, nowMs);
  }
  if (newlyRegistered && gActiveAdvertiser != index &&
      gAdvertisers[index].advertisement.priority >
          gAdvertisers[gActiveAdvertiser].advertisement.priority) {
    return activateAdvertiser(index, nowMs);
  }
  if (gActiveAdvertiser == index &&
      gAdvertisers[index].directedActive &&
      static_cast<int32_t>(nowMs - gAdvertisingStartedMs) >=
          static_cast<int32_t>(kDirectedAdvertisingMs)) {
    return activateAdvertiser(index, nowMs, false);
  }
  if (gActiveAdvertiser == index &&
      static_cast<int32_t>(nowMs - gAdvertisingStartedMs) >=
          static_cast<int32_t>(kAdvertisingSliceMs)) {
    const int8_t next = nextWantedAdvertiser(gActiveAdvertiser);
    if (next >= 0 && next != gActiveAdvertiser) {
      activateAdvertiser(next, nowMs);
    }
  }
  return gActiveAdvertiser == index;
}

void releasePeripheralAdvertising(const void* owner, uint32_t nowMs) {
  const int8_t index = advertiserFor(owner);
  if (index < 0) return;
  gAdvertisers[index].wanted = false;
  if (gActiveAdvertiser == index) {
    NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
    if (advertising != nullptr && advertising->isAdvertising()) {
      advertising->stop();
    }
    gActiveAdvertiser = -1;
    const int8_t next = nextWantedAdvertiser(index);
    if (next >= 0) activateAdvertiser(next, nowMs);
  }
  gAdvertisers[index] = {};
}
}  // namespace studio::ble
