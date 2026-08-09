#pragma once

#include "core/device_driver.h"

namespace studio {

class ActionCameraResearchDriver : public DeviceDriver {
 public:
  explicit ActionCameraResearchDriver(DriverId id) : id_(id) {}
  DriverId driverId() const override { return id_; }
  BleSlotKey bleSlotKey(const DeviceRecord&) const override { return {}; }
  bool activate(const DeviceRecord&) override { return true; }
  void deactivate(InstanceId) override {}
  void loop() override {}
  CommandStatus dispatch(const DeviceCommand&) override {
    return CommandStatus::Unsupported;
  }
  DeviceRuntimeState runtimeState(InstanceId) const override { return {}; }
  const void* specializedState(InstanceId) const override { return nullptr; }
  bool consumePairingUpdate(InstanceId, DeviceRecord&) override { return false; }

 private:
  DriverId id_;
};

}  // namespace studio
