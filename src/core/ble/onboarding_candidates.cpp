#include "core/ble/onboarding_candidates.h"

#include <cstring>

namespace studio::ble {
namespace {

bool sameIdentity(const Advertisement& left, const Advertisement& right) {
  return left.address.type == right.address.type &&
         std::strncmp(left.address.value, right.address.value,
                      sizeof(left.address.value)) == 0;
}

bool sameAdvertisement(const Advertisement& left,
                       const Advertisement& right) {
  return sameIdentity(left, right) && left.rssi == right.rssi &&
         left.payloadLength == right.payloadLength &&
         std::memcmp(left.payload, right.payload, left.payloadLength) == 0;
}

}  // namespace

bool OnboardingCandidates::observe(const Advertisement& advertisement) {
  if (advertisement.address.value[0] == '\0') return false;

  for (size_t i = 0; i < count_; ++i) {
    if (!sameIdentity(entries_[i].advertisement, advertisement)) continue;
    if (sameAdvertisement(entries_[i].advertisement, advertisement)) {
      return false;
    }
    entries_[i].advertisement = advertisement;
    ++revision_;
    return true;
  }

  size_t slot = count_;
  if (count_ == kCapacity) {
    slot = 0;
    for (size_t i = 1; i < count_; ++i) {
      if (entries_[i].advertisement.rssi <
          entries_[slot].advertisement.rssi) {
        slot = i;
      }
    }
    if (advertisement.rssi <= entries_[slot].advertisement.rssi) {
      return false;
    }
  } else {
    ++count_;
  }

  entries_[slot].token = allocateToken();
  entries_[slot].advertisement = advertisement;
  ++revision_;
  return true;
}

void OnboardingCandidates::clear() {
  if (count_ == 0) return;
  count_ = 0;
  ++revision_;
}

const OnboardingCandidates::Entry* OnboardingCandidates::at(
    size_t index) const {
  return index < count_ ? &entries_[index] : nullptr;
}

const OnboardingCandidates::Entry* OnboardingCandidates::find(
    uint32_t token) const {
  if (token == 0) return nullptr;
  for (size_t i = 0; i < count_; ++i) {
    if (entries_[i].token == token) return &entries_[i];
  }
  return nullptr;
}

uint32_t OnboardingCandidates::allocateToken() {
  uint32_t token = nextToken_++;
  if (token == 0) token = nextToken_++;
  return token;
}

}  // namespace studio::ble
