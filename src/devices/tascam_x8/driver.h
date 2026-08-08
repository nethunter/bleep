#pragma once

#include "core/device_driver.h"
#include "devices/tascam_x8/client.h"

namespace studio {

class TascamX8Driver : public DeviceDriver {
 public:
  DriverId driverId() const override { return DriverId::TascamX8; }
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
    tascam_x8::TascamX8Client client;
    InstanceId instanceId = kInvalidInstanceId;
  };
  Session* session_ = nullptr;
};

}  // namespace studio
