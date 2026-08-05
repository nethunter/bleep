#pragma once

#include "core/device_driver.h"

namespace studio {

class AmaranLightDriver : public DeviceDriver {
 public:
  explicit AmaranLightDriver(DriverId id) : id_(id) {}
  DriverId driverId() const override { return id_; }
  bool activate(const DeviceRecord& record) override;
  void deactivate(InstanceId instanceId) override;
  void loop() override;
  CommandStatus dispatch(const DeviceCommand& command) override;
  DeviceRuntimeState runtimeState(InstanceId instanceId) const override;
  const void* specializedState(InstanceId instanceId) const override;
  void forgetPairing(const DeviceRecord& record) override;
  bool consumePairingUpdate(InstanceId instanceId, DeviceRecord& record) override;

 private:
  DriverId id_;
};

}  // namespace studio
