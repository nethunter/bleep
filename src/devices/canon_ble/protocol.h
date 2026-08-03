#pragma once

#include <cstddef>
#include <cstdint>

namespace canon_ble {

constexpr const char* kPrimaryServiceUuid =
    "00050000-0000-1000-0000-d8492fffa821";
constexpr const char* kPairingCharacteristicUuid =
    "00050002-0000-1000-0000-d8492fffa821";
constexpr const char* kControlCharacteristicUuid =
    "00050003-0000-1000-0000-d8492fffa821";

constexpr uint8_t kPairingNamePrefix = 0x03;
constexpr uint8_t kImmediateMode = 0x0c;
constexpr uint8_t kMovieMode = 0x08;
constexpr uint8_t kShutterButton = 0x80;
constexpr uint8_t kRecordTriggerPress = kMovieMode | kShutterButton;
constexpr uint8_t kRecordTriggerRelease = kMovieMode;
constexpr uint32_t kTriggerHoldMs = 200;

struct PairingName {
  uint8_t bytes[41] = {};
  size_t len = 0;
};

PairingName buildPairingName(const char* name);

}  // namespace canon_ble
