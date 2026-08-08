#pragma once

#include "core/ble/ble_backend.h"

namespace studio::ble {

class BleCentralDelegate {
 public:
  virtual ~BleCentralDelegate() = default;
  // Both callbacks run only from BleCentral::loop().
  virtual void onBleAdvertisement(LinkHandle link,
                                  const Advertisement& advertisement) = 0;
  virtual void onBleEvent(LinkHandle link, const Event& event) = 0;
};

class BleCentral {
 public:
  explicit BleCentral(IBleCentralBackend& backend);

  LinkHandle acquire(BleCentralDelegate& delegate, const ConnectPolicy& policy);
  void release(LinkHandle link);
  void loop(uint32_t nowMs);

  bool requestScan(LinkHandle link, bool clearTarget = false);
  bool selectAdvertisement(LinkHandle link,
                           const Advertisement& advertisement);
  bool requestConnect(LinkHandle link, const Address& address);
  void disconnect(LinkHandle link, bool reconnect = false,
                  uint32_t retryDelayMs = 1500);
  bool requestSecurity(LinkHandle link);
  bool deleteBond(const Address& address);
  void markProtocolReady(LinkHandle link);
  void markProtocolFailed(LinkHandle link, bool reconnect = true);
  bool protocolReady(LinkHandle link) const;
  uint32_t timingStartedAt(LinkHandle link) const;

  void addSkipAddress(LinkHandle link, const Address& address);
  void clearSkipAddresses(LinkHandle link);
  bool isAddressAvailable(LinkHandle link, const Address& address) const;

  LinkPhase phase(LinkHandle link) const;
  bool active(LinkHandle link) const;
  bool scanning(LinkHandle link) const;
  void* nativeClient(LinkHandle link);
  size_t activeCount() const;
  uint32_t droppedEvents() const { return backend_.droppedEvents(); }

 private:
  struct Slot {
    BleCentralDelegate* delegate = nullptr;
    ConnectPolicy policy;
    LinkPhase phase = LinkPhase::Idle;
    Address target;
    Address skips[CONFIG_BLE_MAX_SKIP_ADDRESSES] = {};
    uint8_t skipCount = 0;
    uint8_t connectFailures = 0;
    uint32_t retryAtMs = 0;
    uint32_t connectStartedMs = 0;
    bool scanRequested = false;
    bool targetKnown = false;
    bool reconnectRequested = false;
    bool manualDisconnectPending = false;
    bool connectQueued = false;
    bool securityPending = false;
    bool protocolReady = false;
    bool timingActive = false;
    uint32_t timingStartedMs = 0;
    uint32_t stageStartedMs = 0;
  };

  static size_t indexFor(LinkHandle link);
  Slot* slotFor(LinkHandle link);
  const Slot* slotFor(LinkHandle link) const;
  LinkHandle handleFor(size_t index) const;
  void updateScan();
  void handleEvent(const Event& event, uint32_t nowMs);
  void scheduleRetry(Slot& slot, uint32_t nowMs, uint32_t delayMs = 0);
  void runRetry(LinkHandle link, Slot& slot, uint32_t nowMs);
  bool beginConnect(LinkHandle link, Slot& slot);
  bool startConnectNow(LinkHandle link, Slot& slot);
  void startNextQueuedConnect();
  bool controllerProcedureBusy() const;
  void clearClaim(LinkHandle link);

  IBleCentralBackend& backend_;
  Slot slots_[CONFIG_MAX_ACTIVE_LINKS] = {};
  Address claims_[CONFIG_MAX_ACTIVE_LINKS] = {};
  LinkHandle claimOwners_[CONFIG_MAX_ACTIVE_LINKS] = {};
  bool begun_ = false;
  bool scanRunning_ = false;
  uint32_t scanBurstEndsMs_ = 0;
  uint32_t scanResumeAtMs_ = 0;
  uint32_t nowMs_ = 0;
};

}  // namespace studio::ble
