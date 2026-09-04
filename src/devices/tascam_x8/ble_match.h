#pragma once

#include <cstring>

#include "core/ble/ble_types.h"
#include "devices/tascam_x8/protocol.h"

namespace tascam_x8 {

// One blind attempt, then listen. An awake AK-BT1 accepts a direct connect in
// 0.5-0.8 s but is seen advertising only every 2-19 s, so scan-first costs
// more than it saves (measured 2026-09-04). A drowsy dongle answers the poke
// with a failed establishment and 10-30 s of silence; the central then scans
// continuously and connects on the first connectable advertisement.
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
