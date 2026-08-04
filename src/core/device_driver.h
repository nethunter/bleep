#pragma once

#include "core/device_types.h"

namespace studio {

class DeviceDriver {
 public:
  virtual ~DeviceDriver() = default;

  virtual DriverId driverId() const = 0;
  virtual void activate(const DeviceRecord& record) = 0;
  virtual void deactivate() = 0;
  virtual void loop() = 0;
  virtual CommandStatus dispatch(const DeviceCommand& command) = 0;
  virtual DeviceRuntimeState runtimeState() const = 0;
  virtual const void* specializedState() const = 0;

  // Explicit user action; transports may remove controller-side bond data.
  virtual void forgetPairing(const DeviceRecord&) {}

  // Hint used while pairing a new instance: skip peers already claimed by
  // another saved record so a second body can be discovered.
  virtual void preferSkipPeer(const char* /*bleAddress*/) {}

  // Returns true when pairing identity changed and should be persisted.
  virtual bool consumePairingUpdate(DeviceRecord& record) = 0;
};

}  // namespace studio

