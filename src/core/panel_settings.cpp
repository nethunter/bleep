#include "core/panel_settings.h"

#include <cstring>

namespace studio {
namespace {

constexpr uint8_t kMagic[] = {'P', 'S', 'E', 'T'};
constexpr uint16_t kVersion = 1;
constexpr size_t kEncodedSize = sizeof(kMagic) + 2 + 1 + 4;

uint32_t checksum(const uint8_t* data, size_t length) {
  uint32_t value = 2166136261u;
  for (size_t i = 0; i < length; ++i) {
    value = (value ^ data[i]) * 16777619u;
  }
  return value;
}

void putU16(uint8_t*& out, uint16_t value) {
  *out++ = static_cast<uint8_t>(value & 0xff);
  *out++ = static_cast<uint8_t>((value >> 8) & 0xff);
}

void putU32(uint8_t*& out, uint32_t value) {
  for (int shift = 0; shift < 32; shift += 8) {
    *out++ = static_cast<uint8_t>(value >> shift);
  }
}

uint16_t getU16(const uint8_t*& in) {
  const uint16_t value = static_cast<uint16_t>(in[0]) |
                         static_cast<uint16_t>(in[1]) << 8;
  in += 2;
  return value;
}

uint32_t getU32(const uint8_t*& in) {
  uint32_t value = 0;
  for (int shift = 0; shift < 32; shift += 8) {
    value |= static_cast<uint32_t>(*in++) << shift;
  }
  return value;
}

}  // namespace

ConfigLoadStatus PanelSettingsStore::load(PanelSettings& settings) {
  uint8_t bytes[kEncodedSize] = {};
  const size_t length = backend_.read(bytes, sizeof(bytes));
  if (length == 0) {
    settings = {};
    return ConfigLoadStatus::Missing;
  }
  if (length != sizeof(bytes) ||
      std::memcmp(bytes, kMagic, sizeof(kMagic)) != 0) {
    settings = {};
    return ConfigLoadStatus::Corrupt;
  }
  const uint8_t* cursor = bytes + sizeof(kMagic);
  if (getU16(cursor) != kVersion) {
    settings = {};
    return ConfigLoadStatus::Corrupt;
  }
  const uint8_t enabled = *cursor++;
  const uint8_t* checksumCursor = bytes + sizeof(bytes) - 4;
  if (enabled > 1 ||
      getU32(checksumCursor) != checksum(bytes, sizeof(bytes) - 4)) {
    settings = {};
    return ConfigLoadStatus::Corrupt;
  }
  settings.hapticEnabled = enabled != 0;
  return ConfigLoadStatus::Loaded;
}

bool PanelSettingsStore::save(const PanelSettings& settings) {
  uint8_t bytes[kEncodedSize] = {};
  uint8_t* cursor = bytes;
  std::memcpy(cursor, kMagic, sizeof(kMagic));
  cursor += sizeof(kMagic);
  putU16(cursor, kVersion);
  *cursor++ = settings.hapticEnabled ? 1 : 0;
  putU32(cursor, checksum(bytes, sizeof(bytes) - 4));
  return backend_.write(bytes, sizeof(bytes));
}

bool PanelSettingsService::begin() {
  if (begun_) return true;
  const ConfigLoadStatus status = store_.load(settings_);
  begun_ = true;
  return status != ConfigLoadStatus::Corrupt;
}

bool PanelSettingsService::setHapticEnabled(bool enabled) {
  if (!begun_) begin();
  if (settings_.hapticEnabled == enabled) return true;
  PanelSettings next = settings_;
  next.hapticEnabled = enabled;
  if (!store_.save(next)) return false;
  settings_ = next;
  return true;
}

}  // namespace studio
