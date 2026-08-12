#pragma once

#include <cstddef>
#include <cstdint>

namespace studio::ble {

class OnboardingAutoSelect {
 public:
  enum class Decision : uint8_t {
    Wait,
    Select,
    ShowPicker,
  };

  static constexpr uint32_t kSettleMs = 750;

  Decision update(size_t candidateCount, uint32_t onlyToken, uint32_t now) {
    if (candidateCount != 1 || onlyToken == 0) {
      reset();
      return candidateCount == 0 ? Decision::Wait : Decision::ShowPicker;
    }
    if (onlyToken != token_) {
      token_ = onlyToken;
      firstSeenAt_ = now;
      selectionFailed_ = false;
      return Decision::Wait;
    }
    if (selectionFailed_) return Decision::ShowPicker;
    return static_cast<uint32_t>(now - firstSeenAt_) >= kSettleMs
               ? Decision::Select
               : Decision::Wait;
  }

  void selectionFailed(uint32_t token) {
    if (token_ == token) selectionFailed_ = true;
  }

  void reset() {
    token_ = 0;
    firstSeenAt_ = 0;
    selectionFailed_ = false;
  }

 private:
  uint32_t token_ = 0;
  uint32_t firstSeenAt_ = 0;
  bool selectionFailed_ = false;
};

}  // namespace studio::ble
