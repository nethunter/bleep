#include "core/firmware_update_policy.h"

namespace studio {

namespace {

bool reached(uint32_t now, uint32_t deadline) {
  return static_cast<int32_t>(now - deadline) >= 0;
}

}  // namespace

void FirmwareUpdatePolicy::begin(uint32_t nowMs) {
  lastActivityMs_ = nowMs;
  nextCheckMs_ = nowMs + kIdleDelayMs;
  startupDeadlineMs_ = nowMs + kStartupDelayMs;
  failureCount_ = 0;
  immediateRequested_ = false;
  scheduleState_ = ScheduleState::StartupPending;
}

void FirmwareUpdatePolicy::noteUserActivity(uint32_t nowMs) {
  lastActivityMs_ = nowMs;
}

void FirmwareUpdatePolicy::requestImmediate() { immediateRequested_ = true; }

bool FirmwareUpdatePolicy::shouldCheck(uint32_t nowMs, bool runtimeIdle,
                                       bool wifiConfigured) const {
  if (!wifiConfigured || !runtimeIdle) return false;
  if (immediateRequested_) return true;
  if ((scheduleState_ == ScheduleState::StartupPending ||
       scheduleState_ == ScheduleState::DeferredByForeground) &&
      reached(nowMs, startupDeadlineMs_)) {
    scheduleState_ = ScheduleState::StartupChecking;
    return true;
  }
  return reached(nowMs, nextCheckMs_) &&
         reached(nowMs, lastActivityMs_ + kIdleDelayMs);
}

uint32_t FirmwareUpdatePolicy::retryDelayMs(uint8_t failureCount) {
  if (failureCount <= 1) return 60U * 60U * 1000U;
  if (failureCount == 2) return 6U * 60U * 60U * 1000U;
  return kCheckIntervalMs;
}

void FirmwareUpdatePolicy::checked(uint32_t nowMs, bool success) {
  immediateRequested_ = false;
  scheduleState_ = ScheduleState::Scheduled;
  if (success) {
    failureCount_ = 0;
    nextCheckMs_ = nowMs + kCheckIntervalMs;
    return;
  }
  if (failureCount_ < 3) ++failureCount_;
  nextCheckMs_ = nowMs + retryDelayMs(failureCount_);
}

void FirmwareUpdatePolicy::deferStartup() {
  if (scheduleState_ == ScheduleState::StartupPending ||
      scheduleState_ == ScheduleState::StartupChecking) {
    scheduleState_ = ScheduleState::DeferredByForeground;
  }
}

}  // namespace studio
