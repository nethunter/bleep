#pragma once

#include "core/device_driver.h"
#include "devices/zhiyun_x100/client.h"

namespace studio {

class ZhiyunX100Driver : public DeviceDriver {
 public:
  DriverId driverId() const override { return DriverId::ZhiyunX100; }
  bool activate(const DeviceRecord& record) override;
  void deactivate(InstanceId instanceId) override;
  void loop() override;
  CommandStatus dispatch(const DeviceCommand& command) override;
  DeviceRuntimeState runtimeState(InstanceId instanceId) const override;
  const void* specializedState(InstanceId instanceId) const override;
  void cancelOnboarding(const DeviceRecord& record) override;
  bool consumePairingUpdate(InstanceId instanceId,
                            DeviceRecord& record) override;

 private:
  InstanceId instanceId_ = kInvalidInstanceId;
  zhiyun_x100::X100Client client_;
};

}  // namespace studio
