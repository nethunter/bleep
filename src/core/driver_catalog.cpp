#include "core/driver_catalog.h"

#include "driver_config.h"

namespace studio {

namespace {

#if CONFIG_DRIVER_SHARK_NANO_II
constexpr DriverDescriptor kDrivers[] = {
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
};
constexpr size_t kDriverCount = sizeof(kDrivers) / sizeof(kDrivers[0]);
#else
constexpr DriverDescriptor kDrivers[1] = {};
constexpr size_t kDriverCount = 0;
#endif

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

