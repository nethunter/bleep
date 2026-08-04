#pragma once

#include "core/ble/ble_types.h"
#include "devices/tascam_x8/protocol.h"

namespace tascam_x8 {

inline bool matchesAdvertisement(
    const studio::ble::Advertisement& advertisement) {
  return studio::ble::advertisementNameEquals(advertisement, kDeviceName);
}

}  // namespace tascam_x8
