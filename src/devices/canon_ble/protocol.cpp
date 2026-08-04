#include "devices/canon_ble/protocol.h"

#include <cstring>

namespace canon_ble {

namespace {

CommandBytes buildTextCommand(uint8_t prefix, const char* text) {
  CommandBytes result;
  result.bytes[0] = prefix;
  result.len = 1;
  if (text == nullptr) {
    return result;
  }
  const size_t textLength = std::strlen(text);
  const size_t copyLength =
      textLength < sizeof(result.bytes) - 1 ? textLength
                                            : sizeof(result.bytes) - 1;
  std::memcpy(&result.bytes[1], text, copyLength);
  result.len += copyLength;
  return result;
}

}  // namespace

CommandBytes buildHandshakeRequest(const char* name) {
  return buildTextCommand(0x01, name);
}

CommandBytes buildControllerId(const uint8_t id[16]) {
  CommandBytes result;
  result.bytes[0] = 0x03;
  result.len = 1;
  if (id != nullptr) {
    std::memcpy(&result.bytes[1], id, 16);
    result.len += 16;
  }
  return result;
}

CommandBytes buildDeviceName(const char* name) {
  return buildTextCommand(0x04, name);
}

CommandBytes buildAndroidDeviceType() {
  CommandBytes result;
  result.bytes[0] = 0x05;
  result.bytes[1] = 0x02;
  result.len = 2;
  return result;
}

CommandBytes buildHandshakeFinish() {
  CommandBytes result;
  result.bytes[0] = 0x01;
  result.len = 1;
  return result;
}

CommandBytes buildPostPairCommand(uint8_t command) {
  CommandBytes result;
  result.bytes[0] = command;
  result.len = 1;
  return result;
}

CommandBytes buildModeCommand(uint8_t mode) {
  CommandBytes result;
  result.bytes[0] = mode;
  result.len = 1;
  return result;
}

CommandBytes buildRecordCommand(bool start) {
  CommandBytes result;
  result.bytes[0] = 0x00;
  result.bytes[1] = start ? 0x10 : 0x11;
  result.len = 2;
  return result;
}

PairingResponse parsePairingResponse(const uint8_t* data, size_t len) {
  if (data == nullptr || len != 1) {
    return PairingResponse::None;
  }
  if (data[0] == kPairingConfirmed) {
    return PairingResponse::Confirmed;
  }
  if (data[0] == kPairingRejected) {
    return PairingResponse::Rejected;
  }
  return PairingResponse::None;
}

bool isPostPairResponse(uint8_t command, const uint8_t* data, size_t len) {
  if (data == nullptr || len == 0) {
    return false;
  }
  switch (command) {
    case 0x06:
      return data[0] == 0x01;
    case 0x07:
      return data[0] == 0x02;
    case 0x08:
      return data[0] == 0x03;
    case 0x0c:
      return data[0] == 0x07;
    default:
      return false;
  }
}

ModeEvent parseModeEvent(const uint8_t* data, size_t len) {
  if (data == nullptr || len != 1) {
    return ModeEvent::None;
  }
  if (data[0] == kModeAcknowledged) {
    return ModeEvent::Acknowledged;
  }
  if (data[0] == kSessionReady || data[0] == kShootingModeReady) {
    return ModeEvent::SessionReady;
  }
  return ModeEvent::None;
}

RecordEvent parseRecordEvent(const uint8_t* data, size_t len) {
  if (data == nullptr || len != 3 || data[0] != 0x01 ||
      data[1] != 0x01) {
    return RecordEvent::None;
  }
  if (data[2] == 0x02) {
    return RecordEvent::Started;
  }
  if (data[2] == 0x01) {
    return RecordEvent::Stopped;
  }
  return RecordEvent::None;
}

}  // namespace canon_ble
