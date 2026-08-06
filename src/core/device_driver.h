#pragma once

#include "core/device_types.h"

namespace studio {

class DeviceDriver {
 public:
  virtual ~DeviceDriver() = default;

  virtual DriverId driverId() const = 0;
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
  virtual DeviceRuntimeState runtimeState(InstanceId instanceId) const = 0;
  virtual const void* specializedState(InstanceId instanceId) const = 0;

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

  // Returns true when pairing identity changed and should be persisted.
  virtual bool consumePairingUpdate(InstanceId instanceId,
                                    DeviceRecord& record) = 0;
};

}  // namespace studio
