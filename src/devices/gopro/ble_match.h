#pragma once

#include "core/ble/ble_types.h"
#include "devices/gopro/protocol.h"

namespace gopro {

inline bool matchesAdvertisement(const studio::ble::Advertisement& advertisement) {
  return studio::ble::advertisesService(advertisement, kAdvertisedServiceUuid);
}

inline bool processorAwake(const studio::ble::Advertisement& advertisement,
                           bool& known) {
  known = false;
  size_t offset = 0;
  while (offset < advertisement.payloadLength) {
    const size_t fieldLength = advertisement.payload[offset];
    if (fieldLength == 0 ||
        offset + fieldLength >= advertisement.payloadLength) {
      return false;
    }
    if (advertisement.payload[offset + 1] == 0xff && fieldLength >= 5) {
      const uint8_t* data = advertisement.payload + offset + 2;
      if (data[0] == 0xf2 && data[1] == 0x02 &&
          (data[2] == 2 || data[2] == 3)) {
        known = true;
        return (data[3] & 0x01) != 0;
      }
    }
    offset += fieldLength + 1;
  }
  return false;
}

}  // namespace gopro
