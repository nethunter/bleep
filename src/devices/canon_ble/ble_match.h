#pragma once

#include "core/ble/ble_types.h"
#include "devices/canon_ble/protocol.h"

namespace canon_ble {

inline bool matchesAdvertisement(
    const studio::ble::Advertisement& advertisement) {
  return studio::ble::advertisesService(advertisement,
                                        kHandshakeServiceUuid) ||
         studio::ble::manufacturerCompanyId(advertisement) == 0x01A9u ||
         studio::ble::advertisementNameContains(advertisement, "EOS") ||
         studio::ble::advertisementNameContains(advertisement, "R6") ||
         studio::ble::advertisementNameContains(advertisement, "PowerShot");
}

}  // namespace canon_ble
