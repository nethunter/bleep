#pragma once

#include <cstddef>
#include <cstdint>

#include "core/ble/ble_central.h"
#include "devices/canon_ble/state.h"

class NimBLEClient;
class NimBLERemoteCharacteristic;

namespace canon_ble {

class CanonBleClient : public studio::ble::BleCentralDelegate {
 public:
  using Link = CanonBleState::Link;
  using State = CanonBleState;

  bool activate(const char* address, uint8_t addressType, const char* name,
                bool paired);
  void deactivate();
  void loop();

  const State& state() const { return state_; }
  bool connected() const {
    return state_.link == Link::Connected &&
           state_.phase == State::Phase::Ready;
  }
  bool protocolReady() const;

  void retry();
  void startScan();
  void forgetDevice();
  void forgetBond(const char* address, uint8_t addressType);
  void ignorePeerAddress(const char* address);
  bool startRecording();
  bool stopRecording();
  bool powerOn();
  bool powerOff();
  bool consumePairingUpdate(char* address, size_t addressCapacity,
                            uint8_t& addressType, char* name,
                            size_t nameCapacity, bool& paired);

  void onBleAdvertisement(
      studio::ble::LinkHandle link,
      const studio::ble::Advertisement& advertisement) override;
  void onBleEvent(studio::ble::LinkHandle link,
                  const studio::ble::Event& event) override;
  // Characteristic callbacks only queue raw notification bytes.
  void onPairingNotification(const uint8_t* data, size_t len);
  void onPairingInfoNotification(const uint8_t* data, size_t len);
  void onModeNotification(const uint8_t* data, size_t len);
  void onShootingNotification(const uint8_t* data, size_t len);

 private:
  bool begin();
  void beginScan();
  void beginConnect();
  bool completeConnect();
  void failProtocol();
  bool sendNewHandshakeIdentity();
  bool finishAcceptedHandshake();
  bool subscribePairingInfo();
  bool beginPostPairSetup();
  bool sendPostPairCommand();
  bool openCoreSession();
  void handleDisconnect();
  void handleSecurityFailure();
  void readInitialRecordingState();
  void drainNotifications();
  void queueNotification(uint8_t kind, const uint8_t* data, size_t len);
  bool writeCommand(NimBLERemoteCharacteristic* characteristic,
                    const CommandBytes& command);
  void markReady();
  void clearIgnoredAddresses();
  bool isIgnoredAddress(const char* address) const;
  void ignoreAddress(const char* address);
  void considerScanCandidate(
      const studio::ble::Advertisement& advertisement, const char* name,
      bool hasService, bool hasMfg);
  int candidateScore(const char* name, bool hasService, bool hasMfg) const;

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
  bool pairingChanged_ = false;
  bool startRequested_ = false;
  bool stopRequested_ = false;
  bool powerOffRequested_ = false;
  bool setupPending_ = false;
  bool bondRecoveryPending_ = false;
  bool openingShootRequested_ = false;

  char targetAddr_[20] = "";
  uint8_t targetAddrType_ = 0;
  char targetName_[40] = "";
  // When set, reconnect/scan may only use this address (per-instance lock).
  char lockedAddr_[20] = "";
  static constexpr size_t kMaxIgnoredAddresses = 4;
  char ignoredAddrs_[kMaxIgnoredAddresses][20] = {};
  uint8_t ignoredCount_ = 0;
  bool scanCandidatePending_ = false;
  studio::ble::Advertisement scanCandidate_;
  char scanCandidateName_[40] = "";
  bool scanCandidateHasService_ = false;
  bool scanCandidateHasMfg_ = false;
  int scanCandidateScore_ = -1;

  uint32_t setupAtMs_ = 0;
  uint32_t scanDwellUntilMs_ = 0;
  uint32_t phaseDeadlineMs_ = 0;
  uint32_t commandDeadlineMs_ = 0;
  uint8_t postPairStep_ = 0;
  int securityFails_ = 0;
  studio::ble::LinkHandle linkHandle_ =
      studio::ble::kInvalidLinkHandle;
};

}  // namespace canon_ble
