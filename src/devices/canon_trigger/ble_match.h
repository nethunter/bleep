#pragma once

#include "core/ble/ble_types.h"
#include "devices/canon_trigger/protocol.h"

namespace canon_trigger {

inline bool matchesAdvertisement(
    const studio::ble::Advertisement& advertisement) {
  return studio::ble::advertisesService(advertisement, kPrimaryServiceUuid);
}

}  // namespace canon_trigger
