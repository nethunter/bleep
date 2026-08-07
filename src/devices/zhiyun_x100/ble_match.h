#pragma once

#include <cstring>

#include "core/ble/ble_types.h"
#include "devices/zhiyun_x100/protocol.h"

namespace zhiyun_x100 {

inline bool hasPl105ManufacturerData(
    const studio::ble::Advertisement& advertisement) {
  size_t offset = 0;
  while (offset < advertisement.payloadLength) {
    const uint8_t fieldLength = advertisement.payload[offset];
    if (fieldLength == 0) break;
    const size_t next = offset + static_cast<size_t>(fieldLength) + 1;
    if (next > advertisement.payloadLength) return false;
    if (fieldLength >= 8 && advertisement.payload[offset + 1] == 0xff &&
        advertisement.payload[offset + 2] == 0x05 &&
        advertisement.payload[offset + 3] == 0x09 &&
        std::memcmp(advertisement.payload + offset + 4, "pl105", 5) == 0)
      return true;
    offset = next;
  }
  return false;
}

inline bool matchesProduct(const studio::ble::Advertisement& advertisement) {
  return studio::ble::advertisementNameContains(advertisement, "PL105_") ||
         hasPl105ManufacturerData(advertisement);
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
