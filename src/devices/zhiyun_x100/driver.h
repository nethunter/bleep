#pragma once

#include "core/device_driver.h"
#include "devices/zhiyun_x100/client.h"

namespace studio {

class ZhiyunLightDriver : public DeviceDriver {
 public:
  DriverId driverId() const override { return DriverId::ZhiyunLight; }
  bool activate(const DeviceRecord& record) override;
  void deactivate(InstanceId instanceId) override;
  void loop() override;
  CommandStatus dispatch(const DeviceCommand& command) override;
  DeviceRuntimeState runtimeState(InstanceId instanceId) const override;
  const void* specializedState(InstanceId instanceId) const override;
  void cancelOnboarding(const DeviceRecord& record) override;
  void preferSkipPeer(InstanceId instanceId,
                      const char* bleAddress) override;
  bool consumePairingUpdate(InstanceId instanceId,
                            DeviceRecord& record) override;

 private:
  static constexpr size_t kMaxSessions = 4;
  struct Session {
    InstanceId instanceId = kInvalidInstanceId;
    zhiyun_x100::X100Client client;
  };

  Session* find(InstanceId instanceId);
  const Session* find(InstanceId instanceId) const;
  Session sessions_[kMaxSessions];
};

using ZhiyunX100Driver = ZhiyunLightDriver;

}  // namespace studio
