#pragma once

#include <cstddef>
#include <cstdint>

#include "core/ble/ble_types.h"

namespace studio::ble {

class OnboardingCandidates {
 public:
  static constexpr size_t kCapacity = 4;

  struct Entry {
    uint32_t token = 0;
    Advertisement advertisement;
  };

  // Returns true only when the visible candidate data changed. Existing
  // identities retain their token and slot; a full list replaces its weakest
  // member only when a stronger candidate arrives.
  bool observe(const Advertisement& advertisement);
  void clear();

  size_t count() const { return count_; }
  uint32_t revision() const { return revision_; }
  const Entry* at(size_t index) const;
  const Entry* find(uint32_t token) const;

 private:
  uint32_t allocateToken();

  Entry entries_[kCapacity] = {};
  size_t count_ = 0;
  uint32_t nextToken_ = 1;
  uint32_t revision_ = 0;
};

}  // namespace studio::ble
