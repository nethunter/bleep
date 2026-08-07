#pragma once

#include "core/device_driver.h"

namespace studio {

class AmaranLightDriver : public DeviceDriver {
 public:
  explicit AmaranLightDriver(DriverId id) : id_(id) {}
  DriverId driverId() const override { return id_; }
  BleSlotKey bleSlotKey(const DeviceRecord&) const override {
    return {DriverId::AmaranLight, 1};
  }
  bool activate(const DeviceRecord& record) override;
  void deactivate(InstanceId instanceId) override;
  void loop() override;
  CommandStatus dispatch(const DeviceCommand& command) override;
  DeviceRuntimeState runtimeState(InstanceId instanceId) const override;
  const void* specializedState(InstanceId instanceId) const override;
  void forgetPairing(const DeviceRecord& record) override;
  void cancelOnboarding(const DeviceRecord& record) override {
    forgetPairing(record);
  }
  bool consumePairingUpdate(InstanceId instanceId, DeviceRecord& record) override;

 private:
  DriverId id_;
};

}  // namespace studio
