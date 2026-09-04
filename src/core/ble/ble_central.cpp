#include "core/ble/ble_central.h"

#include <cstring>

#include "core/ble/ble_timing.h"

namespace studio::ble {

namespace {
constexpr uint32_t kScanBurstMs = 4000;
constexpr uint32_t kScanPauseMs = 1500;

bool deadlineReached(uint32_t nowMs, uint32_t deadlineMs) {
  return static_cast<int32_t>(nowMs - deadlineMs) >= 0;
}
}  // namespace

BleCentral::BleCentral(IBleCentralBackend& backend) : backend_(backend) {}

size_t BleCentral::indexFor(LinkHandle link) {
  return link == kInvalidLinkHandle ? CONFIG_MAX_ACTIVE_LINKS
                                    : static_cast<size_t>(link - 1);
}

BleCentral::Slot* BleCentral::slotFor(LinkHandle link) {
  const size_t index = indexFor(link);
  return index < CONFIG_MAX_ACTIVE_LINKS && slots_[index].delegate != nullptr
             ? &slots_[index]
             : nullptr;
}

const BleCentral::Slot* BleCentral::slotFor(LinkHandle link) const {
  const size_t index = indexFor(link);
  return index < CONFIG_MAX_ACTIVE_LINKS && slots_[index].delegate != nullptr
             ? &slots_[index]
             : nullptr;
}

LinkHandle BleCentral::handleFor(size_t index) const {
  return static_cast<LinkHandle>(index + 1);
}

LinkHandle BleCentral::acquire(BleCentralDelegate& delegate,
                               const ConnectPolicy& policy) {
  for (size_t i = 0; i < CONFIG_MAX_ACTIVE_LINKS; ++i) {
    if (slots_[i].delegate == &delegate) {
      return handleFor(i);
    }
  }
  if (!begun_) {
    if (!backend_.begin()) {
      return kInvalidLinkHandle;
    }
    begun_ = true;
  }
  for (size_t i = 0; i < CONFIG_MAX_ACTIVE_LINKS; ++i) {
    if (slots_[i].delegate != nullptr) {
      continue;
    }
    const LinkHandle handle = handleFor(i);
    if (!backend_.createLink(handle, policy.connectTimeoutMs)) {
      if (activeCount() == 0) {
        backend_.shutdown();
        begun_ = false;
      }
      return kInvalidLinkHandle;
    }
    slots_[i] = {};
    slots_[i].delegate = &delegate;
    slots_[i].policy = policy;
    return handle;
  }
  if (activeCount() == 0) {
    backend_.shutdown();
    begun_ = false;
  }
  return kInvalidLinkHandle;
}

void BleCentral::release(LinkHandle link) {
  Slot* slot = slotFor(link);
  if (slot == nullptr) {
    return;
  }
  slot->reconnectRequested = false;
  slot->scanRequested = false;
  if (slot->timingActive) {
    logTiming(slot->policy.diagnosticTag, link, "teardown", 0,
              nowMs_ - slot->timingStartedMs, "ok");
  }
  backend_.disconnect(link);
  backend_.destroyLink(link);
  clearClaim(link);
  *slot = {};
  updateScan();
  if (activeCount() == 0 && begun_) {
    backend_.shutdown();
    begun_ = false;
    scanRunning_ = false;
    scanBurstEndsMs_ = 0;
    scanResumeAtMs_ = 0;
  }
}

void BleCentral::loop(uint32_t nowMs) {
  nowMs_ = nowMs;
  for (size_t i = 0; i < CONFIG_MAX_ACTIVE_LINKS; ++i) {
    Slot& slot = slots_[i];
    if (slot.delegate == nullptr || slot.setupParametersRetryAtMs == 0 ||
        static_cast<int32_t>(nowMs - slot.setupParametersRetryAtMs) < 0) {
      continue;
    }
    slot.setupParametersRetryAtMs = 0;
    if (slot.phase == LinkPhase::Connected && !slot.protocolReady) {
      const bool tuned = backend_.updateConnectionParameters(
          handleFor(i), slot.policy.setupParameters);
      logTiming(slot.policy.diagnosticTag, handleFor(i),
                "setup_parameters_retry", slot.setupParametersRetries,
                nowMs - slot.timingStartedMs, tuned ? "ok" : "fallback");
      // A drowsy peer can keep its own update in flight for seconds; back off
      // 400 ms, 1 s, 2 s before giving up on the fast setup interval.
      if (!tuned && ++slot.setupParametersRetries < 3) {
        slot.setupParametersRetryAtMs =
            nowMs + (slot.setupParametersRetries == 1 ? 1000u : 2000u);
      }
    }
  }
  // The backend may still be completing asynchronous client destruction after
  // the last logical link was released. Keep pumping it so teardown can finish
  // and an immediate reacquire does not race stale NimBLE clients.
  backend_.pump();
  if (!begun_) {
    return;
  }
  Event event;
  while (backend_.popEvent(event)) {
    handleEvent(event, nowMs);
  }
  for (size_t i = 0; i < CONFIG_MAX_ACTIVE_LINKS; ++i) {
    Slot& slot = slots_[i];
    if (slot.delegate == nullptr) {
      continue;
    }
    if (slot.phase == LinkPhase::Connecting &&
        static_cast<int32_t>(nowMs - slot.connectStartedMs) >=
            slot.policy.connectWatchdogMs) {
      slot.manualDisconnectPending = true;
      backend_.disconnect(handleFor(i));
      ++slot.connectFailures;
      scheduleRetry(slot, nowMs);
      continue;
    }
    if (slot.phase == LinkPhase::WaitingRetry &&
        static_cast<int32_t>(nowMs - slot.retryAtMs) >= 0) {
      runRetry(handleFor(i), slot, nowMs);
    }
  }
  startNextQueuedConnect();
  updateScan();
}

bool BleCentral::requestScan(LinkHandle link, bool clearTarget) {
  Slot* slot = slotFor(link);
  if (slot == nullptr) {
    return false;
  }
  if (clearTarget) {
    slot->targetKnown = false;
    slot->connectFailures = 0;
    clearClaim(link);
  }
  if (!slot->timingActive) {
    slot->timingActive = true;
    slot->timingStartedMs = nowMs_;
  }
  slot->stageStartedMs = nowMs_;
  slot->protocolReady = false;
  slot->scanRequested = true;
  slot->connectQueued = false;
  slot->reconnectRequested = true;
  slot->phase = LinkPhase::Scanning;
  logTiming(slot->policy.diagnosticTag, link, "scan_requested", 0,
            nowMs_ - slot->timingStartedMs, "ok");
  updateScan();
  return true;
}

void BleCentral::pauseScanForGattMutation() {
  if (!begun_ || !scanRunning_) {
    return;
  }
  backend_.stopScan();
  scanRunning_ = false;
  scanBurstEndsMs_ = 0;
  scanResumeAtMs_ = 0;
}

bool BleCentral::selectAdvertisement(
    LinkHandle link, const Advertisement& advertisement) {
  Slot* slot = slotFor(link);
  if (slot == nullptr ||
      !isAddressAvailable(link, advertisement.address)) {
    return false;
  }
  slot->target = advertisement.address;
  slot->targetKnown = true;
  slot->scanRequested = false;
  claims_[indexFor(link)] = advertisement.address;
  claimOwners_[indexFor(link)] = link;
  updateScan();
  return beginConnect(link, *slot);
}

bool BleCentral::requestConnect(LinkHandle link, const Address& address) {
  Slot* slot = slotFor(link);
  if (slot == nullptr || !isAddressAvailable(link, address)) {
    return false;
  }
  if (!slot->timingActive) {
    slot->timingActive = true;
    slot->timingStartedMs = nowMs_;
  }
  slot->protocolReady = false;
  slot->target = address;
  slot->targetKnown = true;
  slot->scanRequested = false;
  slot->reconnectRequested = true;
  claims_[indexFor(link)] = address;
  claimOwners_[indexFor(link)] = link;
  updateScan();
  return beginConnect(link, *slot);
}

void BleCentral::disconnect(LinkHandle link, bool reconnect,
                            uint32_t retryDelayMs) {
  Slot* slot = slotFor(link);
  if (slot == nullptr) {
    return;
  }
  slot->reconnectRequested = reconnect;
  slot->protocolReady = false;
  slot->connectQueued = false;
  slot->securityPending = false;
  slot->manualDisconnectPending = true;
  slot->scanRequested = false;
  slot->phase = LinkPhase::Disconnecting;
  backend_.disconnect(link);
  if (reconnect) {
    scheduleRetry(*slot, nowMs_, retryDelayMs);
  }
  updateScan();
}

bool BleCentral::requestSecurity(LinkHandle link) {
  Slot* slot = slotFor(link);
  if (slot == nullptr) {
    return false;
  }
  slot->stageStartedMs = nowMs_;
  const bool requested = backend_.secure(link, slot->policy.security);
  slot->securityPending = requested;
  logTiming(slot->policy.diagnosticTag, link, "security_requested", 0,
            nowMs_ - slot->timingStartedMs, requested ? "ok" : "failed");
  return requested;
}

bool BleCentral::deleteBond(const Address& address) {
  if (!begun_ && !backend_.begin()) {
    return false;
  }
  const bool temporaryBegin = !begun_;
  begun_ = true;
  const bool deleted = backend_.deleteBond(address);
  if (temporaryBegin && activeCount() == 0) {
    backend_.shutdown();
    begun_ = false;
  }
  return deleted;
}

void BleCentral::markProtocolReady(LinkHandle link) {
  Slot* slot = slotFor(link);
  if (slot != nullptr) {
    slot->connectFailures = 0;
    slot->reconnectRequested = true;
    slot->protocolReady = true;
    const bool tuned = !slot->policy.readyParameters.configured() ||
                       backend_.updateConnectionParameters(
                           link, slot->policy.readyParameters);
    logTiming(slot->policy.diagnosticTag, link, "protocol_ready",
              nowMs_ - slot->stageStartedMs,
              nowMs_ - slot->timingStartedMs, tuned ? "ok" : "param_fallback");
    slot->stageStartedMs = nowMs_;
  }
}

void BleCentral::markProtocolFailed(LinkHandle link, bool reconnect) {
  Slot* slot = slotFor(link);
  if (slot == nullptr) {
    return;
  }
  logTiming(slot->policy.diagnosticTag, link, "protocol_failed",
            nowMs_ - slot->stageStartedMs,
            nowMs_ - slot->timingStartedMs, "failed");
  slot->protocolReady = false;
  slot->reconnectRequested = reconnect;
  slot->manualDisconnectPending = !reconnect;
  backend_.disconnect(link);
}

bool BleCentral::protocolReady(LinkHandle link) const {
  const Slot* slot = slotFor(link);
  return slot != nullptr && slot->protocolReady;
}

uint32_t BleCentral::timingStartedAt(LinkHandle link) const {
  const Slot* slot = slotFor(link);
  return slot != nullptr && slot->timingActive ? slot->timingStartedMs : 0;
}

void BleCentral::addSkipAddress(LinkHandle link, const Address& address) {
  Slot* slot = slotFor(link);
  if (slot == nullptr || slot->skipCount >= CONFIG_BLE_MAX_SKIP_ADDRESSES) {
    return;
  }
  for (uint8_t i = 0; i < slot->skipCount; ++i) {
    if (addressEqual(slot->skips[i], address)) {
      return;
    }
  }
  slot->skips[slot->skipCount++] = address;
}

void BleCentral::clearSkipAddresses(LinkHandle link) {
  Slot* slot = slotFor(link);
  if (slot != nullptr) {
    slot->skipCount = 0;
  }
}

bool BleCentral::isAddressAvailable(LinkHandle link,
                                    const Address& address) const {
  const Slot* slot = slotFor(link);
  if (slot == nullptr) {
    return false;
  }
  for (uint8_t i = 0; i < slot->skipCount; ++i) {
    if (addressEqual(slot->skips[i], address)) {
      return false;
    }
  }
  for (size_t i = 0; i < CONFIG_MAX_ACTIVE_LINKS; ++i) {
    if (claimOwners_[i] != kInvalidLinkHandle && claimOwners_[i] != link &&
        addressEqual(claims_[i], address)) {
      return false;
    }
  }
  return true;
}

LinkPhase BleCentral::phase(LinkHandle link) const {
  const Slot* slot = slotFor(link);
  return slot == nullptr ? LinkPhase::Idle : slot->phase;
}

bool BleCentral::active(LinkHandle link) const {
  return slotFor(link) != nullptr;
}

bool BleCentral::scanning(LinkHandle link) const {
  const Slot* slot = slotFor(link);
  return slot != nullptr && slot->scanRequested;
}

void* BleCentral::nativeClient(LinkHandle link) {
  return slotFor(link) == nullptr ? nullptr : backend_.nativeClient(link);
}

size_t BleCentral::activeCount() const {
  size_t count = 0;
  for (const Slot& slot : slots_) {
    if (slot.delegate != nullptr) {
      ++count;
    }
  }
  return count;
}

void BleCentral::updateScan() {
  if (!begun_) {
    return;
  }
  bool wanted = false;
  for (const Slot& slot : slots_) {
    wanted = wanted || (slot.delegate != nullptr && slot.scanRequested);
  }
  if (!wanted) {
    if (scanRunning_) {
      backend_.stopScan();
    }
    scanRunning_ = false;
    scanBurstEndsMs_ = 0;
    scanResumeAtMs_ = 0;
    return;
  }

  // The ESP32-C3 can time-slice scanning around established links, but active
  // discovery competes with initiating and securing a new connection. Keep
  // scan demand intact and resume it as soon as the controller procedure ends.
  if (controllerProcedureBusy()) {
    if (scanRunning_) {
      backend_.stopScan();
    }
    scanRunning_ = false;
    scanBurstEndsMs_ = 0;
    scanResumeAtMs_ = 0;
    return;
  }

  if (scanRunning_ && !backend_.scanRunning()) {
    scanRunning_ = false;
    scanResumeAtMs_ = nowMs_ + kScanPauseMs;
  }
  if (scanRunning_ && deadlineReached(nowMs_, scanBurstEndsMs_)) {
    backend_.stopScan();
    scanRunning_ = false;
    scanResumeAtMs_ = nowMs_ + kScanPauseMs;
    return;
  }
  if (!scanRunning_ &&
      (scanResumeAtMs_ == 0 || deadlineReached(nowMs_, scanResumeAtMs_))) {
    scanRunning_ = backend_.startScan();
    if (scanRunning_) {
      scanBurstEndsMs_ = nowMs_ + kScanBurstMs;
      scanResumeAtMs_ = 0;
    }
  }
}

void BleCentral::handleEvent(const Event& event, uint32_t nowMs) {
  if (event.type == EventType::Advertisement) {
    bool recipients[CONFIG_MAX_ACTIVE_LINKS] = {};
    for (size_t i = 0; i < CONFIG_MAX_ACTIVE_LINKS; ++i) {
      Slot& slot = slots_[i];
      recipients[i] =
          slot.delegate != nullptr && slot.scanRequested &&
          isAddressAvailable(handleFor(i), event.advertisement.address);
    }
    for (size_t i = 0; i < CONFIG_MAX_ACTIVE_LINKS; ++i) {
      Slot& slot = slots_[i];
      if (recipients[i]) {
        slot.delegate->onBleAdvertisement(handleFor(i),
                                          event.advertisement);
      }
    }
    return;
  }
  if (event.type == EventType::ScanEnded) {
    scanRunning_ = false;
    scanResumeAtMs_ = nowMs + kScanPauseMs;
    return;
  }

  Slot* slot = slotFor(event.link);
  if (slot == nullptr) {
    return;
  }
  switch (event.type) {
    case EventType::Connected:
      slot->phase = LinkPhase::Connected;
      slot->connectQueued = false;
      slot->protocolReady = false;
      logTiming(slot->policy.diagnosticTag, event.link, "physical_connected",
                nowMs - slot->connectStartedMs,
                nowMs - slot->timingStartedMs, "ok");
      slot->stageStartedMs = nowMs;
      if (slot->policy.setupParameters.configured()) {
        const bool tuned = backend_.updateConnectionParameters(
            event.link, slot->policy.setupParameters);
        logTiming(slot->policy.diagnosticTag, event.link,
                  "setup_parameters", 0,
                  nowMs - slot->timingStartedMs,
                  tuned ? "ok" : "fallback");
        slot->setupParametersRetries = 0;
        slot->setupParametersRetryAtMs = tuned ? 0 : nowMs + 400;
      }
      break;
    case EventType::ConnectFailed:
      slot->connectQueued = false;
      slot->protocolReady = false;
      logTiming(slot->policy.diagnosticTag, event.link, "connect_failed",
                nowMs - slot->connectStartedMs,
                nowMs - slot->timingStartedMs, "failed");
      if (slot->manualDisconnectPending) {
        slot->manualDisconnectPending = false;
        if (slot->phase != LinkPhase::WaitingRetry) {
          slot->phase =
              slot->scanRequested ? LinkPhase::Scanning : LinkPhase::Idle;
        }
      } else if (slot->reconnectRequested) {
        ++slot->connectFailures;
        scheduleRetry(*slot, nowMs);
      } else {
        slot->phase = LinkPhase::Idle;
        clearClaim(event.link);
      }
      break;
    case EventType::Disconnected:
      slot->connectQueued = false;
      slot->securityPending = false;
      slot->protocolReady = false;
      slot->setupParametersRetryAtMs = 0;
      logTiming(slot->policy.diagnosticTag, event.link, "disconnected",
                nowMs - slot->stageStartedMs,
                nowMs - slot->timingStartedMs, "failed");
      if (slot->manualDisconnectPending) {
        slot->manualDisconnectPending = false;
        if (slot->phase != LinkPhase::WaitingRetry) {
          slot->phase =
              slot->scanRequested ? LinkPhase::Scanning : LinkPhase::Idle;
        }
      } else if (slot->reconnectRequested) {
        ++slot->connectFailures;
        scheduleRetry(*slot, nowMs);
      } else {
        slot->phase = LinkPhase::Idle;
        clearClaim(event.link);
      }
      break;
    case EventType::SecurityComplete:
      slot->securityPending = false;
      logTiming(slot->policy.diagnosticTag, event.link, "security_complete",
                nowMs - slot->stageStartedMs,
                nowMs - slot->timingStartedMs,
                event.succeeded ? "ok" : "failed");
      slot->stageStartedMs = nowMs;
      break;
    case EventType::Advertisement:
    case EventType::ScanEnded:
      break;
  }
  slot->delegate->onBleEvent(event.link, event);
}

void BleCentral::scheduleRetry(Slot& slot, uint32_t nowMs,
                               uint32_t delayMs) {
  slot.reconnectRequested = true;
  slot.scanRequested = false;
  slot.phase = LinkPhase::WaitingRetry;
  const uint32_t multiplier =
      slot.connectFailures < 4 ? slot.connectFailures : 4;
  uint32_t delay =
      delayMs != 0 ? delayMs : 1500u * (multiplier ? multiplier : 1);
  if (delayMs == 0 && slot.policy.retryBackoffCapMs != 0 &&
      delay > slot.policy.retryBackoffCapMs) {
    delay = slot.policy.retryBackoffCapMs;
  }
  // The backoff settles a peripheral we are actively poking. When the next
  // retry can only listen for advertisements, scanning is passive: resume it
  // immediately instead of adding dead time after a failed attempt. Mirrors
  // runRetry's direct-versus-scan choice.
  const bool scanFallback =
      !(slot.targetKnown &&
        (slot.policy.alwaysDirect ||
         slot.connectFailures < slot.policy.directAttemptsBeforeScan));
  if (delayMs == 0 && scanFallback) {
    delay = 0;
  }
  slot.retryAtMs = nowMs + delay;
}

void BleCentral::runRetry(LinkHandle link, Slot& slot, uint32_t) {
  if (slot.targetKnown &&
      (slot.policy.alwaysDirect ||
       slot.connectFailures < slot.policy.directAttemptsBeforeScan)) {
    beginConnect(link, slot);
  } else {
    slot.scanRequested = true;
    slot.phase = LinkPhase::Scanning;
  }
}

bool BleCentral::beginConnect(LinkHandle link, Slot& slot) {
  if (!slot.targetKnown) {
    return requestScan(link);
  }
  slot.scanRequested = false;
  if (!slot.timingActive) {
    slot.timingActive = true;
    slot.timingStartedMs = nowMs_;
  }
  slot.protocolReady = false;
  slot.connectQueued = true;
  slot.phase = LinkPhase::WaitingConnect;
  logTiming(slot.policy.diagnosticTag, link, "connect_queued", 0,
            nowMs_ - slot.timingStartedMs, "ok");
  startNextQueuedConnect();
  return true;
}

bool BleCentral::startConnectNow(LinkHandle link, Slot& slot) {
  // Stop discovery before asking NimBLE to initiate. updateScan() keeps it
  // suspended through security, then resumes for every remaining requester.
  if (scanRunning_) {
    backend_.stopScan();
    scanRunning_ = false;
    scanBurstEndsMs_ = 0;
    scanResumeAtMs_ = 0;
  }
  slot.connectQueued = false;
  slot.phase = LinkPhase::Connecting;
  slot.connectStartedMs = nowMs_;
  slot.stageStartedMs = nowMs_;
  logTiming(slot.policy.diagnosticTag, link, "connect_requested", 0,
            nowMs_ - slot.timingStartedMs, "ok");
  if (!backend_.connect(link, slot.target, slot.policy.security)) {
    ++slot.connectFailures;
    scheduleRetry(slot, nowMs_);
    return false;
  }
  return true;
}

void BleCentral::startNextQueuedConnect() {
  if (controllerProcedureBusy()) {
    return;
  }
  for (size_t i = 0; i < CONFIG_MAX_ACTIVE_LINKS; ++i) {
    Slot& slot = slots_[i];
    if (slot.delegate != nullptr && slot.connectQueued) {
      startConnectNow(handleFor(i), slot);
      return;
    }
  }
}

bool BleCentral::controllerProcedureBusy() const {
  for (const Slot& slot : slots_) {
    if (slot.delegate != nullptr &&
        (slot.phase == LinkPhase::Connecting || slot.securityPending)) {
      return true;
    }
  }
  return false;
}

void BleCentral::clearClaim(LinkHandle link) {
  const size_t index = indexFor(link);
  if (index < CONFIG_MAX_ACTIVE_LINKS) {
    claims_[index] = {};
    claimOwners_[index] = kInvalidLinkHandle;
  }
}

}  // namespace studio::ble
