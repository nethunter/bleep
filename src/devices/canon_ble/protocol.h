#pragma once

#include <cstddef>
#include <cstdint>

namespace canon_ble {

constexpr const char* kHandshakeServiceUuid =
    "00010000-0000-1000-0000-d8492fffa821";
constexpr const char* kPairingCommandCharacteristicUuid =
    "00010006-0000-1000-0000-d8492fffa821";
constexpr const char* kPairingDataCharacteristicUuid =
    "0001000a-0000-1000-0000-d8492fffa821";
constexpr const char* kCoreServiceUuid =
    "00030000-0000-1000-0000-d8492fffa821";
constexpr const char* kModeCommandCharacteristicUuid =
    "00030010-0000-1000-0000-d8492fffa821";
constexpr const char* kModeResultCharacteristicUuid =
    "00030011-0000-1000-0000-d8492fffa821";
constexpr const char* kShootingCommandCharacteristicUuid =
    "00030030-0000-1000-0000-d8492fffa821";
constexpr const char* kShootingStateCharacteristicUuid =
    "00030031-0000-1000-0000-d8492fffa821";

constexpr uint8_t kPairingConfirmed = 0x02;
constexpr uint8_t kPairingRejected = 0x03;
constexpr uint8_t kShootingMode = 0x02;
constexpr uint8_t kWakeMode = 0x03;
constexpr uint8_t kRecordingSessionMode = 0x06;

struct CommandBytes {
  uint8_t bytes[41] = {};
  size_t len = 0;
};

enum class PairingResponse : uint8_t {
  None,
  Confirmed,
  Rejected,
};

enum class ModeEvent : uint8_t {
  None,
  Shooting,
  RecordingSession,
};

enum class RecordEvent : uint8_t {
  None,
  Started,
  Stopped,
};

CommandBytes buildHandshakeRequest(const char* name);
CommandBytes buildControllerId(const uint8_t id[16]);
CommandBytes buildDeviceName(const char* name);
CommandBytes buildAndroidDeviceType();
CommandBytes buildHandshakeFinish();
CommandBytes buildModeCommand(uint8_t mode);
CommandBytes buildRecordCommand(bool start);

PairingResponse parsePairingResponse(const uint8_t* data, size_t len);
ModeEvent parseModeEvent(const uint8_t* data, size_t len);
RecordEvent parseRecordEvent(const uint8_t* data, size_t len);

}  // namespace canon_ble
