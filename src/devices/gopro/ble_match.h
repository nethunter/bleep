#pragma once

#include "core/ble/ble_types.h"
#include "devices/gopro/protocol.h"

namespace gopro {

inline bool matchesAdvertisement(const studio::ble::Advertisement& advertisement) {
  return studio::ble::advertisesService(advertisement, kAdvertisedServiceUuid);
}

}  // namespace gopro
