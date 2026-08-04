#pragma once

#include "core/ble/ble_types.h"

namespace studio::ble {

class IBleCentralBackend {
 public:
  virtual ~IBleCentralBackend() = default;

  virtual bool begin() = 0;
  virtual void shutdown() = 0;
  virtual void pump() = 0;

  virtual bool startScan() = 0;
  virtual void stopScan() = 0;
  virtual bool scanRunning() const = 0;

  virtual bool createLink(LinkHandle link, uint16_t connectTimeoutMs) = 0;
  virtual void destroyLink(LinkHandle link) = 0;
  virtual bool connect(LinkHandle link, const Address& address,
                       SecurityPolicy security) = 0;
  virtual void disconnect(LinkHandle link) = 0;
  virtual bool secure(LinkHandle link, SecurityPolicy security) = 0;
  virtual bool updateConnectionParameters(
      LinkHandle link, const ConnectionParameters& parameters) = 0;
  virtual bool deleteBond(const Address& address) = 0;
  virtual void* nativeClient(LinkHandle link) = 0;

  virtual bool popEvent(Event& event) = 0;
  virtual uint32_t droppedEvents() const = 0;
};

}  // namespace studio::ble
