#pragma once

#include <cstddef>
#include <cstdint>

namespace gopro {

constexpr const char* kAdvertisedServiceUuid = "fea6";
constexpr const char* kControlServiceUuid = "fea6";
constexpr const char* kCommandCharacteristicUuid =
    "b5f90072-aa8d-11e3-9046-0002a5d5c51b";
constexpr const char* kResponseCharacteristicUuid =
    "b5f90073-aa8d-11e3-9046-0002a5d5c51b";

constexpr uint8_t kSetShutterCommand = 0x01;
constexpr uint8_t kSetPairingStateCommand = 0x17;
constexpr uint8_t kSuccessStatus = 0x00;

struct Packet {
  uint8_t bytes[8] = {};
  size_t len = 0;
};

struct Response {
  bool valid = false;
  uint8_t command = 0;
  uint8_t status = 0xff;
};

Packet buildSetShutter(bool enabled);
Packet buildSetPairingState();
Response parseResponse(const uint8_t* data, size_t len);

}  // namespace gopro
