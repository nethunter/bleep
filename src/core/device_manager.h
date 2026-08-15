#pragma once

#include "core/active_instance_pool.h"
#include "core/command_queue.h"
#include "core/config_store.h"
#include "core/device_driver.h"
#include "core/device_configuration.h"
#include "core/driver_catalog.h"

namespace studio {

class DeviceManager {
 public:
  // This bounds driver-adapter pointers, not saved devices or active links.
  // Keep spare entries so enabling a new compile-time family cannot silently
  // make later catalog items impossible to open.
  static constexpr size_t kMaxCompiledDrivers = 16;
  static constexpr size_t kMaxActiveInstances = CONFIG_MAX_ACTIVE_INSTANCES;

  DeviceManager(IConfigBackend& backend, DeviceDriver* const* drivers,
                size_t driverCount);

  bool begin();
  void loop();

  size_t count() const { return configuration_.registry().count(); }
  const DeviceRecord* at(size_t index) const {
    return configuration_.registry().at(index);
  }
  const DeviceRecord* find(InstanceId instanceId) const {
    return isPendingAdd(instanceId) ? &configuration_.pending()
                                    : configuration_.registry().find(instanceId);
  }
  InstanceProfile profile(InstanceId instanceId) const;
  InstanceId foregroundInstance() const;
  size_t activeCount() const { return activePool_.count(); }
  size_t bleSlotCount() const;
  bool isActive(InstanceId instanceId) const;
  bool ownedBy(InstanceId instanceId, ConnectionOwner owner) const;
  bool isRetained(InstanceId instanceId) const;

  RegistryStatus add(DriverId driverId, const char* displayName, InstanceId& outId);
  RegistryStatus beginAdd(DriverId driverId, const char* displayName,
                          InstanceId& outId);
  InstanceId pendingAdd() const { return configuration_.pending().instanceId; }
  bool isPendingAdd(InstanceId instanceId) const {
    return instanceId != kInvalidInstanceId &&
           configuration_.pending().instanceId == instanceId;
  }
  bool pendingAddCommitFailed(InstanceId instanceId) const {
    return isPendingAdd(instanceId) && configuration_.pendingCommitFailed();
  }
  bool retryPendingAdd(InstanceId instanceId);
  size_t onboardingCandidateCount(InstanceId instanceId) const;
  bool onboardingCandidate(InstanceId instanceId, size_t index,
                           OnboardingCandidate& candidate) const;
  bool selectOnboardingCandidate(InstanceId instanceId, uint32_t token);
  RegistryStatus cancelPendingAdd(InstanceId instanceId);
  RegistryStatus remove(InstanceId instanceId);
  RegistryStatus update(InstanceId instanceId, const char* displayName,
                        bool enabled);
  RegistryStatus rename(InstanceId instanceId, const char* displayName);
  RegistryStatus setEnabled(InstanceId instanceId, bool enabled);
  RegistryStatus clearPairing(InstanceId instanceId);
  RegistryStatus addHomeAssistantEntity(HomeAssistantDomain domain,
                                        const char* entityId,
                                        const char* displayName,
                                        InstanceId& outId);
  RegistryStatus rebindHomeAssistantEntity(InstanceId instanceId,
                                           HomeAssistantDomain domain,
                                           const char* entityId);
  RegistryStatus replaceHomeAssistantEntities(
      const HomeAssistantEntitySelection* selections, size_t count);

  bool acquire(InstanceId instanceId, ConnectionOwner owner);
  void release(InstanceId instanceId, ConnectionOwner owner);
  CommandStatus disconnectIdle(InstanceId instanceId);
  CommandStatus disconnect(InstanceId instanceId, bool confirmed = false);
  void deactivateAll();
  bool enqueue(DeviceCommand command, uint32_t* assignedRequestId = nullptr);
  bool cancelCommand(uint32_t requestId, InstanceId instanceId);
  bool takeResult(uint32_t requestId, CommandResult& result);
  bool popResult(CommandResult& result) { return results_.pop(result); }

  DeviceRuntimeState runtimeState(InstanceId instanceId) const;
  bool lightControlState(InstanceId instanceId, LightControlState& state) const;
  const void* specializedState(InstanceId instanceId) const;

 private:
  DeviceDriver* driverFor(DriverId driverId) const;
  using ActiveSlot = ActiveInstancePool::Slot;

  ActiveSlot* slotFor(InstanceId instanceId);
  const ActiveSlot* slotFor(InstanceId instanceId) const;
  bool addActive(InstanceId instanceId, ConnectionOwner owner);
  void removeActive(InstanceId instanceId);
  bool ensureBleSlotAvailable(const DeviceRecord& record,
                              const DeviceDriver& driver);
  bool evictOldestIdleInstance();
  bool evictOldestIdleBleGroup();
  BleSlotKey bleSlotKey(InstanceId instanceId) const;
  void deactivate(InstanceId instanceId);
  void touch(ActiveSlot& slot);
  static uint8_t ownerBit(ConnectionOwner owner);
  void applySkipPeers(DeviceDriver& driver, const DeviceRecord& record);
  DeviceRecord* mutableRecord(InstanceId instanceId);
  bool commitPendingAdd();
  bool seedInitialRegistry();
  bool save();
  CommandStatus dispatch(const DeviceCommand& command);

  DeviceDriver* drivers_[kMaxCompiledDrivers] = {};
  size_t driverCount_ = 0;
  DeviceConfiguration configuration_;
  DeviceCommandQueue commands_;
  DeviceResultQueue results_;
  ActiveInstancePool activePool_;
  uint32_t nextRequestId_ = 1;
  bool begun_ = false;
};

DeviceManager& devices();

}  // namespace studio
