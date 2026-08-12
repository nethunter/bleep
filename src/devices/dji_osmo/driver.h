#pragma once

#include "core/device_driver.h"
#include "devices/dji_osmo/client.h"

namespace studio {

class DjiOsmoDriver : public DeviceDriver {
 public:
  DriverId driverId() const override { return DriverId::DjiOsmo; }
  bool activate(const DeviceRecord&) override;
  void deactivate(InstanceId) override;
  void loop() override;
  CommandStatus dispatch(const DeviceCommand&) override;
  DeviceRuntimeState runtimeState(InstanceId) const override;
  const void* specializedState(InstanceId) const override;
  void forgetPairing(const DeviceRecord&) override;
  bool cancelOnboarding(const DeviceRecord&) override;
  bool consumePairingUpdate(InstanceId, DeviceRecord&) override;

 private:
  struct Session { dji_osmo::Client client; InstanceId instanceId = kInvalidInstanceId; };
  Session* sessionFor(InstanceId);
  const Session* sessionFor(InstanceId) const;
  Session* sessions_[4] = {};
};

}  // namespace studio
