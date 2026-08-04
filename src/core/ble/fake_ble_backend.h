#pragma once

#include "core/ble/ble_backend.h"

namespace studio::ble {

class FakeBleBackend : public IBleCentralBackend {
 public:
  bool begin() override;
  void shutdown() override;
  void pump() override {}
  bool startScan() override;
  void stopScan() override;
  bool scanRunning() const override { return scanning_; }
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
  uint32_t droppedEvents() const override { return droppedEvents_; }

  bool emit(const Event& event);
  bool emitAdvertisement(const Advertisement& advertisement);
  void setBeginResult(bool value) { beginResult_ = value; }
  void setConnectResult(bool value) { connectResult_ = value; }
  void setSecureResult(bool value) { secureResult_ = value; }
  void setParameterUpdateResult(bool value) { parameterUpdateResult_ = value; }

  bool initialized() const { return initialized_; }
  uint32_t beginCalls() const { return beginCalls_; }
  uint32_t shutdownCalls() const { return shutdownCalls_; }
  uint32_t scanStarts() const { return scanStarts_; }
  uint32_t scanStops() const { return scanStops_; }
  uint32_t connectCalls(LinkHandle link) const;
  uint32_t disconnectCalls(LinkHandle link) const;
  uint32_t secureCalls(LinkHandle link) const;
  uint32_t parameterUpdateCalls(LinkHandle link) const;
  const ConnectionParameters& lastParameters(LinkHandle link) const;
  const Address& lastConnectAddress(LinkHandle link) const;
  uint32_t bondDeleteCalls() const { return bondDeleteCalls_; }

 private:
  struct FakeSlot {
    bool created = false;
    uint16_t timeoutMs = 0;
    uint32_t connectCalls = 0;
    uint32_t disconnectCalls = 0;
    uint32_t secureCalls = 0;
    uint32_t parameterUpdateCalls = 0;
    Address lastAddress;
    SecurityPolicy security = SecurityPolicy::None;
    ConnectionParameters lastParameters;
  };

  Event events_[CONFIG_BLE_EVENT_QUEUE_SIZE] = {};
  FakeSlot slots_[CONFIG_MAX_ACTIVE_LINKS] = {};
  size_t eventRead_ = 0;
  size_t eventWrite_ = 0;
  size_t eventCount_ = 0;
  uint32_t droppedEvents_ = 0;
  uint32_t beginCalls_ = 0;
  uint32_t shutdownCalls_ = 0;
  uint32_t scanStarts_ = 0;
  uint32_t scanStops_ = 0;
  uint32_t bondDeleteCalls_ = 0;
  bool beginResult_ = true;
  bool connectResult_ = true;
  bool secureResult_ = true;
  bool parameterUpdateResult_ = true;
  bool initialized_ = false;
  bool scanning_ = false;
};

}  // namespace studio::ble
