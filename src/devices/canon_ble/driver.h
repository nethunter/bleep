#pragma once

#include "core/device_driver.h"
#include "devices/canon_ble/client.h"

namespace studio {

class CanonBleDriver : public DeviceDriver {
 public:
  DriverId driverId() const override { return DriverId::CanonBle; }
  void activate(const DeviceRecord& record) override;
  void deactivate() override;
  void loop() override;
  CommandStatus dispatch(const DeviceCommand& command) override;
  DeviceRuntimeState runtimeState() const override;
  const void* specializedState() const override { return &client_.state(); }
  void forgetPairing(const DeviceRecord& record) override;
  void preferSkipPeer(const char* bleAddress) override;
  bool consumePairingUpdate(DeviceRecord& record) override;

 private:
  canon_ble::CanonBleClient client_;
  InstanceId activeInstance_ = kInvalidInstanceId;
  bool active_ = false;
};

}  // namespace studio
