#pragma once

#include "core/device_driver.h"
#include "devices/zhiyun_x100/client.h"

namespace studio {

class ZhiyunLightDriver : public DeviceDriver {
 public:
  DriverId driverId() const override { return DriverId::ZhiyunLight; }
  BleSlotKey bleSlotKey(const DeviceRecord&) const override {
    return {DriverId::PanelOwnedMesh, 1};
  }
  InstanceProfile instanceProfile(
      const DeviceRecord& record,
      const InstanceProfile& catalogProfile) const override;
  bool activate(const DeviceRecord& record) override;
  void deactivate(InstanceId instanceId) override;
  void loop() override;
  CommandStatus dispatch(const DeviceCommand& command) override;
  void cancelPendingCommand(InstanceId instanceId) override;
  DeviceRuntimeState runtimeState(InstanceId instanceId) const override;
  const void* specializedState(InstanceId instanceId) const override;
  bool lightControlState(InstanceId instanceId,
                         LightControlState& state) const override;
  bool cancelOnboarding(const DeviceRecord& record) override;
  size_t onboardingCandidateCount(InstanceId instanceId) const override;
  bool onboardingCandidate(InstanceId instanceId, size_t index,
                           OnboardingCandidate& candidate) const override;
  bool selectOnboardingCandidate(InstanceId instanceId,
                                 uint32_t token) override;
  void preferSkipPeer(InstanceId instanceId,
                      const char* bleAddress) override;
  bool consumePairingUpdate(InstanceId instanceId,
                            DeviceRecord& record) override;

 private:
  static constexpr size_t kMaxSessions = 4;
  struct Session {
    InstanceId instanceId = kInvalidInstanceId;
    DeviceRecord record;
    zhiyun_x100::X100Client client;
    bool sharedGateway = false;
    bool gatewayAttached = false;
    uint32_t gatewayGeneration = 0xffffffffu;
    uint32_t gatewayAttachRetryAt = 0;
    enum class CompoundStage : uint8_t { None, Look, Power };
    CompoundStage compoundStage = CompoundStage::None;
    bool compoundFailed = false;
  };

  Session* find(InstanceId instanceId);
  const Session* find(InstanceId instanceId) const;
  Session* sessions_[kMaxSessions] = {};
  size_t sessionCount_ = 0;
  bool repositoryHeld_ = false;
};

using ZhiyunX100Driver = ZhiyunLightDriver;

}  // namespace studio
