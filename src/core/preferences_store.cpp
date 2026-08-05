#include "core/preferences_store.h"

#include <Preferences.h>

#include <cstring>

namespace studio {

size_t PreferencesConfigBackend::read(uint8_t* destination, size_t capacity) {
  Preferences preferences;
  if (!preferences.begin("studio", true)) {
    return 0;
  }
  const size_t length = preferences.getBytesLength("devices");
  if (length == 0 || length > capacity) {
    preferences.end();
    return 0;
  }
  const size_t readLength = preferences.getBytes("devices", destination, capacity);
  preferences.end();
  return readLength;
}

bool PreferencesConfigBackend::write(const uint8_t* data, size_t length) {
  Preferences preferences;
  if (!preferences.begin("studio", false)) {
    return false;
  }
  const bool written = preferences.putBytes("devices", data, length) == length;
  preferences.end();
  return written;
}

size_t PreferencesScenesBackend::read(uint8_t* destination, size_t capacity) {
  Preferences preferences;
  if (!preferences.begin("studio", true)) {
    return 0;
  }
  const size_t length = preferences.getBytesLength("scenes");
  if (length == 0 || length > capacity) {
    preferences.end();
    return 0;
  }
  const size_t readLength = preferences.getBytes("scenes", destination, capacity);
  preferences.end();
  return readLength;
}

bool PreferencesScenesBackend::write(const uint8_t* data, size_t length) {
  Preferences preferences;
  if (!preferences.begin("studio", false)) {
    return false;
  }
  const bool written = preferences.putBytes("scenes", data, length) == length;
  preferences.end();
  return written;
}

size_t PreferencesHomeAssistantBackend::read(uint8_t* destination,
                                             size_t capacity) {
  Preferences preferences;
  if (!preferences.begin("studio", true)) {
    return 0;
  }
  const size_t length = preferences.getBytesLength("ha_config");
  const size_t read = length > 0 && length <= capacity
                          ? preferences.getBytes("ha_config", destination, length)
                          : 0;
  preferences.end();
  return read;
}

bool PreferencesHomeAssistantBackend::write(const uint8_t* data,
                                             size_t length) {
  Preferences preferences;
  if (!preferences.begin("studio", false)) {
    return false;
  }
  const bool ok = preferences.putBytes("ha_config", data, length) == length;
  preferences.end();
  return ok;
}

size_t PreferencesAmaranBackend::read(uint8_t* destination, size_t capacity) {
  Preferences preferences;
  if (!preferences.begin("studio", true)) return 0;
  const size_t length = preferences.getBytesLength("amaran_mesh");
  const size_t read = length > 0 && length <= capacity
                          ? preferences.getBytes("amaran_mesh", destination, length)
                          : 0;
  preferences.end();
  return read;
}

bool PreferencesAmaranBackend::write(const uint8_t* data, size_t length) {
  Preferences preferences;
  if (!preferences.begin("studio", false)) return false;
  const bool ok = preferences.putBytes("amaran_mesh", data, length) == length;
  preferences.end();
  return ok;
}

bool PreferencesLegacySharkBackend::readLegacyShark(LegacySharkConfig& config) {
  config = LegacySharkConfig{};
  Preferences preferences;
  if (!preferences.begin("shark", true)) {
    return false;
  }
  const String address = preferences.getString("addr", "");
  if (address.length() == 0) {
    preferences.end();
    return false;
  }
  const String name = preferences.getString("name", "Shark Nano II");
  config.addressType = preferences.getUChar("atype", 0);
  preferences.end();

  std::strncpy(config.address, address.c_str(), sizeof(config.address) - 1);
  std::strncpy(config.advertisedName, name.c_str(), sizeof(config.advertisedName) - 1);
  config.paired = true;
  return true;
}

}  // namespace studio
