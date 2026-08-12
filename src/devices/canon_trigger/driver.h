#pragma once

#include "core/device_driver.h"
#include "devices/canon_trigger/client.h"

namespace studio {

class CanonTriggerDriver : public DeviceDriver {
 public:
  DriverId driverId() const override { return DriverId::CanonTrigger; }
  bool activate(const DeviceRecord& record) override;
  void deactivate(InstanceId instanceId) override;
  void loop() override;
  CommandStatus dispatch(const DeviceCommand& command) override;
  DeviceRuntimeState runtimeState(InstanceId instanceId) const override;
  const void* specializedState(InstanceId instanceId) const override;
  void forgetPairing(const DeviceRecord& record) override;
  bool cancelOnboarding(const DeviceRecord& record) override;
  void preferSkipPeer(InstanceId instanceId, const char* bleAddress) override;
  bool consumePairingUpdate(InstanceId instanceId, DeviceRecord& record) override;

 private:
  static constexpr size_t kMaxSessions = 3;
  struct Session {
    canon_trigger::CanonTriggerClient client;
    InstanceId instanceId = kInvalidInstanceId;
    bool metadataRepairPending = false;
  };
  Session* sessionFor(InstanceId instanceId);
  const Session* sessionFor(InstanceId instanceId) const;
  Session* sessions_[kMaxSessions] = {};
};

}  // namespace studio
