#pragma once

#include "core/device_types.h"

namespace studio {

class DeviceDriver {
 public:
  virtual ~DeviceDriver() = default;

  virtual DriverId driverId() const = 0;
  // Physical BLE capacity is charged by transport group rather than by every
  // logical instance. The default is one slot per instance. Drivers whose
  // instances share one actual connection return the same non-empty key;
  // non-BLE runtimes return an empty key.
  virtual BleSlotKey bleSlotKey(const DeviceRecord& record) const {
    return {record.driverId, record.instanceId};
  }
  virtual bool activate(const DeviceRecord& record) = 0;
  // Called when an already-active retained instance gains another owner.
  // Drivers may resume device-specific work without rebuilding the session.
  virtual bool resume(const DeviceRecord&) { return true; }
  // Intentional offline states (for example Canon PoweredOff) may remain in
  // the retained pool. Other ownerless retained sessions are parked after an
  // unexpected disconnect instead of spending power reconnecting forever.
  virtual bool retainWhileDisconnected(InstanceId) const { return false; }
  virtual void deactivate(InstanceId instanceId) = 0;
  virtual void loop() = 0;
  virtual CommandStatus dispatch(const DeviceCommand& command) = 0;
  // Supersedes an asynchronous command before a safety/recovery action such
  // as generated Stop is dispatched. Late replies must be ignored.
  virtual void cancelPendingCommand(InstanceId) {}
  virtual DeviceRuntimeState runtimeState(InstanceId instanceId) const = 0;
  virtual const void* specializedState(InstanceId instanceId) const = 0;
  virtual bool lightControlState(InstanceId, LightControlState&) const {
    return false;
  }

  // Explicit user action; transports may remove controller-side bond data.
  virtual void forgetPairing(const DeviceRecord&) {}

  // Abandons an uncommitted Add-device attempt. Drivers should clear any
  // selected peer, controller bond, or provisional device-local data.
  virtual void cancelOnboarding(const DeviceRecord& record) {
    forgetPairing(record);
  }

  // Hint used while pairing a new instance: skip peers already claimed by
  // another saved record so a second body can be discovered.
  virtual void preferSkipPeer(InstanceId /*instanceId*/,
                              const char* /*bleAddress*/) {}

  // New-device scans expose compatible peers without claiming a BLE link.
  // The opaque token remains stable while that address stays in the bounded
  // candidate set, even when other entries are updated or replaced.
  virtual size_t onboardingCandidateCount(InstanceId) const { return 0; }
  virtual bool onboardingCandidate(InstanceId, size_t,
                                   OnboardingCandidate&) const {
    return false;
  }
  virtual bool selectOnboardingCandidate(InstanceId, uint32_t) {
    return false;
  }

  // Returns true when pairing identity changed and should be persisted.
  virtual bool consumePairingUpdate(InstanceId instanceId,
                                    DeviceRecord& record) = 0;
};

}  // namespace studio
