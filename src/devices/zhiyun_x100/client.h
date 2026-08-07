#pragma once

#include <cstddef>
#include <cstdint>

#include "core/ble/ble_central.h"
#include "core/device_types.h"
#include "core/mesh/pb_gatt_provisioner.h"
#include "devices/amaran_light/store.h"
#include "devices/zhiyun_x100/protocol.h"
#include "devices/zhiyun_x100/state.h"

class NimBLEClient;
class NimBLERemoteCharacteristic;

namespace zhiyun_x100 {

class X100Client : public studio::ble::BleCentralDelegate,
                   public studio::mesh::ProvisioningSender {
 public:
  void activate(studio::InstanceId instanceId, const char* address,
                uint8_t addressType, const char* name, bool paired);
  void deactivate();
  void loop();

  const X100State& state() const { return state_; }
  bool protocolReady() const;
  bool setPower(bool on);
  bool setCct(uint16_t kelvin, uint8_t brightness);
  bool refresh();
  void startScan();
  void forgetDevice();
  bool consumePairingUpdate(char* address, size_t addressCapacity,
                            uint8_t& addressType, char* name,
                            size_t nameCapacity, bool& paired);

  void onBleAdvertisement(
      studio::ble::LinkHandle link,
      const studio::ble::Advertisement& advertisement) override;
  void onBleEvent(studio::ble::LinkHandle link,
                  const studio::ble::Event& event) override;
  void onNotifyBytes(const uint8_t* data, size_t length);
  bool sendProvisioningPdu(const uint8_t* pdu, size_t length) override;

 private:
  enum class Operation : uint8_t { None, Initialize, Power, Cct, Refresh };

  void begin();
  void beginScan();
  void beginConnect();
  bool completeConnect();
  bool setupProvisioning();
  bool loadMesh();
  bool finishProvisioning();
  void handleProvisioningBytes(const uint8_t* data, size_t length);
  void handleDisconnect();
  void drainNotifications();
  bool writeFrame(const FrameBytes& frame);
  bool sendQuery(uint16_t command, const uint8_t* payload = nullptr,
                 size_t payloadLength = 0);
  bool sendInitializationStep();
  bool sendVerificationStep();
  void handleFrame(const ParsedFrame& frame);
  void finishInitialization();
  void finishCommand(bool success, const char* error = nullptr);
  uint16_t nextSequence();

  NimBLEClient* client_ = nullptr;
  NimBLERemoteCharacteristic* writeCharacteristic_ = nullptr;
  NimBLERemoteCharacteristic* provisioningIn_ = nullptr;
  FrameScanner scanner_;
  X100State state_;
  void* notifyStream_ = nullptr;
  studio::ble::LinkHandle linkHandle_ = studio::ble::kInvalidLinkHandle;
  studio::InstanceId instanceId_ = studio::kInvalidInstanceId;
  bool initialized_ = false;
  bool connectRequested_ = false;
  bool haveTarget_ = false;
  bool pairingChanged_ = false;
  bool setupPending_ = false;
  bool provisioningLink_ = false;
  bool scanAfterProvision_ = false;
  char targetAddress_[20] = "";
  uint8_t targetAddressType_ = 0;
  char targetName_[40] = "";
  uint16_t sequence_ = 2;
  uint16_t expectedSequence_ = 0;
  uint16_t expectedCommand_ = 0;
  bool awaitingResponse_ = false;
  Operation operation_ = Operation::None;
  uint8_t step_ = 0;
  bool desiredPower_ = false;
  float desiredBrightness_ = 0.0f;
  uint16_t desiredKelvin_ = 5600;
  uint32_t setupAtMs_ = 0;
  uint32_t verifyAtMs_ = 0;
  uint32_t responseDeadlineMs_ = 0;
  studio::mesh::PbGattProvisioner provisioner_;
  amaran_light::MeshStoreData meshData_;
  uint8_t provisioningBytes_[160] = {};
  size_t provisioningLength_ = 0;
};

}  // namespace zhiyun_x100
