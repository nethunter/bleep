#pragma once

#include <cstddef>
#include <cstdint>

#include "core/ble/ble_central.h"
#include "core/device_types.h"
#include "devices/amaran_light/state.h"
#include "devices/amaran_light/store.h"

class NimBLERemoteCharacteristic;

namespace amaran_light {

class AmaranRuntime : public studio::ble::BleCentralDelegate {
 public:
  bool activate(const studio::DeviceRecord& record);
  void deactivate(studio::InstanceId instanceId);
  void loop();
  studio::CommandStatus dispatch(const studio::DeviceCommand& command);
  studio::DeviceRuntimeState runtimeState(studio::InstanceId instanceId) const;
  const AmaranLightState* state(studio::InstanceId instanceId) const;
  bool consumePairingUpdate(studio::InstanceId instanceId,
                            studio::DeviceRecord& record);
  void forgetLocal(studio::InstanceId instanceId);

  void onBleAdvertisement(studio::ble::LinkHandle link,
                          const studio::ble::Advertisement& advertisement) override;
  void onBleEvent(studio::ble::LinkHandle link,
                  const studio::ble::Event& event) override;
  void enqueueNotification(const uint8_t* data, size_t length);
#ifdef UI_SIMULATOR
  void simSetPhase(studio::InstanceId instanceId, AmaranLightState::Phase phase);
#endif

 private:
  struct Session {
    studio::InstanceId instanceId = studio::kInvalidInstanceId;
    studio::DriverId model = studio::DriverId::Unknown;
    AmaranLightState state;
    bool pairingDirty = false;
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
  bool handleCapabilities(const uint8_t* pdu, size_t length);
  bool handleDevicePublicKey(const uint8_t* pdu, size_t length);
  bool handleDeviceConfirmation(const uint8_t* pdu, size_t length);
  bool handleDeviceRandom(const uint8_t* pdu, size_t length);
  bool completeProvisioning();
  bool configureNext();
  bool sendAccess(studio::InstanceId instanceId, const uint8_t* access,
                  size_t length);
  void fail(Session& session, const char* error);
  void updateSharedReady();

  Session sessions_[CONFIG_MAX_ACTIVE_INSTANCES] = {};
  studio::ble::LinkHandle link_ = studio::ble::kInvalidLinkHandle;
  studio::InstanceId linkInstance_ = studio::kInvalidInstanceId;
  bool provisioningLink_ = false;
  bool loaded_ = false;
  bool connected_ = false;
  uint8_t provisioningStep_ = 0;
  uint8_t configStep_ = 0;
  uint32_t nextConfigAt_ = 0;
  uint32_t lastLoopMs_ = 0xffffffffu;
  NimBLERemoteCharacteristic* dataIn_ = nullptr;
  Notification notifications_[8] = {};
  uint8_t notifyHead_ = 0;
  uint8_t notifyTail_ = 0;
  MeshStoreData storeData_;
  SequenceAllocator sequences_;
  uint8_t capabilities_[12] = {};
  uint8_t localPublic_[64] = {};
  uint8_t remotePublic_[64] = {};
  uint8_t ecdhSecret_[32] = {};
  uint8_t confirmationSalt_[16] = {};
  uint8_t confirmationKey_[16] = {};
  uint8_t localRandom_[16] = {};
  uint8_t remoteRandom_[16] = {};
  uint8_t remoteConfirmation_[16] = {};
  uint8_t deviceKey_[16] = {};
  char provisioningAddress_[studio::kBleAddressCapacity] = "";
  uint8_t provisioningAddressType_ = 0;
};

AmaranRuntime& runtime();

}  // namespace amaran_light
