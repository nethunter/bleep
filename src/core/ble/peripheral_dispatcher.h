#pragma once

#include <cstdint>

class NimBLEServer;
class NimBLEConnInfo;

namespace studio::ble {

class PeripheralListener {
 public:
  virtual ~PeripheralListener() = default;
  virtual bool acceptsPeripheralPeer(const char* address) const = 0;
  virtual bool defaultPeripheralPeer() const { return false; }
  // Called on NimBLE's host task; implementations may only enqueue data.
  virtual void onPeripheralConnected(NimBLEConnInfo&) = 0;
  virtual void onPeripheralDisconnected(NimBLEConnInfo&) = 0;
  virtual void onPeripheralAuthentication(NimBLEConnInfo&) = 0;
};

bool registerPeripheralListener(NimBLEServer* server, PeripheralListener* listener);
void unregisterPeripheralListener(PeripheralListener* listener);
// NimBLE only permits its GATT table to change with no active GAP procedure or
// peripheral connection. Returns false when an existing peer makes the change
// unsafe; callers must fail activation instead of invoking NimBLEServer::start.
bool preparePeripheralGattMutation(NimBLEServer* server);

struct PeripheralAdvertisement {
  PeripheralAdvertisement() = default;
  PeripheralAdvertisement(const char* tag, const char* advertisedName,
                          const char* uuid, uint16_t advertisedAppearance,
                          uint8_t reconnectPriority = 0,
                          const char* directedPeerAddress = nullptr,
                          uint8_t directedPeerAddressType = 0)
      : diagnosticTag(tag),
        name(advertisedName),
        serviceUuid(uuid),
        appearance(advertisedAppearance),
        priority(reconnectPriority),
        peerAddress(directedPeerAddress),
        peerAddressType(directedPeerAddressType) {}
  const char* diagnosticTag = nullptr;
  const char* name = nullptr;
  const char* serviceUuid = nullptr;
  uint16_t appearance = 0;
  uint8_t priority = 0;
  const char* peerAddress = nullptr;
  uint8_t peerAddressType = 0;
};

// The ESP32-C3 profile uses one legacy advertising instance. Peripheral
// drivers request it through this coordinator so multiple disconnected peers
// receive bounded reconnect windows instead of overwriting each other's data.
bool requestPeripheralAdvertising(
    const void* owner, const PeripheralAdvertisement& advertisement,
    uint32_t nowMs);
void releasePeripheralAdvertising(const void* owner, uint32_t nowMs);

}  // namespace studio::ble
