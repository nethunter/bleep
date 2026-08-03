#include "devices/tascam_x8/protocol.h"

#include <cstring>

namespace tascam_x8 {

namespace {

constexpr size_t kCommandPayloadLen = 14;

FrameBytes buildCommand(uint8_t group, uint8_t direction, uint8_t field0,
                        uint8_t field1) {
  uint8_t payload[kCommandPayloadLen] = {
      'D', 'R', group, direction, field0, field1,
  };
  return encodeFrame(payload, sizeof(payload));
}

bool append(FrameBytes& destination, const FrameBytes& source) {
  if (source.len == 0 || destination.len + source.len > sizeof(destination.bytes)) {
    destination.len = 0;
    return false;
  }
  std::memcpy(destination.bytes + destination.len, source.bytes, source.len);
  destination.len += source.len;
  return true;
}

}  // namespace

FrameBytes encodeFrame(const uint8_t* payload, size_t payloadLen) {
  FrameBytes result;
  if (payload == nullptr || payloadLen == 0 ||
      payloadLen > kMaxDecodedFrame) {
    return result;
  }

  result.bytes[0] = 0;
  size_t codeIndex = 1;
  size_t writeIndex = 2;
  uint8_t code = 1;

  for (size_t i = 0; i < payloadLen; ++i) {
    if (writeIndex >= sizeof(result.bytes) - 1) {
      return FrameBytes{};
    }
    if (payload[i] == 0) {
      result.bytes[codeIndex] = code;
      codeIndex = writeIndex++;
      code = 1;
      continue;
    }
    result.bytes[writeIndex++] = payload[i];
    ++code;
    if (code == 0xFF) {
      result.bytes[codeIndex] = code;
      codeIndex = writeIndex++;
      code = 1;
    }
  }

  result.bytes[codeIndex] = code;
  result.bytes[writeIndex++] = 0;
  result.len = writeIndex;
  return result;
}

FrameBytes buildRecordStart() {
  return buildCommand(0x10, 0x41, 0x00, 0x0B);
}

FrameBytes buildRecordStop() {
  return buildCommand(0x10, 0x41, 0x00, 0x08);
}

FrameBytes buildInitialization() {
  // Packet 26202 in the annotated official-app capture. Replaying these
  // read-style requests enables the same initial state stream while unknown
  // response families remain ignored by the record-only driver.
  constexpr uint8_t queries[][4] = {
      {0x20, 0x42, 0x00, 0x00}, {0x20, 0x42, 0x11, 0x00},
      {0x20, 0x42, 0x31, 0x00}, {0xF0, 0x41, 0x32, 0x00},
      {0x20, 0x42, 0x20, 0x10}, {0x20, 0x42, 0x20, 0x05},
      {0x20, 0x42, 0x21, 0x00}, {0x20, 0x42, 0x22, 0x00},
      {0x20, 0x42, 0x26, 0x00}, {0x20, 0x42, 0x20, 0x13},
      {0x03, 0x41, 0x1E, 0x00}, {0x03, 0x41, 0x15, 0x00},
      {0xF0, 0x41, 0x00, 0x02},
  };

  FrameBytes result;
  for (const auto& query : queries) {
    uint8_t payload[kCommandPayloadLen] = {
        'D', 'R', query[0], query[1], query[2], query[3],
    };
    if (!append(result, encodeFrame(payload, sizeof(payload)))) {
      return FrameBytes{};
    }
  }
  return result;
}

void FrameScanner::feed(const uint8_t* data, size_t len,
                        const Handler& handler) {
  if (data == nullptr || !handler) {
    return;
  }
  for (size_t i = 0; i < len; ++i) {
    if (data[i] == 0) {
      finish(handler);
      encodedLen_ = 0;
      discarding_ = false;
      continue;
    }
    if (discarding_) {
      continue;
    }
    if (encodedLen_ >= sizeof(encoded_)) {
      encodedLen_ = 0;
      discarding_ = true;
      continue;
    }
    encoded_[encodedLen_++] = data[i];
  }
}

void FrameScanner::finish(const Handler& handler) {
  if (encodedLen_ == 0 || discarding_) {
    return;
  }

  uint8_t decoded[kMaxDecodedFrame] = {};
  size_t decodedLen = 0;
  size_t readIndex = 0;
  while (readIndex < encodedLen_) {
    const uint8_t code = encoded_[readIndex++];
    if (code == 0 ||
        readIndex + static_cast<size_t>(code - 1) > encodedLen_) {
      return;
    }
    for (uint8_t i = 1; i < code; ++i) {
      if (decodedLen >= sizeof(decoded)) {
        return;
      }
      decoded[decodedLen++] = encoded_[readIndex++];
    }
    if (code < 0xFF && readIndex < encodedLen_) {
      if (decodedLen >= sizeof(decoded)) {
        return;
      }
      decoded[decodedLen++] = 0;
    }
  }

  if (decodedLen < 2 || decoded[0] != 'D' || decoded[1] != 'R') {
    return;
  }
  ParsedFrame frame;
  frame.data = decoded;
  frame.len = decodedLen;
  handler(frame);
}

RecordEvent parseRecordEvent(const ParsedFrame& frame) {
  if (frame.data == nullptr || frame.len < 5 || frame.data[0] != 'D' ||
      frame.data[1] != 'R') {
    return RecordEvent::None;
  }
  if (frame.len >= 6 && frame.data[2] == 0x20 && frame.data[3] == 0x20 &&
      frame.data[4] == 0x24 && frame.data[5] == 0x01) {
    return RecordEvent::Started;
  }
  if (frame.data[2] == 0x10 && frame.data[3] == 0x20 &&
      frame.data[4] == 0x08) {
    return RecordEvent::Stopped;
  }
  return RecordEvent::None;
}

}  // namespace tascam_x8
