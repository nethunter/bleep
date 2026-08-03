#pragma once

#include <cstdint>

#include "devices/shark_nano_ii/protocol.h"
#include "devices/shark_nano_ii/state.h"

// Forward declarations to keep NimBLE headers out of this interface.
class NimBLEClient;
class NimBLERemoteCharacteristic;
class NimBLEAdvertisedDevice;

namespace shark {

// BLE central controller for the iFootage Shark Nano II. Owns the NimBLE
// connection lifecycle (scan / connect / auto-reconnect), parses notifications
// into a single shared state struct, and exposes high-level operator actions.
//
// Threading: NimBLE callbacks run on the host task and only ever push raw bytes
// into a stream buffer or flip flags. All parsing, state mutation, and GATT
// writes happen from loop() via poll(). The UI reads state() from loop() too.
class SharkClient {
 public:
  using Link = SharkState::Link;
  using State = SharkState;

  void begin();
  void activate(const char* address, uint8_t addressType, const char* name, bool paired);
  void deactivate();
  void loop();

  const State& state() const { return state_; }
  bool connected() const { return state_.link == Link::Connected; }

  // Connection management.
  void startScan();
  void disconnectLink();
  void forgetDevice();
  bool consumePairingUpdate(char* address, size_t addressCapacity, uint8_t& addressType,
                            char* name, size_t nameCapacity, bool& paired);

  // Operator actions (ignored unless connected). `slot` is the keypoint index
  // 0..7 (A..H).
  void keypointSet(int slot);
  void keypointGo(int slot);
  void keypointDelete(int slot);
  void setSpeed(int slot, int percent);
  void setHold(int slot, int seconds);
  void requestTiming();
  void setRunState(uint8_t runState);
  void setLoop(bool on);
  void setDirection(bool reverse);
  void setManualTracking(bool enabled);
  void setMotionVector(int slideVelocity, int panVelocity);
  void stopMotion();
  void refreshAll();

  // Called only from NimBLE callbacks.
  void onScanMatch(const NimBLEAdvertisedDevice* device);
  void onLinkDisconnected();
  void onNotifyBytes(const uint8_t* data, size_t len);

 private:
  uint8_t nextTx() { return ++tx_; }
  bool sendFrame(const FrameBytes& frame, bool response = true);

  void drainNotifications();
  void applyFrame(const ParsedFrame& frame);
  void applyTimingTable(const ParsedFrame& frame);

  void beginScan();
  void beginConnect();
  void completeConnect();
  void teardownConnection();
  void sendHandshake();

  void resetDeviceState();
  void handleDisconnect();
  void editTiming(int slot, int speed, int holdSeconds);

  static void scheduleRetry(uint32_t& whenMs, uint32_t delayMs);

  NimBLEClient* client_ = nullptr;
  NimBLERemoteCharacteristic* writeChar_ = nullptr;
  FrameScanner scanner_;
  State state_;

  // FreeRTOS StreamBufferHandle_t, stored as void* to keep FreeRTOS headers out
  // of this interface.
  void* notifyStream_ = nullptr;

  // Latest raw 29-byte timing table for read-modify-write edits.
  uint8_t timingTable_[kTimingDataLen] = {0};
  bool haveTable_ = false;

  uint8_t tx_ = 0;

  // Target device for (re)connection.
  bool haveTarget_ = false;
  char targetAddr_[20] = "";
  uint8_t targetAddrType_ = 0;
  char targetName_[40] = "";

  // Flags/handshake set from the host task, consumed in loop().
  volatile bool scanHit_ = false;
  volatile bool disconnectedFlag_ = false;
  char scanHitAddr_[20] = "";
  uint8_t scanHitType_ = 0;
  char scanHitName_[40] = "";

  uint32_t retryAtMs_ = 0;
  int connectFails_ = 0;
  bool scanActive_ = false;
  bool initialized_ = false;
  bool connectRequested_ = false;
  bool pairingChanged_ = false;

  // Pending manual-tracking ACK (tx-matched), mirrors the web controller.
  bool trackingPending_ = false;
  uint8_t trackingPendingTx_ = 0;
  bool trackingPendingValue_ = false;
  uint32_t trackingPendingExpiryMs_ = 0;

  // Pending timing edit applied once a fresh timing table arrives.
  bool timingPending_ = false;
  int timingPendingSlot_ = -1;
  int timingPendingSpeed_ = -1;
  int timingPendingHold_ = -1;
};

}  // namespace shark
