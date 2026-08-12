#pragma once

#include "core/device_driver.h"
#include "devices/gopro/client.h"

namespace studio {

class GoProDriver : public DeviceDriver {
 public:
  DriverId driverId() const override { return DriverId::GoPro; }
  bool activate(const DeviceRecord& record) override;
  void deactivate(InstanceId instanceId) override;
  void loop() override;
  CommandStatus dispatch(const DeviceCommand& command) override;
  DeviceRuntimeState runtimeState(InstanceId instanceId) const override;
  const void* specializedState(InstanceId instanceId) const override;
  void forgetPairing(const DeviceRecord& record) override;
  bool cancelOnboarding(const DeviceRecord& record) override;
  bool consumePairingUpdate(InstanceId instanceId, DeviceRecord& record) override;

 private:
  struct Session {
    gopro::GoProClient client;
    InstanceId instanceId = kInvalidInstanceId;
  };
  Session* sessionFor(InstanceId instanceId);
  const Session* sessionFor(InstanceId instanceId) const;
  Session* sessions_[4] = {};
};

}  // namespace studio
