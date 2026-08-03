#pragma once

#include "core/device_driver.h"
#include "devices/tascam_x8/client.h"

namespace studio {

class TascamX8Driver : public DeviceDriver {
 public:
  DriverId driverId() const override { return DriverId::TascamX8; }
  void activate(const DeviceRecord& record) override;
  void deactivate() override;
  void loop() override;
  CommandStatus dispatch(const DeviceCommand& command) override;
  DeviceRuntimeState runtimeState() const override;
  const void* specializedState() const override { return &client_.state(); }
  bool consumePairingUpdate(DeviceRecord& record) override;

 private:
  tascam_x8::TascamX8Client client_;
  InstanceId activeInstance_ = kInvalidInstanceId;
  bool active_ = false;
};

}  // namespace studio
