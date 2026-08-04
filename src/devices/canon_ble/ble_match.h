#pragma once

#include "core/ble/ble_types.h"
#include "devices/canon_ble/protocol.h"

namespace canon_ble {

inline bool matchesAdvertisement(
    const studio::ble::Advertisement& advertisement) {
  return studio::ble::advertisesService(advertisement,
                                        kHandshakeServiceUuid) ||
         studio::ble::manufacturerCompanyId(advertisement) == 0x01A9u;
}

}  // namespace canon_ble
