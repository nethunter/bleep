#include "core/home_assistant_config.h"

#include <cstring>

namespace studio {
namespace {

constexpr uint8_t kMagic[] = {'H', 'A', 'C', 'F'};
constexpr uint16_t kVersion = 1;
constexpr size_t kBodySize = 1 + kWifiSsidCapacity + kWifiPasswordCapacity +
                             kHomeAssistantUrlCapacity +
                             kHomeAssistantTokenCapacity;
constexpr size_t kEncodedSize = sizeof(kMagic) + 2 + kBodySize + 4;

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
    *out++ = static_cast<uint8_t>((value >> shift) & 0xff);
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

template <size_t N>
void copyText(char (&destination)[N], const uint8_t*& cursor) {
  std::memcpy(destination, cursor, N);
  destination[N - 1] = '\0';
  cursor += N;
}

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
  const uint8_t* cursor = bytes + sizeof(kMagic);
  if (getU16(cursor) != kVersion) {
    config = {};
    return ConfigLoadStatus::Corrupt;
  }
  const uint8_t* checksumCursor = bytes + sizeof(bytes) - 4;
  if (getU32(checksumCursor) != checksum(bytes, sizeof(bytes) - 4)) {
    config = {};
    return ConfigLoadStatus::Corrupt;
  }
  config.configured = *cursor++ != 0;
  copyText(config.wifiSsid, cursor);
  copyText(config.wifiPassword, cursor);
  copyText(config.baseUrl, cursor);
  copyText(config.token, cursor);
  if (config.configured &&
      (config.wifiSsid[0] == '\0' || config.token[0] == '\0' ||
       !validLocalHomeAssistantUrl(config.baseUrl))) {
    config = {};
    return ConfigLoadStatus::Corrupt;
  }
  return ConfigLoadStatus::Loaded;
}

bool HomeAssistantConfigStore::save(const HomeAssistantConfig& config) {
  if (config.configured &&
      (config.wifiSsid[0] == '\0' || config.token[0] == '\0' ||
       !validLocalHomeAssistantUrl(config.baseUrl))) {
    return false;
  }
  uint8_t bytes[kEncodedSize] = {};
  uint8_t* cursor = bytes;
  std::memcpy(cursor, kMagic, sizeof(kMagic));
  cursor += sizeof(kMagic);
  putU16(cursor, kVersion);
  *cursor++ = config.configured ? 1 : 0;
  std::memcpy(cursor, config.wifiSsid, sizeof(config.wifiSsid));
  cursor += sizeof(config.wifiSsid);
  std::memcpy(cursor, config.wifiPassword, sizeof(config.wifiPassword));
  cursor += sizeof(config.wifiPassword);
  std::memcpy(cursor, config.baseUrl, sizeof(config.baseUrl));
  cursor += sizeof(config.baseUrl);
  std::memcpy(cursor, config.token, sizeof(config.token));
  cursor += sizeof(config.token);
  putU32(cursor, checksum(bytes, sizeof(bytes) - 4));
  return backend_.write(bytes, sizeof(bytes));
}

bool HomeAssistantConfigStore::clear() {
  HomeAssistantConfig config;
  return save(config);
}

}  // namespace studio
