#pragma once

#include "core/config_store.h"

namespace studio {

class PreferencesConfigBackend : public IConfigBackend {
 public:
  size_t read(uint8_t* destination, size_t capacity) override;
  bool write(const uint8_t* data, size_t length) override;
};

class PreferencesScenesBackend : public IConfigBackend {
 public:
  size_t read(uint8_t* destination, size_t capacity) override;
  bool write(const uint8_t* data, size_t length) override;
};

class PreferencesHomeAssistantBackend : public IConfigBackend {
 public:
  size_t read(uint8_t* destination, size_t capacity) override;
  bool write(const uint8_t* data, size_t length) override;
};

class PreferencesAmaranBackend : public IConfigBackend {
 public:
  size_t read(uint8_t* destination, size_t capacity) override;
  bool write(const uint8_t* data, size_t length) override;
};

// Shared mesh consumers retain the existing NVS key for schema compatibility.
using PreferencesMeshBackend = PreferencesAmaranBackend;

class PreferencesLegacySharkBackend : public ILegacySharkBackend {
 public:
  bool readLegacyShark(LegacySharkConfig& config) override;
};

}  // namespace studio
