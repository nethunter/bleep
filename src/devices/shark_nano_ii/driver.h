#pragma once

#include "core/device_driver.h"
#include "devices/shark_nano_ii/client.h"

namespace studio {

class SharkDriver : public DeviceDriver {
 public:
  DriverId driverId() const override { return DriverId::SharkNanoII; }
  bool activate(const DeviceRecord& record) override;
  void deactivate(InstanceId instanceId) override;
  void loop() override;
  CommandStatus dispatch(const DeviceCommand& command) override;
  DeviceRuntimeState runtimeState(InstanceId instanceId) const override;
  const void* specializedState(InstanceId instanceId) const override;
  void cancelOnboarding(const DeviceRecord& record) override;
  bool consumePairingUpdate(InstanceId instanceId, DeviceRecord& record) override;

 private:
  struct Session {
    shark::SharkClient client;
    InstanceId instanceId = kInvalidInstanceId;
  } session_;
};

}  // namespace studio
