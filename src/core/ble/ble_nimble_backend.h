#pragma once

#include "core/ble/ble_backend.h"

namespace studio::ble {

class BleNimbleBackend final : public IBleCentralBackend {
 public:
  BleNimbleBackend();
  ~BleNimbleBackend() override;

  bool begin() override;
  void shutdown() override;
  void pump() override;
  bool startScan() override;
  void stopScan() override;
  bool scanRunning() const override;
  bool createLink(LinkHandle link, uint16_t connectTimeoutMs) override;
  void destroyLink(LinkHandle link) override;
  bool connect(LinkHandle link, const Address& address,
               SecurityPolicy security) override;
  void disconnect(LinkHandle link) override;
  bool secure(LinkHandle link, SecurityPolicy security) override;
  bool updateConnectionParameters(
      LinkHandle link, const ConnectionParameters& parameters) override;
  bool deleteBond(const Address& address) override;
  void* nativeClient(LinkHandle link) override;
  bool popEvent(Event& event) override;
  uint32_t droppedEvents() const override;

 private:
  struct Impl;
  Impl* impl_;
};

}  // namespace studio::ble
