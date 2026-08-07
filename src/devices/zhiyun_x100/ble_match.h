#pragma once

#include "core/ble/ble_types.h"
#include "devices/zhiyun_x100/protocol.h"

namespace zhiyun_x100 {

inline bool matchesProduct(const studio::ble::Advertisement& advertisement) {
  return studio::ble::advertisementNameContains(advertisement, "PL105_") ||
         studio::ble::manufacturerCompanyId(advertisement) == 0x0905u;
}

inline bool matchesAdvertisement(
    const studio::ble::Advertisement& advertisement) {
  return matchesProduct(advertisement) &&
         studio::ble::advertisesService(advertisement,
                                        kProxyAdvertisedService);
}

inline bool matchesUnprovisionedAdvertisement(
    const studio::ble::Advertisement& advertisement) {
  return matchesProduct(advertisement) &&
         studio::ble::advertisesService(advertisement, "1827");
}

}  // namespace zhiyun_x100
