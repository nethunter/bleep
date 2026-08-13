#include "devices/gopro/protocol.h"

namespace gopro {

Packet buildSetShutter(bool enabled) {
  Packet packet;
  packet.bytes[0] = 0x03;
  packet.bytes[1] = kSetShutterCommand;
  packet.bytes[2] = 0x01;
  packet.bytes[3] = enabled ? 0x01 : 0x00;
  packet.len = 4;
  return packet;
}

Packet buildSleep() {
  Packet packet;
  packet.bytes[0] = 0x01;
  packet.bytes[1] = kSleepCommand;
  packet.len = 2;
  return packet;
}

Packet buildSetPairingState() {
  Packet packet;
  packet.bytes[0] = 0x03;
  packet.bytes[1] = kSetPairingStateCommand;
  packet.bytes[2] = 0x01;
  packet.bytes[3] = 0x01;
  packet.len = 4;
  return packet;
}

Packet buildGetHardwareInfo() {
  Packet packet;
  packet.bytes[0] = 0x01;
  packet.bytes[1] = kGetHardwareInfoCommand;
  packet.len = 2;
  return packet;
}

Packet buildGetEncoding(bool twoByteIds) {
  Packet packet;
  if (twoByteIds) {
    packet.bytes[0] = 0x03;
    packet.bytes[1] = kGetStatusValues2Byte;
    packet.bytes[2] = 0x00;
    packet.bytes[3] = kEncodingStatus;
    packet.len = 4;
  } else {
    packet.bytes[0] = 0x02;
    packet.bytes[1] = kGetStatusValues;
    packet.bytes[2] = kEncodingStatus;
    packet.len = 3;
  }
  return packet;
}

Packet buildRegisterEncoding(bool twoByteIds) {
  Packet packet;
  if (twoByteIds) {
    packet.bytes[0] = 0x03;
    packet.bytes[1] = kRegisterStatusUpdates2Byte;
    packet.bytes[2] = 0x00;
    packet.bytes[3] = kEncodingStatus;
    packet.len = 4;
  } else {
    packet.bytes[0] = 0x02;
    packet.bytes[1] = kRegisterStatusUpdates;
    packet.bytes[2] = kEncodingStatus;
    packet.len = 3;
  }
  return packet;
}

void PacketAccumulator::reset() {
  expected_ = 0;
  length_ = 0;
  continuation_ = 0;
}

bool PacketAccumulator::feed(const uint8_t* data, size_t len,
                             Message& message) {
  message.len = 0;
  if (data == nullptr || len == 0) return false;
  size_t offset = 0;
  if ((data[0] & 0x80) != 0) {
    if (expected_ == 0 || (data[0] & 0x0f) != continuation_) {
      reset();
      return false;
    }
    continuation_ = static_cast<uint8_t>((continuation_ + 1) & 0x0f);
    offset = 1;
  } else {
    reset();
    const uint8_t headerType = static_cast<uint8_t>((data[0] >> 5) & 0x03);
    if (headerType == 0) {
      expected_ = data[0] & 0x1f;
      offset = 1;
    } else if (headerType == 1 && len >= 2) {
      expected_ = (static_cast<size_t>(data[0] & 0x1f) << 8) | data[1];
      offset = 2;
    } else if (headerType == 2 && len >= 3) {
      expected_ = (static_cast<size_t>(data[1]) << 8) | data[2];
      offset = 3;
    } else {
      reset();
      return false;
    }
    if (expected_ == 0 || expected_ > sizeof(bytes_)) {
      reset();
      return false;
    }
  }
  const size_t incoming = len - offset;
  if (length_ + incoming > expected_ || length_ + incoming > sizeof(bytes_)) {
    reset();
    return false;
  }
  for (size_t i = 0; i < incoming; ++i) bytes_[length_ + i] = data[offset + i];
  length_ += incoming;
  if (length_ != expected_) return false;
  message.len = length_;
  for (size_t i = 0; i < length_; ++i) message.bytes[i] = bytes_[i];
  reset();
  return true;
}

Response parseCommandPayload(const uint8_t* data, size_t len) {
  Response response;
  if (data == nullptr || len < 2) return response;
  response.valid = true;
  response.command = data[0];
  response.status = data[1];
  return response;
}

Response parseResponse(const uint8_t* data, size_t len) {
  Response response;
  if (data == nullptr || len < 3 || data[0] < 2 || data[0] + 1 > len) {
    return response;
  }
  return parseCommandPayload(data + 1, data[0]);
}

StatusResponse parseStatusPayload(const uint8_t* data, size_t len) {
  StatusResponse response;
  if (data == nullptr || len < 2) return response;
  const bool twoByteIds = data[0] == kGetStatusValues2Byte ||
                          data[0] == kRegisterStatusUpdates2Byte ||
                          data[0] == kNotifyStatusUpdate2Byte;
  if (!twoByteIds && data[0] != kGetStatusValues &&
      data[0] != kRegisterStatusUpdates &&
      data[0] != kNotifyStatusUpdate) {
    return response;
  }
  response.valid = true;
  response.operation = data[0];
  response.status = data[1];
  size_t position = 2;
  while (position < len) {
    const size_t headerLength = twoByteIds ? 3 : 2;
    if (position + headerLength > len) return StatusResponse{};
    const uint16_t id = twoByteIds
                            ? static_cast<uint16_t>(data[position] << 8) |
                                  data[position + 1]
                            : data[position];
    const uint8_t valueLength = data[position + headerLength - 1];
    position += headerLength;
    if (position + valueLength > len) return StatusResponse{};
    if (id == kEncodingStatus) {
      if (valueLength != 1 || data[position] > 1) return StatusResponse{};
      response.hasEncoding = true;
      response.encoding = data[position] == 1;
    }
    position += valueLength;
  }
  return response;
}

}  // namespace gopro
