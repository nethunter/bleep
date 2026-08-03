#pragma once

#include "core/device_driver.h"
#include "shark_client.h"

namespace studio {

class SharkDriver : public DeviceDriver {
 public:
  DriverId driverId() const override { return DriverId::SharkNanoII; }
  void activate(const DeviceRecord& record) override;
  void deactivate() override;
  void loop() override;
  CommandStatus dispatch(const DeviceCommand& command) override;
  DeviceRuntimeState runtimeState() const override;
  const void* specializedState() const override { return &client_.state(); }
  bool consumePairingUpdate(DeviceRecord& record) override;

 private:
  shark::SharkClient client_;
  InstanceId activeInstance_ = kInvalidInstanceId;
  bool active_ = false;
};

}  // namespace studio

