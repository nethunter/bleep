#pragma once

#include <cstdint>

namespace studio {

class FirmwareUpdatePolicy {
 public:
  enum class ScheduleState : uint8_t {
    StartupPending,
    StartupChecking,
    Scheduled,
    DeferredByForeground,
  };

  static constexpr uint32_t kStartupDelayMs = 5U * 1000U;
  static constexpr uint32_t kIdleDelayMs = 10U * 60U * 1000U;
  static constexpr uint32_t kCheckIntervalMs = 24U * 60U * 60U * 1000U;

  void begin(uint32_t nowMs);
  void noteUserActivity(uint32_t nowMs);
  void requestImmediate();
  bool shouldCheck(uint32_t nowMs, bool runtimeIdle, bool wifiConfigured) const;
  void checked(uint32_t nowMs, bool success);
  void deferStartup();

  bool immediateRequested() const { return immediateRequested_; }
  uint8_t failureCount() const { return failureCount_; }
  uint32_t lastActivityMs() const { return lastActivityMs_; }
  ScheduleState scheduleState() const { return scheduleState_; }

 private:
  static uint32_t retryDelayMs(uint8_t failureCount);

  uint32_t lastActivityMs_ = 0;
  uint32_t nextCheckMs_ = 0;
  uint8_t failureCount_ = 0;
  bool immediateRequested_ = false;
  mutable ScheduleState scheduleState_ = ScheduleState::StartupPending;
  uint32_t startupDeadlineMs_ = 0;
};

}  // namespace studio
