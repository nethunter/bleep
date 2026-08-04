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

