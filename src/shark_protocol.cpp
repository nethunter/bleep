#include "shark_protocol.h"

#include <cstring>

namespace shark {

uint32_t crc32Ieee(const uint8_t* data, size_t len) {
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0; i < len; ++i) {
    crc ^= data[i];
    for (int bit = 0; bit < 8; ++bit) {
      const uint32_t mask = -(int32_t)(crc & 1u);
      crc = (crc >> 1) ^ (0xEDB88320u & mask);
    }
  }
  return crc ^ 0xFFFFFFFFu;
}

FrameBytes encodeFrame(uint8_t family, uint8_t code, const uint8_t* data, size_t dataLen) {
  FrameBytes frame;
  // 2 prefix + 4 body header + data + 4 crc + 2 suffix.
  const size_t total = 12 + dataLen;
  if (total > kMaxFrame) {
    frame.len = 0;
    return frame;
  }

  // Build the body first so the CRC can be computed over it.
  uint8_t body[kMaxFrame];
  body[0] = family;
  body[1] = code;
  body[2] = static_cast<uint8_t>((dataLen >> 8) & 0xFF);
  body[3] = static_cast<uint8_t>(dataLen & 0xFF);
  if (dataLen > 0 && data != nullptr) {
    memcpy(body + 4, data, dataLen);
  }
  const size_t bodyLen = 4 + dataLen;
  const uint32_t crc = crc32Ieee(body, bodyLen);

  size_t i = 0;
  frame.bytes[i++] = kFramePrefix0;
  frame.bytes[i++] = kFramePrefix1;
  memcpy(frame.bytes + i, body, bodyLen);
  i += bodyLen;
  frame.bytes[i++] = static_cast<uint8_t>((crc >> 24) & 0xFF);
  frame.bytes[i++] = static_cast<uint8_t>((crc >> 16) & 0xFF);
  frame.bytes[i++] = static_cast<uint8_t>((crc >> 8) & 0xFF);
  frame.bytes[i++] = static_cast<uint8_t>(crc & 0xFF);
  frame.bytes[i++] = kFrameSuffix0;
  frame.bytes[i++] = kFrameSuffix1;
  frame.len = i;
  return frame;
}

