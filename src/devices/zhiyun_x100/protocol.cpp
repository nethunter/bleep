#include "devices/zhiyun_x100/protocol.h"

#include <cmath>
#include <cstring>

namespace zhiyun_x100 {
namespace {

void put16(uint8_t* destination, uint16_t value) {
  destination[0] = static_cast<uint8_t>(value);
  destination[1] = static_cast<uint8_t>(value >> 8);
}

uint16_t get16(const uint8_t* source) {
  return static_cast<uint16_t>(source[0]) |
         static_cast<uint16_t>(source[1]) << 8;
}

FrameBytes buildStateWrite(uint16_t sequence, uint16_t command,
                           const uint8_t* value, size_t valueLength) {
  uint8_t payload[7] = {0x00, 0x80, 0x01};
  if (value == nullptr || valueLength > sizeof(payload) - 3) {
    return {};
  }
  std::memcpy(payload + 3, value, valueLength);
  return buildRequest(sequence, command, payload, valueLength + 3);
}

bool statePayload(const ParsedFrame& frame, uint16_t command,
                  size_t valueLength, const uint8_t*& value) {
  if (!frame.response || frame.command != command ||
      frame.payloadLength != valueLength + 3 || frame.payload == nullptr ||
      frame.payload[0] != 0x00 || frame.payload[1] != 0x80 ||
      frame.payload[2] != 0x00) {
    return false;
  }
  value = frame.payload + 3;
  return true;
}

}  // namespace

uint16_t crc16Xmodem(const uint8_t* data, size_t length) {
  uint16_t crc = 0;
  for (size_t i = 0; i < length; ++i) {
    crc ^= static_cast<uint16_t>(data[i]) << 8;
    for (uint8_t bit = 0; bit < 8; ++bit) {
      crc = (crc & 0x8000u) != 0
                ? static_cast<uint16_t>((crc << 1) ^ 0x1021u)
                : static_cast<uint16_t>(crc << 1);
    }
  }
  return crc;
}

FrameBytes buildRequest(uint16_t sequence, uint16_t command,
                        const uint8_t* payload, size_t payloadLength) {
  FrameBytes frame;
  const size_t bodyLength = 6 + payloadLength;
  frame.length = bodyLength + 6;
  if (frame.length > sizeof(frame.bytes) ||
      (payloadLength > 0 && payload == nullptr)) {
    return {};
  }
  frame.bytes[0] = 0x24;
  frame.bytes[1] = 0x3c;
  put16(frame.bytes + 2, static_cast<uint16_t>(bodyLength));
  frame.bytes[4] = 0x00;
  frame.bytes[5] = 0x01;
  put16(frame.bytes + 6, sequence);
  put16(frame.bytes + 8, command);
  if (payloadLength > 0) {
    std::memcpy(frame.bytes + 10, payload, payloadLength);
  }
  put16(frame.bytes + 10 + payloadLength,
        crc16Xmodem(frame.bytes + 4, bodyLength));
  return frame;
}

FrameBytes buildReadRequest(uint16_t sequence, uint16_t command) {
  uint8_t payload[7] = {0x00, 0x80, 0x00};
  size_t valueLength = 0;
  if (command == kCommandBrightness) valueLength = 4;
  else if (command == kCommandCct) valueLength = 2;
  else if (command == kCommandPower) valueLength = 1;
  else return {};
  return buildRequest(sequence, command, payload, valueLength + 3);
}

FrameBytes buildPowerWrite(uint16_t sequence, bool on) {
  const uint8_t value = on ? 1 : 0;
  return buildStateWrite(sequence, kCommandPower, &value, 1);
}

FrameBytes buildBrightnessWrite(uint16_t sequence, float percent) {
  if (!std::isfinite(percent) || percent < 0.0f || percent > 100.0f) return {};
  uint8_t value[4];
  std::memcpy(value, &percent, sizeof(value));
  return buildStateWrite(sequence, kCommandBrightness, value, sizeof(value));
}

uint16_t normalizeCct(uint16_t kelvin) {
  if (kelvin <= kMinKelvin) return kMinKelvin;
  if (kelvin >= kMaxKelvin) return kMaxKelvin;
  return static_cast<uint16_t>(
      ((kelvin + kCctStepKelvin / 2) / kCctStepKelvin) * kCctStepKelvin);
}

FrameBytes buildCctWrite(uint16_t sequence, uint16_t kelvin) {
  if (kelvin < kMinKelvin || kelvin > kMaxKelvin) return {};
  uint8_t value[2];
  put16(value, kelvin);
  return buildStateWrite(sequence, kCommandCct, value, sizeof(value));
}

bool parseBrightness(const ParsedFrame& frame, float& percent) {
  const uint8_t* value = nullptr;
  if (!statePayload(frame, kCommandBrightness, 4, value)) return false;
  std::memcpy(&percent, value, sizeof(percent));
  return std::isfinite(percent) && percent >= 0.0f && percent <= 100.0f;
}

bool parseCct(const ParsedFrame& frame, uint16_t& kelvin) {
  const uint8_t* value = nullptr;
  if (!statePayload(frame, kCommandCct, 2, value)) return false;
  kelvin = get16(value);
  return kelvin >= kMinKelvin && kelvin <= kMaxKelvin;
}

bool parsePower(const ParsedFrame& frame, bool& on) {
  const uint8_t* value = nullptr;
  if (!statePayload(frame, kCommandPower, 1, value) || value[0] > 1) return false;
  on = value[0] != 0;
  return true;
}

bool identityIsX100(const ParsedFrame& frame) {
  if (!frame.response || frame.command != kCommandIdentity ||
      frame.payload == nullptr || frame.payloadLength < 5) return false;
  constexpr char marker[] = "pl105";
  for (size_t i = 0; i + sizeof(marker) - 1 <= frame.payloadLength; ++i) {
    bool match = true;
    for (size_t j = 0; j < sizeof(marker) - 1; ++j) {
      uint8_t value = frame.payload[i + j];
      if (value >= 'A' && value <= 'Z') value = static_cast<uint8_t>(value + 32);
      if (value != static_cast<uint8_t>(marker[j])) {
        match = false;
        break;
      }
    }
    if (match) return true;
  }
  return false;
}

void FrameScanner::feed(const uint8_t* data, size_t length,
                        const Handler& handler) {
  if (data == nullptr || !handler) return;
  for (size_t i = 0; i < length; ++i) {
    if (length_ == 0 && data[i] != 0x24) continue;
    if (length_ == 1 && data[i] != 0x3c) {
      length_ = data[i] == 0x24 ? 1 : 0;
      continue;
    }
    if (length_ >= sizeof(bytes_)) {
      length_ = 0;
      continue;
    }
    bytes_[length_++] = data[i];
    process(handler);
  }
}

void FrameScanner::process(const Handler& handler) {
  if (length_ < 4) return;
  const size_t total = static_cast<size_t>(get16(bytes_ + 2)) + 6;
  if (total < 12 || total > sizeof(bytes_)) {
    length_ = 0;
    return;
  }
  if (length_ < total) return;
  const size_t bodyLength = total - 6;
  if (crc16Xmodem(bytes_ + 4, bodyLength) == get16(bytes_ + total - 2)) {
    ParsedFrame frame;
    frame.response = bytes_[4] == 0x01 && bytes_[5] == 0x00;
    frame.sequence = get16(bytes_ + 6);
    frame.command = get16(bytes_ + 8);
    frame.payload = bytes_ + 10;
    frame.payloadLength = bodyLength - 6;
    handler(frame);
  }
  const size_t remaining = length_ - total;
  if (remaining > 0) std::memmove(bytes_, bytes_ + total, remaining);
  length_ = remaining;
  if (remaining > 0) process(handler);
}

}  // namespace zhiyun_x100
