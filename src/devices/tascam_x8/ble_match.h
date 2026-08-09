#pragma once

#include <cstring>

#include "core/ble/ble_types.h"
#include "devices/tascam_x8/protocol.h"

namespace tascam_x8 {

constexpr uint8_t kDirectAttemptsBeforeScan = 1;

inline bool matchesAdvertisement(
    const studio::ble::Advertisement& advertisement) {
  return studio::ble::advertisesService(advertisement, kPrimaryServiceUuid) ||
         studio::ble::advertisementNameEquals(advertisement, kDeviceName);
}

inline bool matchesSavedAdvertisement(
    const studio::ble::Advertisement& advertisement, const char* savedAddress,
    uint8_t savedAddressType) {
  return matchesAdvertisement(advertisement) &&
         (savedAddress == nullptr || savedAddress[0] == '\0' ||
          (advertisement.address.type == savedAddressType &&
           std::strcmp(advertisement.address.value, savedAddress) == 0));
}

}  // namespace tascam_x8