namespace {

int clampMotionVelocity(int value) {
  if (value < -kManualMotionMaxVelocity) {
    return -kManualMotionMaxVelocity;
  }
  if (value > kManualMotionMaxVelocity) {
    return kManualMotionMaxVelocity;
  }
  return value;
}

// Attempt to validate and parse a complete candidate frame starting at `buf`.
// Returns true and fills `out` if the candidate is a structurally valid frame
// with a matching CRC. `totalLen` is the full frame length including envelope.
bool tryParse(const uint8_t* buf, size_t available, size_t& totalLen, ParsedFrame& out) {
  if (available < 12) {
    return false;
  }
  if (buf[0] != kFramePrefix0 || buf[1] != kFramePrefix1) {
    return false;
  }
  const uint16_t dataLen = (static_cast<uint16_t>(buf[4]) << 8) | buf[5];
  totalLen = 12 + dataLen;
  if (totalLen > available) {
    return false;
  }
  if (buf[totalLen - 2] != kFrameSuffix0 || buf[totalLen - 1] != kFrameSuffix1) {
    return false;
  }

  const uint8_t* body = buf + 2;
  const size_t bodyLen = 4 + dataLen;
  const uint32_t expected = (static_cast<uint32_t>(buf[2 + bodyLen]) << 24) |
                            (static_cast<uint32_t>(buf[3 + bodyLen]) << 16) |
                            (static_cast<uint32_t>(buf[4 + bodyLen]) << 8) |
                            static_cast<uint32_t>(buf[5 + bodyLen]);
  if (crc32Ieee(body, bodyLen) != expected) {
    return false;
  }

  out.family = body[0];
  out.code = body[1];
  out.kind = dataLen;
  out.data = (dataLen > 0) ? (body + 4) : nullptr;
  out.dataLen = dataLen;
  return true;
}

int findPrefix(const uint8_t* buf, size_t len, size_t from) {
  for (size_t i = from; i + 1 < len; ++i) {
    if (buf[i] == kFramePrefix0 && buf[i + 1] == kFramePrefix1) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

}  // namespace

void FrameScanner::feed(const uint8_t* data, size_t len, const Handler& handler) {
  // Append new bytes, dropping the oldest data if we somehow overflow (the
  // device never legitimately sends a fragment this large).
  if (len_ + len > kBufCap) {
    const size_t overflow = len_ + len - kBufCap;
    if (overflow >= len_) {
      len_ = 0;
    } else {
      memmove(buf_, buf_ + overflow, len_ - overflow);
      len_ -= overflow;
    }
  }
  const size_t copy = (len > kBufCap) ? kBufCap : len;
  if (len_ + copy > kBufCap) {
    len_ = kBufCap - copy;
  }
  memcpy(buf_ + len_, data + (len - copy), copy);
  len_ += copy;

  size_t offset = 0;
  while (offset < len_) {
    const int start = findPrefix(buf_, len_, offset);
    if (start < 0) {
      // No prefix left; discard everything (keep nothing, no partial prefix).
      len_ = 0;
      return;
    }
    const size_t available = len_ - static_cast<size_t>(start);
    if (available < 12) {
      // Possible partial frame; keep from the prefix onward.
      memmove(buf_, buf_ + start, available);
      len_ = available;
      return;
    }
    size_t totalLen = 0;
    ParsedFrame parsed;
    if (tryParse(buf_ + start, available, totalLen, parsed)) {
      handler(parsed);
      offset = static_cast<size_t>(start) + totalLen;
      continue;
    }
    // The length header may say the frame is longer than what we have; if the
    // declared length still fits in the buffer capacity, wait for more bytes.
    const uint16_t declaredLen = (static_cast<uint16_t>(buf_[start + 4]) << 8) | buf_[start + 5];
    const size_t declaredTotal = 12 + declaredLen;
    if (declaredTotal > available && declaredTotal <= kBufCap) {
      memmove(buf_, buf_ + start, available);
      len_ = available;
      return;
    }
    // Bad candidate (CRC/suffix mismatch). Resync to the next prefix.
    offset = static_cast<size_t>(start) + 1;
  }
  len_ = 0;
}

FrameBytes buildControlPing(uint8_t code, uint8_t tx) {
  const uint8_t data[1] = {tx};
  return encodeFrame(0x06, code, data, 1);
}

FrameBytes buildKeypointSlots(uint8_t tx, const uint8_t slots[kKeypointCount]) {
  uint8_t data[1 + kKeypointCount];
  data[0] = tx;
  memcpy(data + 1, slots, kKeypointCount);
  return encodeFrame(0x03, 0x03, data, sizeof(data));
}

FrameBytes buildKeypointAction(int slotIndex, uint8_t marker, uint8_t tx) {
  uint8_t slots[kKeypointCount];
  for (int i = 0; i < kKeypointCount; ++i) {
    slots[i] = kMarkerIdle;
  }
  if (slotIndex >= 0 && slotIndex < kKeypointCount) {
    slots[slotIndex] = marker;
  }
  return buildKeypointSlots(tx, slots);
}

FrameBytes buildKeypointDelete(int slotIndex, const bool present[kKeypointCount], uint8_t tx) {
  uint8_t slots[kKeypointCount];
  for (int i = 0; i < kKeypointCount; ++i) {
    slots[i] = kMarkerIdle;
  }
  if (slotIndex >= 0 && slotIndex < kKeypointCount) {
    slots[slotIndex] = kMarkerDelete;
    if (present != nullptr) {
      for (int i = slotIndex; i < kKeypointCount; ++i) {
        if (present[i]) {
          slots[i] = kMarkerDelete;
        }
      }
    }
  }
  return buildKeypointSlots(tx, slots);
}

FrameBytes buildRunState(uint8_t state, uint8_t tx) {
  const uint8_t data[3] = {tx, 0x00, state};
  return encodeFrame(0x03, 0x0D, data, sizeof(data));
}

FrameBytes buildRouteConfig(uint8_t tx, const int* slotIndexes, const uint8_t* slotValues,
                            int count) {
  uint8_t data[1 + kRouteConfigSlots];
  data[0] = tx;
  for (int i = 0; i < kRouteConfigSlots; ++i) {
    data[1 + i] = 0xFF;
  }
  for (int i = 0; i < count; ++i) {
    const int idx = slotIndexes[i];
    if (idx >= 0 && idx < kRouteConfigSlots) {
      data[1 + idx] = slotValues[i];
    }
  }
  return encodeFrame(0x03, 0x05, data, sizeof(data));
}

FrameBytes buildLoop(bool loopOn, uint8_t tx) {
  const int idx[1] = {kRouteLoopSlot};
  const uint8_t val[1] = {static_cast<uint8_t>(loopOn ? 0x01 : 0x00)};
  return buildRouteConfig(tx, idx, val, 1);
}

FrameBytes buildDirection(bool reverse, uint8_t tx) {
  const int idx[1] = {kRouteDirectionSlot};
  const uint8_t val[1] = {static_cast<uint8_t>(reverse ? 0x01 : 0x00)};
  return buildRouteConfig(tx, idx, val, 1);
}

FrameBytes buildTimingQuery(uint8_t tx) {
  const uint8_t data[1] = {tx};
  return encodeFrame(0x06, 0x08, data, 1);
}

FrameBytes buildManualTracking(bool enabled, uint8_t tx) {
  // Confirmed: disable writes 0x01; enable is the inferred 0x00 counterpart.
  const uint8_t data[2] = {tx, static_cast<uint8_t>(enabled ? 0x00 : 0x01)};
  return encodeFrame(0x03, 0x02, data, sizeof(data));
}

FrameBytes buildMotionVector(int slideVelocity, int panVelocity, uint8_t tx) {
  const int slide = clampMotionVelocity(slideVelocity);
  const int pan = clampMotionVelocity(panVelocity);
  const uint8_t data[7] = {
      tx,
      static_cast<uint8_t>(static_cast<int8_t>(slide)),
      static_cast<uint8_t>(static_cast<int8_t>(pan)),
      0x00,
      0x00,
      0x00,
      0x00,
  };
  return encodeFrame(0x03, 0x04, data, sizeof(data));
}

bool patchTimingTable(const uint8_t* table, size_t tableLen, int slotIndex, int speed,
                      int holdSeconds, uint8_t tx, FrameBytes& out) {
  if (table == nullptr || tableLen != kTimingDataLen) {
    return false;
  }
  if (slotIndex <= 0 || slotIndex >= kKeypointCount) {
    // A (index 0) has no travel segment; out-of-range rejected too.
    return false;
  }
  if (speed < 0 && holdSeconds < 0) {
    return false;
  }
  if (speed > 100 || holdSeconds > 255) {
    return false;
  }

  uint8_t data[kTimingDataLen];
  memcpy(data, table, kTimingDataLen);
  data[0] = tx;

  const int segment = slotIndex - 1;  // B->0, C->1, ...
  const int base = 1 + segment * 4;
  if (speed >= 0) {
    data[base + kTimingSpeedOffset] = static_cast<uint8_t>(speed);
  }
  if (holdSeconds >= 0) {
    data[base + kTimingHoldOffset] = static_cast<uint8_t>(holdSeconds);
  }

  out = encodeFrame(0x03, 0x08, data, kTimingDataLen);
  return out.len > 0;
}

bool isRunProgress(const ParsedFrame& frame) {
  return frame.family == 0x16 && frame.code == 0x10 && frame.dataLen == kRunProgressDataLen;
}

bool parseRunProgress(const ParsedFrame& frame, RunProgress& out) {
  if (!isRunProgress(frame) || frame.data == nullptr) {
    return false;
  }
  const uint8_t* d = frame.data;
  out.sequence = d[0];
  out.stateCode = d[2];
  out.segment = d[3];
  out.stage = d[4];
  out.progressUnits = (static_cast<uint16_t>(d[5]) << 8) | d[6];
  uint16_t clamped = out.progressUnits;
  if (clamped > 1000) {
    clamped = 1000;
  }
  out.progressPercent = clamped / 10.0f;
  out.stepIndex = d[10];
  return true;
}

const char* runStateLabel(uint8_t stateCode) {
  switch (stateCode) {
    case kRunStop:
      return "stopped";
    case kRunStandby:
      return "standby";
    case kRunStart:
      return "running";
    case 0x06:
      return "preview";
    default:
      return "unknown";
  }
}

}  // namespace shark
