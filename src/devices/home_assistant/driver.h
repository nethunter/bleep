#pragma once

#include "core/device_driver.h"
#include "devices/home_assistant/client.h"

namespace studio {

class HomeAssistantDriver : public DeviceDriver {
 public:
  DriverId driverId() const override { return DriverId::HomeAssistant; }
  BleSlotKey bleSlotKey(const DeviceRecord&) const override { return {}; }
  bool activate(const DeviceRecord& record) override;
  void deactivate(InstanceId instanceId) override;
  void loop() override;
  CommandStatus dispatch(const DeviceCommand& command) override;
  DeviceRuntimeState runtimeState(InstanceId instanceId) const override;
  const void* specializedState(InstanceId instanceId) const override;
  bool consumePairingUpdate(InstanceId, DeviceRecord&) override { return false; }

 private:
  home_assistant::HomeAssistantClient* client_ = nullptr;
  size_t activeCount_ = 0;
};

}  // namespace studio
