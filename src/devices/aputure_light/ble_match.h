#pragma once

#include <cstring>

#include "core/ble/ble_types.h"
#include "devices/aputure_light/crypto.h"

namespace aputure_light {

inline bool matchesMeshProxyNetwork(
    const studio::ble::Advertisement& advertisement,
    const uint8_t networkKey[16]) {
  if (networkKey == nullptr) return false;
  uint8_t advertisedNetworkId[8];
  if (!studio::ble::meshProxyNetworkId(advertisement,
                                       advertisedNetworkId)) {
    return false;
  }
  uint8_t expectedNetworkId[8];
  meshK3(networkKey, expectedNetworkId);
  return std::memcmp(advertisedNetworkId, expectedNetworkId,
                     sizeof(expectedNetworkId)) == 0;
}

}  // namespace aputure_light
