#pragma once

#include <cstddef>
#include <cstdint>

#include "devices/tascam_x8/protocol.h"
#include "devices/tascam_x8/state.h"

class NimBLEAdvertisedDevice;
class NimBLEClient;
class NimBLERemoteCharacteristic;

namespace tascam_x8 {

class TascamX8Client {
 public:
  using Link = TascamX8State::Link;
  using State = TascamX8State;

  void activate(const char* address, uint8_t addressType, const char* name,
                bool paired);
  void deactivate();
  void loop();

  const State& state() const { return state_; }
  bool connected() const { return state_.link == Link::Connected; }

  void startScan();
  void forgetDevice();
  bool startRecording();
  bool stopRecording();
  bool consumePairingUpdate(char* address, size_t addressCapacity,
                            uint8_t& addressType, char* name,
                            size_t nameCapacity, bool& paired);

  // NimBLE host-task callbacks only copy identity, bytes, or flags.
  void onScanMatch(const NimBLEAdvertisedDevice* device);
  void onLinkDisconnected();
  void onDataBytes(const uint8_t* data, size_t len);
  void onSessionByte(uint8_t value);

 private:
  void begin();
  void beginScan();
  void beginConnect();
  bool completeConnect();
  void teardownConnection();
  void handleDisconnect();
  void drainNotifications();
  bool sendData(const FrameBytes& frame);
  void scheduleRetry(uint32_t delayMs);

  NimBLEClient* client_ = nullptr;
  NimBLERemoteCharacteristic* dataChar_ = nullptr;
  NimBLERemoteCharacteristic* sessionChar_ = nullptr;
  FrameScanner scanner_;
  State state_;
  void* notifyStream_ = nullptr;

  bool initialized_ = false;
  bool connectRequested_ = false;
  bool haveTarget_ = false;
  bool scanActive_ = false;
  bool pairingChanged_ = false;
  bool startRequested_ = false;
  bool stopRequested_ = false;
  volatile bool scanHit_ = false;
  volatile bool disconnectedFlag_ = false;
  volatile uint8_t sessionByte_ = 0;

  char targetAddr_[20] = "";
  uint8_t targetAddrType_ = 0;
  char targetName_[40] = "";
  char scanHitAddr_[20] = "";
  uint8_t scanHitType_ = 0;
  char scanHitName_[40] = "";

  uint32_t retryAtMs_ = 0;
  uint32_t sessionDeadlineMs_ = 0;
  uint32_t keepaliveAtMs_ = 0;
  uint32_t commandDeadlineMs_ = 0;
  int connectFails_ = 0;
};

}  // namespace tascam_x8
