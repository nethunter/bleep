#pragma once

#include "core/device_driver.h"
#include "devices/canon_ble/client.h"

namespace studio {

class CanonBleDriver : public DeviceDriver {
 public:
  DriverId driverId() const override { return DriverId::CanonBle; }
  bool activate(const DeviceRecord& record) override;
  bool resume(const DeviceRecord& record) override;
  bool retainWhileDisconnected(InstanceId instanceId) const override;
  void deactivate(InstanceId instanceId) override;
  void loop() override;
  CommandStatus dispatch(const DeviceCommand& command) override;
  DeviceRuntimeState runtimeState(InstanceId instanceId) const override;
  const void* specializedState(InstanceId instanceId) const override;
  void forgetPairing(const DeviceRecord& record) override;
  void preferSkipPeer(InstanceId instanceId, const char* bleAddress) override;
  bool consumePairingUpdate(InstanceId instanceId, DeviceRecord& record) override;

 private:
  static constexpr size_t kMaxSessions = 3;
  struct Session {
    canon_ble::CanonBleClient client;
    InstanceId instanceId = kInvalidInstanceId;
  };
  Session* sessionFor(InstanceId instanceId);
  const Session* sessionFor(InstanceId instanceId) const;
  Session sessions_[kMaxSessions];
};

}  // namespace studio
