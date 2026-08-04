#pragma once

#include <cstddef>
#include <cstdint>

#include "devices/canon_trigger/state.h"

class NimBLEAdvertisedDevice;
class NimBLEClient;
class NimBLERemoteCharacteristic;

namespace canon_trigger {

class CanonTriggerClient {
 public:
  using Link = CanonTriggerState::Link;
  using State = CanonTriggerState;

  void activate(const char* address, uint8_t addressType, const char* name,
                bool paired);
  void deactivate();
  void loop();

  const State& state() const { return state_; }
  bool connected() const { return state_.link == Link::Connected; }

  void startScan();
  void forgetDevice();
  void forgetBond(const char* address, uint8_t addressType);
  bool triggerRecord();
  bool consumePairingUpdate(char* address, size_t addressCapacity,
                            uint8_t& addressType, char* name,
                            size_t nameCapacity, bool& paired);

  // NimBLE host-task callbacks only set flags or copy advertisement identity.
  void onScanMatch(const NimBLEAdvertisedDevice* device);
  void onLinkConnected();
  void onConnectFailed();
  void onLinkDisconnected();
  void onSecurityComplete(bool succeeded);

 private:
  void begin();
  void beginScan();
  void beginConnect();
  bool completeConnect();
  void teardownConnection();
  void handleDisconnect();
  void scheduleRetry(uint32_t delayMs);

  NimBLEClient* client_ = nullptr;
  NimBLERemoteCharacteristic* pairingChar_ = nullptr;
  NimBLERemoteCharacteristic* controlChar_ = nullptr;
  State state_;

  bool initialized_ = false;
  bool connectRequested_ = false;
  bool haveTarget_ = false;
  bool scanActive_ = false;
  bool pairingChanged_ = false;
  bool triggerRequested_ = false;
  bool triggerReleasePending_ = false;
  volatile bool scanHit_ = false;
  volatile bool connectedFlag_ = false;
  volatile bool connectFailedFlag_ = false;
  volatile bool disconnectedFlag_ = false;
  volatile bool securityCompleteFlag_ = false;
  volatile bool securitySucceeded_ = false;

  char targetAddr_[20] = "";
  uint8_t targetAddrType_ = 0;
  char targetName_[40] = "";
  char scanHitAddr_[20] = "";
  uint8_t scanHitType_ = 0;
  char scanHitName_[40] = "";

  uint32_t retryAtMs_ = 0;
  uint32_t triggerReleaseAtMs_ = 0;
  int connectFails_ = 0;
};

}  // namespace canon_trigger
