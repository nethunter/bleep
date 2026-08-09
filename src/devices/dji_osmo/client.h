#pragma once

#include <cstddef>
#include <cstdint>

#include "core/ble/ble_central.h"
#include "devices/dji_osmo/protocol.h"
#include "devices/dji_osmo/state.h"

class NimBLEClient;
class NimBLERemoteCharacteristic;

namespace dji_osmo {

class Client : public studio::ble::BleCentralDelegate {
 public:
  void activate(const char* address, uint8_t addressType, const char* name,
                bool paired, uint32_t deviceId, uint32_t cameraNumber);
  void deactivate();
  void loop();
  void startScan();
  void forgetDevice();
  void forgetBond(const char* address, uint8_t addressType);
  bool setRecording(bool start);
  bool protocolReady() const;
  const State& state() const { return state_; }
  bool consumePairingUpdate(char* address, size_t addressCapacity,
                            uint8_t& addressType, char* name,
                            size_t nameCapacity, bool& paired);

  void onBleAdvertisement(studio::ble::LinkHandle,
                          const studio::ble::Advertisement&) override;
  void onBleEvent(studio::ble::LinkHandle,
                  const studio::ble::Event&) override;
  void onNotification(const uint8_t* data, size_t length);
  bool ownsNotifyCharacteristic(const void* characteristic) const {
    return notify_ == characteristic;
  }

 private:
  struct Chunk { uint8_t length = 0; uint8_t data[96] = {}; };
  void begin();
  void beginScan();
  void beginConnect();
  bool completeGattSetup();
  void drainNotifications();
  void consumeBytes(const uint8_t* data, size_t length);
  void handleFrame(const uint8_t* data, size_t length);
  bool send(const Packet& packet);
  void markReady();
  void handleDisconnect();

  NimBLEClient* client_ = nullptr;
  NimBLERemoteCharacteristic* write_ = nullptr;
  NimBLERemoteCharacteristic* notify_ = nullptr;
  State state_;
  void* queue_ = nullptr;
  studio::ble::LinkHandle link_ = studio::ble::kInvalidLinkHandle;
  uint8_t stream_[192] = {};
  size_t streamLength_ = 0;
  uint32_t deviceId_ = 0;
  uint32_t cameraNumber_ = 0;
  uint16_t sequence_ = 1;
  uint16_t pendingRecordSequence_ = 0;
  uint32_t handshakeDeadlineMs_ = 0;
  uint32_t statusSubscriptionAtMs_ = 0;
  uint32_t commandDeadlineMs_ = 0;
  bool initialized_ = false;
  bool connectRequested_ = false;
  bool haveTarget_ = false;
  bool paired_ = false;
  bool pairingChanged_ = false;
  bool setupPending_ = false;
  bool handshakePending_ = false;
  bool statusSubscriptionPending_ = false;
  uint8_t statusSubscriptionAttempts_ = 0;
  bool commandRequested_ = false;
  bool requestedRecording_ = false;
  char targetAddress_[20] = "";
  uint8_t targetAddressType_ = 0;
  char targetName_[40] = "";
};

}  // namespace dji_osmo
