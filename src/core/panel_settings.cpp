#include "core/panel_settings.h"

#include <cstring>

#include "core/blob_codec.h"

namespace studio {
namespace {

constexpr uint8_t kMagic[] = {'P', 'S', 'E', 'T'};
constexpr uint16_t kVersion = 1;
constexpr size_t kEncodedSize = sizeof(kMagic) + 2 + 1 + 4;

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
  BlobReader reader(bytes + sizeof(kMagic), sizeof(bytes) - sizeof(kMagic));
  uint16_t version = 0;
  if (!reader.u16(version) || version != kVersion) {
    settings = {};
    return ConfigLoadStatus::Corrupt;
  }
  uint8_t enabled = 0;
  BlobReader checksumReader(bytes + sizeof(bytes) - 4, 4);
  uint32_t storedChecksum = 0;
  if (!reader.u8(enabled) || enabled > 1 ||
      !checksumReader.u32(storedChecksum) ||
      storedChecksum != fnv1a(bytes, sizeof(bytes) - 4)) {
    settings = {};
    return ConfigLoadStatus::Corrupt;
  }
  settings.hapticEnabled = enabled != 0;
  return ConfigLoadStatus::Loaded;
}

bool PanelSettingsStore::save(const PanelSettings& settings) {
  uint8_t bytes[kEncodedSize] = {};
  BlobWriter writer(bytes, sizeof(bytes));
  writer.bytes(kMagic, sizeof(kMagic));
  writer.u16(kVersion);
  writer.u8(settings.hapticEnabled ? 1 : 0);
  writer.u32(fnv1a(bytes, sizeof(bytes) - 4));
  return writer.valid() && writer.size() == sizeof(bytes) &&
         backend_.write(bytes, sizeof(bytes));
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
