#pragma once

#include "core/device_driver.h"

namespace studio {

class AputureLightDriver : public DeviceDriver {
 public:
  DriverId driverId() const override { return DriverId::AputureLight; }
  BleSlotKey bleSlotKey(const DeviceRecord&) const override {
    return {DriverId::PanelOwnedMesh, 1};
  }
  bool activate(const DeviceRecord& record) override;
  void deactivate(InstanceId instanceId) override;
  void loop() override;
  CommandStatus dispatch(const DeviceCommand& command) override;
  void cancelPendingCommand(InstanceId instanceId) override;
  DeviceRuntimeState runtimeState(InstanceId instanceId) const override;
  const void* specializedState(InstanceId instanceId) const override;
  bool lightControlState(InstanceId instanceId,
                         LightControlState& state) const override;
  void forgetPairing(const DeviceRecord& record) override;
  void cancelOnboarding(const DeviceRecord& record) override {
    forgetPairing(record);
  }
  bool consumePairingUpdate(InstanceId instanceId, DeviceRecord& record) override;

};

}  // namespace studio
