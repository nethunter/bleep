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

