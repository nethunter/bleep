#include "devices/dji_osmo/protocol.h"

#include <cstring>

namespace dji_osmo {
namespace {

void put16(uint8_t* out, uint16_t value) {
  out[0] = static_cast<uint8_t>(value);
  out[1] = static_cast<uint8_t>(value >> 8);
}

void put32(uint8_t* out, uint32_t value) {
  out[0] = static_cast<uint8_t>(value);
  out[1] = static_cast<uint8_t>(value >> 8);
  out[2] = static_cast<uint8_t>(value >> 16);
  out[3] = static_cast<uint8_t>(value >> 24);
}

uint16_t get16(const uint8_t* data) {
  return static_cast<uint16_t>(data[0]) |
         (static_cast<uint16_t>(data[1]) << 8);
}

uint32_t get32(const uint8_t* data) {
  return static_cast<uint32_t>(data[0]) |
         (static_cast<uint32_t>(data[1]) << 8) |
         (static_cast<uint32_t>(data[2]) << 16) |
         (static_cast<uint32_t>(data[3]) << 24);
}

Packet build(uint8_t commandSet, uint8_t commandId, uint8_t commandType,
             uint16_t sequence, const uint8_t* payload, size_t payloadLength) {
  Packet packet;
  packet.len = 14 + payloadLength + 4;
  if (packet.len > sizeof(packet.bytes)) {
    packet.len = 0;
    return packet;
  }
  packet.bytes[0] = 0xaa;
  put16(packet.bytes + 1, static_cast<uint16_t>(packet.len & 0x03ff));
  packet.bytes[3] = commandType;
  put16(packet.bytes + 8, sequence);
  put16(packet.bytes + 10, crc16(packet.bytes, 10));
  packet.bytes[12] = commandSet;
  packet.bytes[13] = commandId;
  if (payloadLength != 0) std::memcpy(packet.bytes + 14, payload, payloadLength);
  put32(packet.bytes + packet.len - 4, crc32(packet.bytes, packet.len - 4));
  return packet;
}

}  // namespace

uint16_t crc16(const uint8_t* data, size_t length) {
  uint16_t value = 0x3aa3;
  while (length-- != 0) {
    value ^= *data++;
    for (uint8_t bit = 0; bit < 8; ++bit)
      value = (value & 1) ? static_cast<uint16_t>((value >> 1) ^ 0xa001)
                          : static_cast<uint16_t>(value >> 1);
  }
  return value;
}

uint32_t crc32(const uint8_t* data, size_t length) {
  uint32_t value = 0x00003aa3;
  while (length-- != 0) {
    value ^= *data++;
    for (uint8_t bit = 0; bit < 8; ++bit)
      value = (value & 1) ? (value >> 1) ^ 0xedb88320UL : value >> 1;
  }
  return value;
}

Packet buildConnectionRequest(uint16_t sequence, uint32_t deviceId,
                              const uint8_t localAddress[6],
                              uint8_t verificationMode,
                              uint16_t verificationCode) {
  uint8_t payload[33] = {};
  put32(payload, deviceId);
  payload[4] = 6;
  if (localAddress != nullptr) std::memcpy(payload + 5, localAddress, 6);
  payload[25] = 0;  // connection index
  payload[26] = verificationMode;
  put16(payload + 27, verificationCode);
  return build(kCmdSetGeneral, kCmdConnection, 0x02, sequence, payload,
               sizeof(payload));
}

Packet buildConnectionResponse(uint16_t sequence, uint32_t deviceId,
                               uint8_t cameraIndex) {
  uint8_t payload[9] = {};
  put32(payload, deviceId);
  payload[5] = cameraIndex;
  return build(kCmdSetGeneral, kCmdConnection, 0x20, sequence, payload,
               sizeof(payload));
}

Packet buildStatusSubscription(uint16_t sequence) {
  const uint8_t payload[6] = {3, 20, 0, 0, 0, 0};
  return build(kCmdSetCamera, kCmdStatusSubscribe, 0x00, sequence, payload,
               sizeof(payload));
}

Packet buildRecordControl(uint16_t sequence, bool start) {
  const uint8_t payload[9] = {0x00, 0x00, 0xff, 0x33,
                              static_cast<uint8_t>(start ? 0 : 1),
                              0, 0, 0, 0};
  return build(kCmdSetCamera, kCmdRecord, 0x01, sequence, payload,
               sizeof(payload));
}

size_t declaredFrameLength(const uint8_t* data, size_t length) {
  return data != nullptr && length >= 3 && data[0] == 0xaa
             ? get16(data + 1) & 0x03ff
             : 0;
}

Frame parseFrame(const uint8_t* data, size_t length) {
  Frame frame;
  const size_t declared = declaredFrameLength(data, length);
  if (declared < 18 || declared > length || crc16(data, 10) != get16(data + 10) ||
      crc32(data, declared - 4) != get32(data + declared - 4)) return frame;
  frame.valid = true;
  frame.commandType = data[3];
  frame.sequence = get16(data + 8);
  frame.commandSet = data[12];
  frame.commandId = data[13];
  frame.payload = data + 14;
  frame.payloadLength = declared - 18;
  return frame;
}

bool parseConnectionApproval(const Frame& frame, bool& approved) {
  if (!frame.valid || frame.commandSet != kCmdSetGeneral ||
      frame.commandId != kCmdConnection ||
      (frame.commandType & 0x20) != 0 || frame.payloadLength < 29 ||
      frame.payload[26] != 2) {
    return false;
  }
  approved = frame.payload[27] == 0 && frame.payload[28] == 0;
  return true;
}

bool decodeCameraRecordingStatus(uint8_t cameraMode, uint8_t cameraStatus,
                                 bool& recording) {
  switch (cameraStatus) {
    case 0x00:  // Screen off
    case 0x01:  // Live view
    case 0x02:  // Playback
      recording = false;
      return true;
    case 0x03:  // Photo or recording in progress
    case 0x05:  // Pre-recording
      recording = cameraMode != 0x05 && cameraMode != 0x3f;
      return true;
    default:
      return false;
  }
}

}  // namespace dji_osmo
