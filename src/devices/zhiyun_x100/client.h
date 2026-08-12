#pragma once

#include <cstddef>
#include <cstdint>

#include "core/ble/ble_central.h"
#include "core/ble/onboarding_candidates.h"
#include "core/device_types.h"
#include "core/mesh/pb_gatt_provisioner.h"
#include "core/mesh/mesh_store.h"
#include "devices/zhiyun_x100/protocol.h"
#include "devices/zhiyun_x100/state.h"

class NimBLEClient;
class NimBLERemoteCharacteristic;

namespace zhiyun_x100 {

class X100Client : public studio::ble::BleCentralDelegate,
                   public studio::mesh::ProvisioningSender {
 public:
  bool activate(studio::InstanceId instanceId, const char* address,
                uint8_t addressType, const char* name, bool paired);
  bool activateShared(studio::InstanceId instanceId, const char* address,
                      uint8_t addressType, const char* name, bool paired);
  bool attachShared(void* nativeClient, studio::ble::LinkHandle link);
  void detachShared();
  void deactivate();
  void loop();

  const X100State& state() const { return state_; }
  bool protocolReady() const;
  bool setPower(bool on);
  bool setCct(uint16_t kelvin, uint8_t brightness);
  bool setRgb(uint32_t rgb, uint8_t brightness);
  bool refresh();
  void cancelPendingCommand();
  void startScan();
  void forgetDevice();
  bool cancelOnboarding();
  size_t onboardingCandidateCount() const { return candidates_.count(); }
  bool onboardingCandidate(size_t index,
                           studio::OnboardingCandidate& candidate) const;
  bool selectOnboardingCandidate(uint32_t token);
  void ignorePeerAddress(const char* address);
  bool consumePairingUpdate(char* address, size_t addressCapacity,
                            uint8_t& addressType, char* name,
                            size_t nameCapacity, bool& paired);

  void onBleAdvertisement(
      studio::ble::LinkHandle link,
      const studio::ble::Advertisement& advertisement) override;
  void onBleEvent(studio::ble::LinkHandle link,
                  const studio::ble::Event& event) override;
  void onNotifyBytes(const uint8_t* data, size_t length);
  bool ownsNotifyCharacteristic(
      const NimBLERemoteCharacteristic* characteristic) const;
  bool sendProvisioningPdu(const uint8_t* pdu, size_t length) override;

 private:
  enum class Operation : uint8_t {
    None,
    Initialize,
    Power,
    Cct,
    Rgb,
    Refresh
  };

  bool begin();
  bool beginShared();
  void prepareActivation(studio::InstanceId instanceId, const char* address,
                         uint8_t addressType, const char* name, bool paired);
  void beginScan();
  void beginConnect();
  bool completeConnect();
  bool setupProvisioning();
  bool loadMesh();
  bool finishProvisioning();
  void handleProvisioningBytes(const uint8_t* data, size_t length);
  void handleDisconnect();
  void returnToOnboardingPicker(const char* error = nullptr);
  bool rollbackPendingProvision();
  void drainNotifications();
  bool writeFrame(const FrameBytes& frame);
  bool sendQuery(uint16_t command, const uint8_t* payload = nullptr,
                 size_t payloadLength = 0);
  bool sendInitializationStep();
  bool sendVerificationStep();
  bool sendRgbStep();
  bool retryCctVerification();
  void handleFrame(const ParsedFrame& frame);
  void finishInitialization();
  void finishCommand(bool success, const char* error = nullptr);
  void markProtocolReady();
  void markProtocolFailed();
  uint16_t nextSequence();
  uint8_t selector() const { return routingSelector_; }
  const char* identityMarker() const;

  NimBLEClient* client_ = nullptr;
  NimBLERemoteCharacteristic* writeCharacteristic_ = nullptr;
  NimBLERemoteCharacteristic* notifyCharacteristic_ = nullptr;
  NimBLERemoteCharacteristic* provisioningIn_ = nullptr;
  NimBLERemoteCharacteristic* provisioningOut_ = nullptr;
  FrameScanner scanner_;
  X100State state_;
  void* notifyStream_ = nullptr;
  studio::ble::LinkHandle linkHandle_ = studio::ble::kInvalidLinkHandle;
  studio::InstanceId instanceId_ = studio::kInvalidInstanceId;
  bool initialized_ = false;
  bool sharedTransport_ = false;
  bool connectRequested_ = false;
  bool haveTarget_ = false;
  bool pairingChanged_ = false;
  bool setupPending_ = false;
  bool provisioningLink_ = false;
  bool scanAfterProvision_ = false;
  char targetAddress_[20] = "";
  uint8_t targetAddressType_ = 0;
  char targetName_[40] = "";
  char ignoredAddresses_[CONFIG_BLE_MAX_SKIP_ADDRESSES][20] = {};
  uint8_t ignoredAddressCount_ = 0;
  studio::ble::OnboardingCandidates candidates_;
  studio::mesh::StoreData* provisioningSnapshot_ = nullptr;
  bool onboardingSelectionActive_ = false;
  uint8_t routingSelector_ = 0;
  uint16_t sequence_ = 2;
  uint16_t expectedSequence_ = 0;
  uint16_t expectedCommand_ = 0;
  bool awaitingResponse_ = false;
  uint8_t verificationAttempts_ = 0;
  Operation operation_ = Operation::None;
  uint8_t step_ = 0;
  bool desiredPower_ = false;
  float desiredBrightness_ = 0.0f;
  uint16_t desiredKelvin_ = 5600;
  uint32_t desiredRgb_ = 0xffffff;
  uint16_t desiredHue_ = 0;
  uint8_t desiredSaturation_ = 0;
  uint32_t setupAtMs_ = 0;
  uint32_t verifyAtMs_ = 0;
  uint32_t responseDeadlineMs_ = 0;
  uint32_t provisioningDeadlineMs_ = 0;
  studio::mesh::PbGattProvisioner provisioner_;
  uint8_t provisioningBytes_[160] = {};
  size_t provisioningLength_ = 0;
};

}  // namespace zhiyun_x100
