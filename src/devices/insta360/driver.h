#pragma once
#include "core/device_driver.h"
#include "devices/insta360/state.h"
namespace studio {
class Insta360Driver : public DeviceDriver {
 public:
  DriverId driverId() const override { return DriverId::Insta360; }
  bool activate(const DeviceRecord&) override; void deactivate(InstanceId) override;
  void loop() override; CommandStatus dispatch(const DeviceCommand&) override;
  DeviceRuntimeState runtimeState(InstanceId) const override;
  const void* specializedState(InstanceId) const override;
  void forgetPairing(const DeviceRecord&) override;
  void cancelOnboarding(const DeviceRecord&) override;
  bool consumePairingUpdate(InstanceId, DeviceRecord&) override;
 private:
  class Runtime;
  struct Session {
    InstanceId instanceId = kInvalidInstanceId; insta360::State state;
    uint16_t connHandle = 0xffff; bool paired = false; bool pairingChanged = false;
    bool triggerRequested = false; char address[kBleAddressCapacity] = ""; uint8_t addressType = 0;
  };
  Session* sessionFor(InstanceId); const Session* sessionFor(InstanceId) const;
  Session* sessionForAddress(const char*); Session* firstAwaiting();
  void updateAdvertising();
  Session* sessions_[4] = {}; Runtime* runtime_ = nullptr;
};
}  // namespace studio
