#pragma once

#include <cstring>

#include "core/ble/ble_types.h"
#include "devices/zhiyun_x100/protocol.h"

namespace zhiyun_x100 {

inline bool hasManufacturerMarker(
    const studio::ble::Advertisement& advertisement, const char* marker) {
  if (marker == nullptr || marker[0] == '\0') return false;
  const size_t markerLength = std::strlen(marker);
  size_t offset = 0;
  while (offset < advertisement.payloadLength) {
    const uint8_t fieldLength = advertisement.payload[offset];
    if (fieldLength == 0) break;
    const size_t next = offset + static_cast<size_t>(fieldLength) + 1;
    if (next > advertisement.payloadLength) return false;
    if (fieldLength >= markerLength + 3 &&
        advertisement.payload[offset + 1] == 0xff &&
        advertisement.payload[offset + 2] == 0x05 &&
        advertisement.payload[offset + 3] == 0x09 &&
        std::memcmp(advertisement.payload + offset + 4, marker,
                    markerLength) == 0)
      return true;
    offset = next;
  }
  return false;
}

inline bool matchesProduct(const studio::ble::Advertisement& advertisement) {
  return studio::ble::advertisementNameContains(advertisement, "PL105_") ||
         hasManufacturerMarker(advertisement, "pl105");
}

inline bool matchesProduct(const studio::ble::Advertisement& advertisement,
                           const char* namePrefix,
                           const char* manufacturerMarker) {
  return studio::ble::advertisementNameContains(advertisement, namePrefix) ||
         hasManufacturerMarker(advertisement, manufacturerMarker);
}

inline bool matchesAdvertisement(
    const studio::ble::Advertisement& advertisement, const char* namePrefix,
    const char* manufacturerMarker) {
  return matchesProduct(advertisement, namePrefix, manufacturerMarker) &&
         studio::ble::advertisesService(advertisement,
                                        kProxyAdvertisedService);
}

inline bool matchesUnprovisionedAdvertisement(
    const studio::ble::Advertisement& advertisement, const char* namePrefix,
    const char* manufacturerMarker) {
  return matchesProduct(advertisement, namePrefix, manufacturerMarker) &&
         studio::ble::advertisesService(advertisement, "1827");
}

inline MolusModel advertisementModel(
    const studio::ble::Advertisement& advertisement) {
  if (matchesProduct(advertisement, "PL105_", "pl105"))
    return MolusModel::X100;
  if (matchesProduct(advertisement, "X104_", "plx104"))
    return MolusModel::X60Rgb;
  return MolusModel::Unknown;
}

inline MolusModel modelFromName(const char* name) {
  if (name == nullptr) return MolusModel::Unknown;
  if (std::strstr(name, "PL105_") != nullptr) return MolusModel::X100;
  if (std::strstr(name, "X104_") != nullptr) return MolusModel::X60Rgb;
  return MolusModel::Unknown;
}

inline bool matchesMolusAdvertisement(
    const studio::ble::Advertisement& advertisement, bool provisioned) {
  if (advertisementModel(advertisement) == MolusModel::Unknown) return false;
  return studio::ble::advertisesService(
      advertisement, provisioned ? kProxyAdvertisedService : "1827");
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
