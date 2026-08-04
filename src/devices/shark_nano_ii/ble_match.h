#pragma once

#include "core/ble/ble_types.h"

namespace shark {

inline bool matchesAdvertisement(
    const studio::ble::Advertisement& advertisement) {
  return studio::ble::advertisesService(advertisement, "fff0") ||
         studio::ble::advertisementNameContains(advertisement, "Nano") ||
         studio::ble::advertisementNameContains(advertisement, "Shark");
}

}  // namespace shark
