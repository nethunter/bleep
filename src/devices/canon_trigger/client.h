#pragma once

#include <cstddef>
#include <cstdint>

#include "core/ble/ble_central.h"
#include "devices/canon_trigger/state.h"

class NimBLEAdvertisedDevice;
class NimBLEClient;
class NimBLERemoteCharacteristic;

namespace canon_trigger {

class CanonTriggerClient : public studio::ble::BleCentralDelegate {
 public:
  using Link = CanonTriggerState::Link;
  using State = CanonTriggerState;

  bool activate(const char* address, uint8_t addressType, const char* name,
                bool paired);
  void deactivate();
  void loop();

  const State& state() const { return state_; }
  bool connected() const { return state_.link == Link::Connected; }
  bool protocolReady() const;

  void retry();
  void startScan();
  void forgetDevice();
  void forgetBond(const char* address, uint8_t addressType);
  void ignorePeerAddress(const char* address);
  bool triggerRecord();
  bool consumePairingUpdate(char* address, size_t addressCapacity,
                            uint8_t& addressType, char* name,
                            size_t nameCapacity, bool& paired);

  void onBleAdvertisement(
      studio::ble::LinkHandle link,
      const studio::ble::Advertisement& advertisement) override;
  void onBleEvent(studio::ble::LinkHandle link,
                  const studio::ble::Event& event) override;

 private:
  bool begin();
  void beginScan();
  void beginConnect();
  bool completeConnect();
  void teardownConnection();
  void handleDisconnect();
  void clearIgnoredAddresses();
  bool isIgnoredAddress(const char* address) const;
  void ignoreAddress(const char* address);
  bool isBondedAdvertisement(
      const studio::ble::Advertisement& advertisement) const;
  void adoptResolvedIdentity();

  NimBLEClient* client_ = nullptr;
  NimBLERemoteCharacteristic* pairingChar_ = nullptr;
  NimBLERemoteCharacteristic* controlChar_ = nullptr;
  State state_;

  bool initialized_ = false;
  bool connectRequested_ = false;
  bool haveTarget_ = false;
  bool newPairing_ = false;
  bool pairingChanged_ = false;
  bool triggerRequested_ = false;
  bool triggerReleasePending_ = false;
  bool setupPending_ = false;

  char targetAddr_[20] = "";
  uint8_t targetAddrType_ = 0;
  char targetName_[40] = "";
  char lockedAddr_[20] = "";
  static constexpr size_t kMaxIgnoredAddresses = 4;
  char ignoredAddrs_[kMaxIgnoredAddresses][20] = {};
  uint8_t ignoredCount_ = 0;
  uint32_t triggerReleaseAtMs_ = 0;
  studio::ble::LinkHandle linkHandle_ =
      studio::ble::kInvalidLinkHandle;
};

}  // namespace canon_trigger
