#pragma once

#include "core/device_driver.h"
#include "devices/phone_camera/state.h"

namespace studio {

class PhoneCameraDriver : public DeviceDriver {
 public:
  DriverId driverId() const override { return DriverId::PhoneCamera; }
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
  class Runtime;
  struct Session {
    InstanceId instanceId = kInvalidInstanceId;
    phone_camera::PhoneCameraState state;
    bool paired = false;
    bool pairingChanged = false;
    bool triggerRequested = false;
    bool releasePending = false;
    uint16_t connHandle = 0xffff;
    uint32_t releaseAtMs = 0;
    char address[kBleAddressCapacity] = "";
    uint8_t addressType = 0;
  };
  Session* sessionFor(InstanceId instanceId);
  const Session* sessionFor(InstanceId instanceId) const;
  Session* sessionForAddress(const char* address);
  Session* firstAwaitingSession();
  void startAdvertisingIfNeeded();
  Session* sessions_[4] = {};
  Runtime* runtime_ = nullptr;
};

}  // namespace studio
