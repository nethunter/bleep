#pragma once

#include "core/device_driver.h"
#include "devices/canon_trigger/client.h"

namespace studio {

class CanonTriggerDriver : public DeviceDriver {
 public:
  DriverId driverId() const override { return DriverId::CanonTrigger; }
  void activate(const DeviceRecord& record) override;
  void deactivate() override;
  void loop() override;
  CommandStatus dispatch(const DeviceCommand& command) override;
  DeviceRuntimeState runtimeState() const override;
  const void* specializedState() const override { return &client_.state(); }
  void forgetPairing(const DeviceRecord& record) override;
  bool consumePairingUpdate(DeviceRecord& record) override;

 private:
  canon_trigger::CanonTriggerClient client_;
  InstanceId activeInstance_ = kInvalidInstanceId;
  bool active_ = false;
};

}  // namespace studio
