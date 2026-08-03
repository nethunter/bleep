#include "core/device_manager.h"

#include "core/preferences_store.h"
#include "driver_config.h"
#if CONFIG_DRIVER_CANON_BLE
#include "devices/canon_ble/driver.h"
#endif
#if CONFIG_DRIVER_SHARK_NANO_II
#include "devices/shark_nano_ii/driver.h"
#endif

namespace studio {

DeviceManager& devices() {
  static PreferencesConfigBackend backend;
  static PreferencesLegacySharkBackend legacyBackend;
#if CONFIG_DRIVER_SHARK_NANO_II
  static SharkDriver sharkDriver;
#endif
#if CONFIG_DRIVER_CANON_BLE
  static CanonBleDriver canonBleDriver;
#endif
  static DeviceDriver* drivers[] = {
#if CONFIG_DRIVER_SHARK_NANO_II
      &sharkDriver,
#endif
#if CONFIG_DRIVER_CANON_BLE
      &canonBleDriver,
#endif
      nullptr,
  };
  static DeviceManager manager(
      backend, legacyBackend, drivers,
      (sizeof(drivers) / sizeof(drivers[0])) - 1);
  return manager;
}

}  // namespace studio

