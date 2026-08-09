#pragma once

#include <cstddef>
#include <cstdint>

#include "core/ble/ble_central.h"
#include "devices/gopro/state.h"

class NimBLEClient;
class NimBLERemoteCharacteristic;

namespace gopro {

class GoProClient : public studio::ble::BleCentralDelegate {
 public:
  using Link = GoProState::Link;
  using State = GoProState;

  void activate(const char* address, uint8_t addressType, const char* name,
                bool paired);
  void deactivate();
  void loop();
  const State& state() const { return state_; }
  bool protocolReady() const;
  void startScan();
  void forgetDevice();
  void forgetBond(const char* address, uint8_t addressType);
  bool setShutter(bool enabled);
  bool consumePairingUpdate(char* address, size_t addressCapacity,
                            uint8_t& addressType, char* name,
                            size_t nameCapacity, bool& paired);

  void onBleAdvertisement(studio::ble::LinkHandle link,
                          const studio::ble::Advertisement& advertisement) override;
  void onBleEvent(studio::ble::LinkHandle link,
                  const studio::ble::Event& event) override;
  void onResponse(const uint8_t* data, size_t len);
  bool ownsResponseCharacteristic(const void* characteristic) const {
    return responseChar_ == characteristic;
  }

 private:
  struct QueuedResponse {
    uint8_t len = 0;
    uint8_t data[24] = {};
  };

  void begin();
  void beginScan();
  void beginConnect();
  bool completeConnect();
  void teardownConnection();
  void handleDisconnect();
  void drainResponses();
  bool send(const uint8_t* data, size_t len);
  void markReady();

  NimBLEClient* client_ = nullptr;
  NimBLERemoteCharacteristic* commandChar_ = nullptr;
  NimBLERemoteCharacteristic* responseChar_ = nullptr;
  State state_;
  void* responseQueue_ = nullptr;
  bool initialized_ = false;
  bool connectRequested_ = false;
  bool haveTarget_ = false;
  bool pairingChanged_ = false;
  bool setupPending_ = false;
  bool pairingResponsePending_ = false;
  bool commandRequested_ = false;
  bool requestedStart_ = false;
  char targetAddr_[20] = "";
  uint8_t targetAddrType_ = 0;
  char targetName_[40] = "";
  uint32_t responseDeadlineMs_ = 0;
  studio::ble::LinkHandle linkHandle_ = studio::ble::kInvalidLinkHandle;
};

}  // namespace gopro
