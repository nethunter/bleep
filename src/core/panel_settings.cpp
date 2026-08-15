#include "core/panel_settings.h"

#include <cstring>

#include "core/blob_codec.h"

namespace studio {
namespace {

constexpr uint8_t kMagic[] = {'P', 'S', 'E', 'T'};
constexpr uint16_t kVersion = 2;
constexpr size_t kV1EncodedSize = sizeof(kMagic) + 2 + 1 + 4;
constexpr size_t kEncodedSize = sizeof(kMagic) + 2 + 2 + 4;

}  // namespace

ConfigLoadStatus PanelSettingsStore::load(PanelSettings& settings) {
  uint8_t bytes[kEncodedSize] = {};
  const size_t length = backend_.read(bytes, sizeof(bytes));
  if (length == 0) {
    settings = {};
    return ConfigLoadStatus::Missing;
  }
  if ((length != kV1EncodedSize && length != sizeof(bytes)) ||
      std::memcmp(bytes, kMagic, sizeof(kMagic)) != 0) {
    settings = {};
    return ConfigLoadStatus::Corrupt;
  }
  BlobReader reader(bytes + sizeof(kMagic), length - sizeof(kMagic));
  uint16_t version = 0;
  if (!reader.u16(version) ||
      (version == 1 && length != kV1EncodedSize) ||
      (version == kVersion && length != kEncodedSize) ||
      (version != 1 && version != kVersion)) {
    settings = {};
    return ConfigLoadStatus::Corrupt;
  }
  uint8_t enabled = 0;
  uint8_t channel = 0;
  BlobReader checksumReader(bytes + length - 4, 4);
  uint32_t storedChecksum = 0;
  if (!reader.u8(enabled) || enabled > 1 ||
      (version >= 2 && !reader.u8(channel)) ||
      channel > static_cast<uint8_t>(FirmwareUpdateChannel::Development) ||
      !checksumReader.u32(storedChecksum) ||
      storedChecksum != fnv1a(bytes, length - 4)) {
    settings = {};
    return ConfigLoadStatus::Corrupt;
  }
  settings.hapticEnabled = enabled != 0;
  settings.firmwareUpdateChannel =
      static_cast<FirmwareUpdateChannel>(channel);
  return ConfigLoadStatus::Loaded;
}

bool PanelSettingsStore::save(const PanelSettings& settings) {
  uint8_t bytes[kEncodedSize] = {};
  BlobWriter writer(bytes, sizeof(bytes));
  writer.bytes(kMagic, sizeof(kMagic));
  writer.u16(kVersion);
  writer.u8(settings.hapticEnabled ? 1 : 0);
  writer.u8(static_cast<uint8_t>(settings.firmwareUpdateChannel));
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

bool PanelSettingsService::setFirmwareUpdateChannel(
    FirmwareUpdateChannel channel) {
  if (!begun_) begin();
  if (settings_.firmwareUpdateChannel == channel) return true;
  PanelSettings next = settings_;
  next.firmwareUpdateChannel = channel;
  if (!store_.save(next)) return false;
  settings_ = next;
  return true;
}

}  // namespace studio
