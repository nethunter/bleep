#pragma once

#include "core/device_registry.h"

namespace studio {

class IConfigBackend {
 public:
  virtual ~IConfigBackend() = default;
  virtual size_t read(uint8_t* destination, size_t capacity) = 0;
  virtual bool write(const uint8_t* data, size_t length) = 0;
};

enum class ConfigLoadStatus : uint8_t {
  Loaded,
  Missing,
  Corrupt,
};

class ConfigStore {
 public:
  static constexpr uint16_t kSchemaVersion = 2;
  // Header/checksum plus 24 schema-v2 records requires 4,168 bytes.
  static constexpr size_t kMaxBlobSize = 4224;

  explicit ConfigStore(IConfigBackend& backend) : backend_(backend) {}

  ConfigLoadStatus load(DeviceRegistry& registry);
  bool save(const DeviceRegistry& registry);

 private:
  IConfigBackend& backend_;
};

}  // namespace studio
