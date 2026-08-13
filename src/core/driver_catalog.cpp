#include "core/driver_catalog.h"

#include "driver_config.h"

namespace studio {

namespace {

constexpr DriverDescriptor kDrivers[] = {
#if CONFIG_DRIVER_SHARK_NANO_II
    {
        DriverId::SharkNanoII,
        "ifootage.shark_nano_ii",
        "iFootage",
        "Shark Nano II",
        DeviceType::Motion,
        capabilityBit(Capability::Link) | capabilityBit(Capability::Battery) |
            capabilityBit(Capability::Keypoints) | capabilityBit(Capability::Timing) |
            capabilityBit(Capability::ManualMotion) |
            capabilityBit(Capability::RunControl) |
            capabilityBit(Capability::RunProgress) | capabilityBit(Capability::Loop) |
            capabilityBit(Capability::Direction),
        1,
    },
#endif
#if CONFIG_DRIVER_CANON_TRIGGER
    {
        DriverId::CanonTrigger,
        "canon.eos_r6.trigger",
        "Canon",
        "Canon (Trigger)",
        DeviceType::Camera,
        capabilityBit(Capability::Link) |
            capabilityBit(Capability::RecordTrigger),
        3,
    },
#endif
#if CONFIG_DRIVER_CANON_BLE
    {
        DriverId::CanonBle,
        "canon.eos_r6.smartphone_ble",
        "Canon",
        "Canon (Smart)",
        DeviceType::Camera,
        capabilityBit(Capability::Link) |
            capabilityBit(Capability::RecordStart) |
            capabilityBit(Capability::RecordStop) |
            capabilityBit(Capability::RecordingState),
        3,
    },
#endif
#if CONFIG_DRIVER_TASCAM_X8
    {
        DriverId::TascamX8,
        "tascam.portacapture_x8",
        "Tascam",
        "Portacapture X8",
        DeviceType::Recorder,
        capabilityBit(Capability::Link) |
            capabilityBit(Capability::RecordStart) |
            capabilityBit(Capability::RecordStop) |
            capabilityBit(Capability::RecordingState),
        1,
    },
#endif
#if CONFIG_DRIVER_HOME_ASSISTANT
    {
        DriverId::HomeAssistant,
        "home_assistant.entity",
        "Home Assistant",
        "Entity",
        DeviceType::Action,
        capabilityBit(Capability::Link),
        4,
    },
#endif
#if CONFIG_DRIVER_APUTURE_LIGHT
    {
        DriverId::AputureLight,
        "aputure.light",
        "Aputure",
        "Aputure Light",
        DeviceType::Light,
        capabilityBit(Capability::Link) |
            capabilityBit(Capability::TurnOn) |
            capabilityBit(Capability::TurnOff) |
            capabilityBit(Capability::SetLightCct) |
            capabilityBit(Capability::SetLightRgb) |
            capabilityBit(Capability::SetLightTint),
        4,
    },
#endif
#if CONFIG_DRIVER_ZHIYUN_X100
    {
        DriverId::ZhiyunLight,
        "zhiyun.light",
        "ZHIYUN",
        "Zhiyun Light",
        DeviceType::Light,
        capabilityBit(Capability::Link) | capabilityBit(Capability::TurnOn) |
            capabilityBit(Capability::TurnOff) |
            capabilityBit(Capability::SetLightCct) |
            capabilityBit(Capability::SetLightRgb),
        4,
    },
#endif
#if CONFIG_DRIVER_GOPRO
    {
        DriverId::GoPro,
        "gopro.open_gopro",
        "GoPro",
        "GoPro",
        DeviceType::Camera,
        capabilityBit(Capability::Link) |
            capabilityBit(Capability::RecordStart) |
            capabilityBit(Capability::RecordStop) |
            capabilityBit(Capability::RecordingState),
        4,
    },
#endif
#if CONFIG_DRIVER_INSTA360
    {DriverId::Insta360, "insta360.gps_remote", "Insta360", "Insta360",
     DeviceType::Camera,
     capabilityBit(Capability::Link) |
         capabilityBit(Capability::RecordStart) |
         capabilityBit(Capability::RecordStop) |
         capabilityBit(Capability::RecordingState),
     4},
#endif
#if CONFIG_DRIVER_DJI_OSMO
    {DriverId::DjiOsmo, "dji.osmo_controller", "DJI", "DJI Osmo",
     DeviceType::Camera,
     capabilityBit(Capability::Link) |
         capabilityBit(Capability::Battery) |
         capabilityBit(Capability::RecordStart) |
         capabilityBit(Capability::RecordStop) |
         capabilityBit(Capability::RecordingState),
     4},
#endif
#if CONFIG_DRIVER_ACTION_CAMERA_RESEARCH
    {DriverId::SonyCamera, "sony.camera.research", "Sony", "Sony Camera",
     DeviceType::Camera, capabilityBit(Capability::Link), 4},
#endif
#if CONFIG_DRIVER_PHONE_CAMERA
    {DriverId::PhoneCamera, "phone.camera.hid", "Phone", "Phone Camera",
     DeviceType::Camera,
     capabilityBit(Capability::Link) |
         capabilityBit(Capability::RecordTrigger),
     4},
#endif
    {},
};
constexpr size_t kDriverCount = (sizeof(kDrivers) / sizeof(kDrivers[0])) - 1;

}  // namespace

size_t DriverCatalog::count() { return kDriverCount; }

const DriverDescriptor* DriverCatalog::at(size_t index) {
  return index < kDriverCount ? &kDrivers[index] : nullptr;
}

const DriverDescriptor* DriverCatalog::find(DriverId id) {
  for (size_t i = 0; i < kDriverCount; ++i) {
    if (kDrivers[i].id == id) {
      return &kDrivers[i];
    }
  }
  return nullptr;
}

}  // namespace studio
