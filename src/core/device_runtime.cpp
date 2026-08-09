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
#if CONFIG_DRIVER_APUTURE_LIGHT
#include "devices/aputure_light/driver.h"
#endif
#if CONFIG_DRIVER_ZHIYUN_X100
#include "devices/zhiyun_x100/driver.h"
#endif
#if CONFIG_DRIVER_GOPRO
#include "devices/gopro/driver.h"
#endif
#if CONFIG_DRIVER_INSTA360
#include "devices/insta360/driver.h"
#endif
#if CONFIG_DRIVER_DJI_OSMO
#include "devices/dji_osmo/driver.h"
#endif
#if CONFIG_DRIVER_ACTION_CAMERA_RESEARCH
#include "devices/action_camera_research/driver.h"
#endif
#if CONFIG_DRIVER_PHONE_CAMERA
#include "devices/phone_camera/driver.h"
#endif

namespace studio {

// A compiled driver is a flash-backed factory shell, not a session pool.
// Keep this guard beside the globals so adding mutable runtime state fails the
// build before it quietly consumes the full profile's static RAM.
#if CONFIG_DRIVER_SHARK_NANO_II
static_assert(sizeof(SharkDriver) <= 64, "Shark driver shell must stay dormant");
#endif
#if CONFIG_DRIVER_CANON_TRIGGER
static_assert(sizeof(CanonTriggerDriver) <= 64,
              "Canon Trigger driver shell must stay dormant");
#endif
#if CONFIG_DRIVER_CANON_BLE
static_assert(sizeof(CanonBleDriver) <= 64,
              "Canon Smart driver shell must stay dormant");
#endif
#if CONFIG_DRIVER_TASCAM_X8
static_assert(sizeof(TascamX8Driver) <= 64,
              "Tascam driver shell must stay dormant");
#endif
#if CONFIG_DRIVER_HOME_ASSISTANT
static_assert(sizeof(HomeAssistantDriver) <= 64,
              "Home Assistant driver shell must stay dormant");
#endif
#if CONFIG_DRIVER_APUTURE_LIGHT
static_assert(sizeof(AputureLightDriver) <= 64,
              "Aputure Light driver shell must stay dormant");
#endif
#if CONFIG_DRIVER_ZHIYUN_X100
static_assert(sizeof(ZhiyunLightDriver) <= 64,
              "Zhiyun driver shell must stay dormant");
#endif
#if CONFIG_DRIVER_GOPRO
static_assert(sizeof(GoProDriver) <= 64, "GoPro driver shell must stay dormant");
#endif
#if CONFIG_DRIVER_INSTA360
static_assert(sizeof(Insta360Driver) <= 64, "Insta360 driver shell must stay dormant");
#endif
#if CONFIG_DRIVER_DJI_OSMO
static_assert(sizeof(DjiOsmoDriver) <= 64, "DJI Osmo driver shell must stay dormant");
#endif
#if CONFIG_DRIVER_ACTION_CAMERA_RESEARCH
static_assert(sizeof(ActionCameraResearchDriver) <= 64,
              "Research driver shell must stay dormant");
#endif
#if CONFIG_DRIVER_PHONE_CAMERA
static_assert(sizeof(PhoneCameraDriver) <= 64,
              "Phone Camera driver shell must stay dormant");
#endif

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
#if CONFIG_DRIVER_APUTURE_LIGHT
  static AputureLightDriver aputureLight;
#endif
#if CONFIG_DRIVER_ZHIYUN_X100
  static ZhiyunLightDriver zhiyunLightDriver;
#endif
#if CONFIG_DRIVER_GOPRO
  static GoProDriver goProDriver;
#endif
#if CONFIG_DRIVER_INSTA360
  static Insta360Driver insta360Driver;
#endif
#if CONFIG_DRIVER_DJI_OSMO
  static DjiOsmoDriver djiOsmoDriver;
#endif
#if CONFIG_DRIVER_ACTION_CAMERA_RESEARCH
  static ActionCameraResearchDriver sonyCameraDriver(DriverId::SonyCamera);
#endif
#if CONFIG_DRIVER_PHONE_CAMERA
  static PhoneCameraDriver phoneCameraDriver;
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
#if CONFIG_DRIVER_APUTURE_LIGHT
      &aputureLight,
#endif
#if CONFIG_DRIVER_ZHIYUN_X100
      &zhiyunLightDriver,
#endif
#if CONFIG_DRIVER_GOPRO
      &goProDriver,
#endif
#if CONFIG_DRIVER_INSTA360
      &insta360Driver,
#endif
#if CONFIG_DRIVER_DJI_OSMO
      &djiOsmoDriver,
#endif
#if CONFIG_DRIVER_ACTION_CAMERA_RESEARCH
      &sonyCameraDriver,
#endif
#if CONFIG_DRIVER_PHONE_CAMERA
      &phoneCameraDriver,
#endif
      nullptr,
  };
  static_assert((sizeof(drivers) / sizeof(drivers[0])) - 1 <=
                    DeviceManager::kMaxCompiledDrivers,
                "compiled driver table exceeds DeviceManager capacity");
  static DeviceManager manager(
      backend, legacyBackend, drivers,
      (sizeof(drivers) / sizeof(drivers[0])) - 1);
  return manager;
}

}  // namespace studio
