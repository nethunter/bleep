#include "core/device_manager.h"

#include "core/preferences_store.h"
#include "devices/shark_nano_ii/driver.h"

namespace studio {

DeviceManager& devices() {
  static PreferencesConfigBackend backend;
  static PreferencesLegacySharkBackend legacyBackend;
  static SharkDriver sharkDriver;
  static DeviceManager manager(backend, legacyBackend, sharkDriver);
  return manager;
}

}  // namespace studio

