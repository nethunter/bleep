#pragma once

#include <cstddef>
#include <cstdint>

#include "core/ble/ble_central.h"
#include "core/ble/onboarding_candidates.h"
#include "core/device_types.h"
#include "core/mesh/pb_gatt_provisioner.h"
#include "devices/aputure_light/state.h"
#include "devices/aputure_light/store.h"
#include "devices/aputure_light/protocol.h"

class NimBLERemoteCharacteristic;

namespace aputure_light {

class AputureLightRuntime : public studio::ble::BleCentralDelegate,
                      public studio::mesh::ProvisioningSender {
 public:
  bool activate(const studio::DeviceRecord& record);
  void deactivate(studio::InstanceId instanceId);
  void loop();
  studio::CommandStatus dispatch(const studio::DeviceCommand& command);
  studio::DeviceRuntimeState runtimeState(studio::InstanceId instanceId) const;
  const AputureLightState* state(studio::InstanceId instanceId) const;
  bool consumePairingUpdate(studio::InstanceId instanceId,
                            studio::DeviceRecord& record);
  bool identifyVendorModel(studio::InstanceId instanceId, uint16_t companyId,
                           uint16_t modelId,
                           const char* productName = nullptr);
  bool canIdentifyVendorModel(studio::InstanceId instanceId) const;
  void cancelPendingCommand(studio::InstanceId instanceId);
  void forgetLocal(studio::InstanceId instanceId);
  bool cancelOnboarding(studio::InstanceId instanceId);
  size_t onboardingCandidateCount(studio::InstanceId instanceId) const;
  bool onboardingCandidate(studio::InstanceId instanceId, size_t index,
                           studio::OnboardingCandidate& candidate) const;
  bool selectOnboardingCandidate(studio::InstanceId instanceId,
                                 uint32_t token);
  // Saved mesh members from other protocol families attach to this one
  // physical proxy bearer. PB-GATT onboarding remains an exclusive temporary
  // operation; steady-state control uses the shared native client below.
  bool acquireGateway(const studio::DeviceRecord& record);
  void releaseGateway(studio::InstanceId instanceId);
  void* gatewayClient() const;
  studio::ble::LinkHandle gatewayLink() const { return link_; }
  bool gatewayConnected() const { return connected_ && !provisioningLink_; }
  uint32_t gatewayGeneration() const { return gatewayGeneration_; }
  bool idle() const { return !hasActiveUsers(); }

  void onBleAdvertisement(studio::ble::LinkHandle link,
                          const studio::ble::Advertisement& advertisement) override;
  void onBleEvent(studio::ble::LinkHandle link,
                  const studio::ble::Event& event) override;
  void enqueueNotification(const uint8_t* data, size_t length);
  bool sendProvisioningPdu(const uint8_t* pdu, size_t length) override;
#ifdef UI_SIMULATOR
  void simSetPhase(studio::InstanceId instanceId, AputureLightState::Phase phase);
  bool simObserveCandidate(const studio::ble::Advertisement& advertisement);
#endif

 private:
  struct Session {
    studio::InstanceId instanceId = studio::kInvalidInstanceId;
    studio::DriverId model = studio::DriverId::Unknown;
    AputureLightState state;
    bool pairingDirty = false;
    bool receiveSequenceKnown = false;
    uint32_t receiveSequence = 0;
    bool followupPowerPending = false;
    bool followupPowerOn = false;
    bool followupLookPending = false;
    AccessPayload followupLook = {};
    studio::CommandType followupLookType = studio::CommandType::Refresh;
    int32_t followupValue0 = 0;
    int32_t followupValue1 = 0;
    int32_t followupValue2 = 0;
    uint32_t followupPowerAt = 0;
    char productName[studio::kBleNameCapacity] = "";
  };
  struct Notification {
    uint8_t bytes[80] = {};
    uint8_t length = 0;
  };

  Session* sessionFor(studio::InstanceId instanceId);
  const Session* sessionFor(studio::InstanceId instanceId) const;
  bool ensureLoaded();
  bool beginLink(studio::InstanceId instanceId, bool provisioning);
  bool setupProvisioning();
  bool setupProxy();
  void processNotification(const Notification& notification);
  bool sendProvisioning(const uint8_t* pdu, size_t length);
  bool completeProvisioning();
  bool configureNext();
  bool handleConfigurationStatus(const DecodedAccessMessage& decoded);
  bool sendAccess(studio::InstanceId instanceId, const uint8_t* access,
                  size_t length);
  bool sendAccessTo(uint16_t destination, const uint8_t* access,
                    size_t length);
  uint16_t controlGroupFor(studio::InstanceId instanceId) const;
  void fail(Session& session, const char* error);
  void updateSharedReady();
  studio::InstanceId preferredGatewayInstance() const;
  bool hasActiveUsers() const;
  bool isKnownGatewayAddress(const char* address) const;
  void returnToOnboardingPicker(const char* error = nullptr);
  bool rollbackPendingProvision();

  Session sessions_[CONFIG_MAX_ACTIVE_INSTANCES] = {};
  studio::InstanceId gatewayUsers_[CONFIG_MAX_ACTIVE_INSTANCES] = {};
  studio::ble::LinkHandle link_ = studio::ble::kInvalidLinkHandle;
  studio::InstanceId linkInstance_ = studio::kInvalidInstanceId;
  bool provisioningLink_ = false;
  bool connected_ = false;
  uint8_t configStep_ = 0;
  uint8_t configRetryCount_ = 0;
  bool configAwaitingStatus_ = false;
  NetworkPduBatch configBatch_ = {};
  DeviceMessageReassembly configReassembly_ = {};
  uint8_t configBatchIndex_ = 0;
  uint32_t configStatusDeadlineMs_ = 0;
  uint32_t provisioningStartedAt_ = 0;
  uint32_t provisioningDeadlineMs_ = 0;
  uint32_t configurationDeadlineMs_ = 0;
  uint32_t nextConfigAt_ = 0;
  uint32_t lastLoopMs_ = 0xffffffffu;
  uint32_t gatewayGeneration_ = 0;
  NimBLERemoteCharacteristic* dataIn_ = nullptr;
  Notification notifications_[8] = {};
  uint8_t notifyHead_ = 0;
  uint8_t notifyTail_ = 0;
  char provisioningAddress_[studio::kBleAddressCapacity] = "";
  uint8_t provisioningAddressType_ = 0;
  char provisioningName_[studio::kBleNameCapacity] = "";
  studio::ble::OnboardingCandidates candidates_;
  MeshStoreData* provisioningSnapshot_ = nullptr;
  studio::mesh::PbGattProvisioner provisioner_;
};

AputureLightRuntime* runtime();
AputureLightRuntime* runtimeIfActive();
void releaseRuntimeIfIdle();

}  // namespace aputure_light
