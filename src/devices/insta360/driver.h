#pragma once
#include "core/device_driver.h"
#include "devices/insta360/state.h"
namespace studio {
class Insta360Driver : public DeviceDriver {
 public:
  DriverId driverId() const override { return DriverId::Insta360; }
  bool activate(const DeviceRecord&) override;
  void deactivate(InstanceId) override;
  bool retainWhileDisconnected(InstanceId) const override;
  void loop() override;
  CommandStatus dispatch(const DeviceCommand&) override;
  DeviceRuntimeState runtimeState(InstanceId) const override;
  const void* specializedState(InstanceId) const override;
  void forgetPairing(const DeviceRecord&) override;
  bool cancelOnboarding(const DeviceRecord&) override;
  bool consumePairingUpdate(InstanceId, DeviceRecord&) override;

 private:
  class Runtime;
  struct Session {
    InstanceId instanceId = kInvalidInstanceId;
    insta360::State state;
    uint16_t connHandle = 0xffff;
    bool paired = false;
    bool pairingChanged = false;
    enum class TriggerTarget : uint8_t { None, Start, Stop };
    bool triggerRequested = false;
    TriggerTarget triggerTarget = TriggerTarget::None;
    bool powerOffRequested = false;
    bool powerOnRequested = false;
    uint32_t commandDeadlineMs = 0;
    uint32_t connectedAtMs = 0;
    uint32_t syncDeadlineMs = 0;
    uint8_t diagnosticWritesRemaining = 0;
    uint8_t diagnosticWritesSeen = 0;
    bool syncConfirmed = false;
    bool subscriptionEnabled = false;
    char address[kBleAddressCapacity] = "";
    uint8_t addressType = 0;
  };
  Session* sessionFor(InstanceId);
  const Session* sessionFor(InstanceId) const;
  Session* sessionForAddress(const char*, insta360::RemoteProtocol);
  Session* firstAwaiting(insta360::RemoteProtocol);
  Session* firstPoweringOn();
  Session* sessionForHandle(uint16_t);
  void updateAdvertising();
  Session* sessions_[8] = {};
  Runtime* runtime_ = nullptr;
};

class Insta360MiniDriver : public DeviceDriver {
 public:
  explicit Insta360MiniDriver(Insta360Driver& shared) : shared_(shared) {}
  DriverId driverId() const override { return DriverId::Insta360Mini; }
  bool activate(const DeviceRecord& record) override {
    return shared_.activate(record);
  }
  bool retainWhileDisconnected(InstanceId id) const override {
    return shared_.retainWhileDisconnected(id);
  }
  void deactivate(InstanceId id) override { shared_.deactivate(id); }
  void loop() override {}
  CommandStatus dispatch(const DeviceCommand& command) override {
    return shared_.dispatch(command);
  }
  DeviceRuntimeState runtimeState(InstanceId id) const override {
    return shared_.runtimeState(id);
  }
  const void* specializedState(InstanceId id) const override {
    return shared_.specializedState(id);
  }
  void forgetPairing(const DeviceRecord& record) override {
    shared_.forgetPairing(record);
  }
  bool cancelOnboarding(const DeviceRecord& record) override {
    return shared_.cancelOnboarding(record);
  }
  bool consumePairingUpdate(InstanceId id, DeviceRecord& record) override {
    return shared_.consumePairingUpdate(id, record);
  }

 private:
  Insta360Driver& shared_;
};
}  // namespace studio
