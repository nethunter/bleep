#include "core/device_manager.h"

#include "core/preferences_store.h"
#include "driver_config.h"
#if CONFIG_DRIVER_CANON_BLE
#include "devices/canon_ble/driver.h"
#endif
#if CONFIG_DRIVER_CANON_TRIGGER
#include "devices/canon_trigger/driver.h"
#endif
#if CONFIG_DRIVER_SHARK_NANO_II
#include "devices/shark_nano_ii/driver.h"
#endif
#if CONFIG_DRIVER_TASCAM_X8
#include "devices/tascam_x8/driver.h"
#endif
#if CONFIG_DRIVER_HOME_ASSISTANT
#include "devices/home_assistant/driver.h"
#endif
#if CONFIG_DRIVER_AMARAN_LIGHT
#include "devices/amaran_light/driver.h"
#endif
#if CONFIG_DRIVER_ZHIYUN_X100
#include "devices/zhiyun_x100/driver.h"
#endif

namespace studio {

DeviceManager& devices() {
  static PreferencesConfigBackend backend;
  static PreferencesLegacySharkBackend legacyBackend;
#if CONFIG_DRIVER_SHARK_NANO_II
  static SharkDriver sharkDriver;
#endif
#if CONFIG_DRIVER_CANON_TRIGGER
  static CanonTriggerDriver canonTriggerDriver;
#endif
#if CONFIG_DRIVER_CANON_BLE
  static CanonBleDriver canonBleDriver;
#endif
#if CONFIG_DRIVER_TASCAM_X8
  static TascamX8Driver tascamX8Driver;
#endif
#if CONFIG_DRIVER_HOME_ASSISTANT
  static HomeAssistantDriver homeAssistantDriver;
#endif
#if CONFIG_DRIVER_AMARAN_LIGHT
  static AmaranLightDriver amaranLight(DriverId::AmaranLight);
  static AmaranLightDriver amaranPano120c(DriverId::AmaranPano120c);
  static AmaranLightDriver amaranAce25c(DriverId::AmaranAce25c);
#endif
#if CONFIG_DRIVER_ZHIYUN_X100
  static ZhiyunLightDriver zhiyunLightDriver;
#endif
  static DeviceDriver* drivers[] = {
#if CONFIG_DRIVER_SHARK_NANO_II
      &sharkDriver,
#endif
#if CONFIG_DRIVER_CANON_TRIGGER
      &canonTriggerDriver,
#endif
#if CONFIG_DRIVER_CANON_BLE
      &canonBleDriver,
#endif
#if CONFIG_DRIVER_TASCAM_X8
      &tascamX8Driver,
#endif
#if CONFIG_DRIVER_HOME_ASSISTANT
      &homeAssistantDriver,
#endif
#if CONFIG_DRIVER_AMARAN_LIGHT
      &amaranLight,
      &amaranPano120c,
      &amaranAce25c,
#endif
#if CONFIG_DRIVER_ZHIYUN_X100
      &zhiyunLightDriver,
#endif
      nullptr,
  };
  static DeviceManager manager(
      backend, legacyBackend, drivers,
      (sizeof(drivers) / sizeof(drivers[0])) - 1);
  return manager;
}

}  // namespace studio
