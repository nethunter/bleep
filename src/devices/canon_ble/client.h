#pragma once

#include <cstddef>
#include <cstdint>

#include "devices/canon_ble/state.h"

class NimBLEAdvertisedDevice;
class NimBLEClient;
class NimBLERemoteCharacteristic;

namespace canon_ble {

class CanonBleClient {
 public:
  using Link = CanonBleState::Link;
  using State = CanonBleState;

  void activate(const char* address, uint8_t addressType, const char* name,
                bool paired);
  void deactivate();
  void loop();

  const State& state() const { return state_; }
  bool connected() const {
    return state_.link == Link::Connected &&
           state_.phase == State::Phase::Ready;
  }

  void startScan();
  void forgetDevice();
  void forgetBond(const char* address, uint8_t addressType);
  bool startRecording();
  bool stopRecording();
  bool powerOn();
  bool powerOff();
  bool consumePairingUpdate(char* address, size_t addressCapacity,
                            uint8_t& addressType, char* name,
                            size_t nameCapacity, bool& paired);

  // NimBLE host-task callbacks only set flags or queue raw notification bytes.
  void onScanMatch(const NimBLEAdvertisedDevice* device);
  void onLinkConnected();
  void onConnectFailed();
  void onLinkDisconnected();
  void onSecurityComplete(bool succeeded);
  void onPairingNotification(const uint8_t* data, size_t len);
  void onPairingInfoNotification(const uint8_t* data, size_t len);
  void onModeNotification(const uint8_t* data, size_t len);
  void onShootingNotification(const uint8_t* data, size_t len);

 private:
  void begin();
  void beginScan();
  void beginConnect();
  bool completeConnect();
  bool sendNewHandshakeIdentity();
  bool finishAcceptedHandshake();
  bool subscribePairingInfo();
  bool beginPostPairSetup();
  bool sendPostPairCommand();
  bool openCoreSession();
  void teardownConnection();
  void handleDisconnect();
  void handleSecurityFailure();
  void readInitialRecordingState();
  void drainNotifications();
  void queueNotification(uint8_t kind, const uint8_t* data, size_t len);
  bool writeCommand(NimBLERemoteCharacteristic* characteristic,
                    const CommandBytes& command);
  void markReady();
  void scheduleRetry(uint32_t delayMs);

  NimBLEClient* client_ = nullptr;
  NimBLERemoteCharacteristic* pairingCommandChar_ = nullptr;
  NimBLERemoteCharacteristic* pairingDataChar_ = nullptr;
  NimBLERemoteCharacteristic* pairingInfoChar_ = nullptr;
  NimBLERemoteCharacteristic* modeCommandChar_ = nullptr;
  NimBLERemoteCharacteristic* modeResultChar_ = nullptr;
  NimBLERemoteCharacteristic* shootingCommandChar_ = nullptr;
  NimBLERemoteCharacteristic* shootingStateChar_ = nullptr;
  State state_;
  void* notifyQueue_ = nullptr;

  bool initialized_ = false;
  bool connectRequested_ = false;
  bool haveTarget_ = false;
  bool newHandshake_ = false;
  bool scanActive_ = false;
  bool pairingChanged_ = false;
  bool startRequested_ = false;
  bool stopRequested_ = false;
  bool powerOffRequested_ = false;
  bool setupPending_ = false;
  bool bondRecoveryPending_ = false;
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
  uint32_t setupAtMs_ = 0;
  uint32_t phaseDeadlineMs_ = 0;
  uint32_t commandDeadlineMs_ = 0;
  uint8_t postPairStep_ = 0;
  int connectFails_ = 0;
  int securityFails_ = 0;
};

}  // namespace canon_ble
