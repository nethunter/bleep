#pragma once

#include <cstdint>

namespace studio {

class RecoveryTouchGate {
 public:
  static constexpr uint32_t kBootGuardMs = 1500;
  static constexpr uint32_t kReleaseSettleMs = 300;

  bool update(bool touched, uint32_t nowMs) {
    if (armed_) return true;
    if (!started_) {
      started_ = true;
      startedAtMs_ = nowMs;
    }
    if (nowMs - startedAtMs_ < kBootGuardMs) {
      releaseStarted_ = false;
      return false;
    }
    if (touched) {
      releaseStarted_ = false;
      return false;
    }
    if (!releaseStarted_) {
      releaseStarted_ = true;
      releasedAtMs_ = nowMs;
      return false;
    }
    if (nowMs - releasedAtMs_ >= kReleaseSettleMs) armed_ = true;
    return armed_;
  }

  bool armed() const { return armed_; }

 private:
  bool armed_ = false;
  bool started_ = false;
  bool releaseStarted_ = false;
  uint32_t startedAtMs_ = 0;
  uint32_t releasedAtMs_ = 0;
};

}  // namespace studio
