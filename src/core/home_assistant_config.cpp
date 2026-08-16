#include "core/home_assistant_config.h"

#include <cstring>

#include "core/blob_codec.h"

namespace studio {
namespace {

constexpr uint8_t kMagic[] = {'H', 'A', 'C', 'F'};
constexpr uint16_t kVersion = 1;
constexpr size_t kBodySize = 1 + kWifiSsidCapacity + kWifiPasswordCapacity +
                             kHomeAssistantUrlCapacity +
                             kHomeAssistantTokenCapacity;
constexpr size_t kEncodedSize = sizeof(kMagic) + 2 + kBodySize + 4;

}  // namespace

bool validLocalHomeAssistantUrl(const char* url) {
  if (url == nullptr || std::strncmp(url, "http://", 7) != 0) {
    return false;
  }
  const char* host = url + 7;
  if (*host == '\0' || std::strchr(host, ' ') != nullptr ||
      std::strchr(host, '\n') != nullptr || std::strchr(host, '\r') != nullptr) {
    return false;
  }
  if (std::strpbrk(host, "?#") != nullptr) return false;
  const char* path = std::strchr(host, '/');
  return path == nullptr || std::strcmp(path, "/") == 0;
}

ConfigLoadStatus HomeAssistantConfigStore::load(HomeAssistantConfig& config) {
  uint8_t bytes[kEncodedSize] = {};
  const size_t length = backend_.read(bytes, sizeof(bytes));
  if (length == 0) {
    config = {};
    return ConfigLoadStatus::Missing;
  }
  if (length != sizeof(bytes) ||
      std::memcmp(bytes, kMagic, sizeof(kMagic)) != 0) {
    config = {};
    return ConfigLoadStatus::Corrupt;
  }
  BlobReader reader(bytes + sizeof(kMagic), sizeof(bytes) - sizeof(kMagic));
  uint16_t version = 0;
  if (!reader.u16(version) || version != kVersion) {
    config = {};
    return ConfigLoadStatus::Corrupt;
  }
  BlobReader checksumReader(bytes + sizeof(bytes) - 4, 4);
  uint32_t storedChecksum = 0;
  if (!checksumReader.u32(storedChecksum) ||
      storedChecksum != fnv1a(bytes, sizeof(bytes) - 4)) {
    config = {};
    return ConfigLoadStatus::Corrupt;
  }
  uint8_t homeAssistantConfigured = 0;
  if (!reader.u8(homeAssistantConfigured) || homeAssistantConfigured > 1 ||
      !reader.text(config.wifiSsid) || !reader.text(config.wifiPassword) ||
      !reader.text(config.baseUrl) || !reader.text(config.token)) {
    config = {};
    return ConfigLoadStatus::Corrupt;
  }
  config.wifiConfigured = config.wifiSsid[0] != '\0';
  config.homeAssistantConfigured = homeAssistantConfigured != 0;
  if ((config.wifiConfigured && config.wifiSsid[0] == '\0') ||
      (config.homeAssistantConfigured &&
       (config.token[0] == '\0' || !validLocalHomeAssistantUrl(config.baseUrl)))) {
    config = {};
    return ConfigLoadStatus::Corrupt;
  }
  return ConfigLoadStatus::Loaded;
}

bool HomeAssistantConfigStore::save(const HomeAssistantConfig& config) {
  if ((config.wifiConfigured && config.wifiSsid[0] == '\0') ||
      (config.homeAssistantConfigured &&
       (config.token[0] == '\0' || !validLocalHomeAssistantUrl(config.baseUrl)))) {
    return false;
  }
  uint8_t bytes[kEncodedSize] = {};
  BlobWriter writer(bytes, sizeof(bytes));
  writer.bytes(kMagic, sizeof(kMagic));
  writer.u16(kVersion);
  writer.u8(config.homeAssistantConfigured ? 1 : 0);
  writer.bytes(config.wifiSsid, sizeof(config.wifiSsid));
  writer.bytes(config.wifiPassword, sizeof(config.wifiPassword));
  writer.bytes(config.baseUrl, sizeof(config.baseUrl));
  writer.bytes(config.token, sizeof(config.token));
  writer.u32(fnv1a(bytes, sizeof(bytes) - 4));
  return writer.valid() && writer.size() == sizeof(bytes) &&
         backend_.write(bytes, sizeof(bytes));
}

bool HomeAssistantConfigStore::clear() {
  HomeAssistantConfig config;
  return save(config);
}

}  // namespace studio
