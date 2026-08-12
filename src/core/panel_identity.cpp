#include "core/panel_identity.h"

#include <cstdio>

namespace studio {

namespace {

constexpr uint64_t kHardwareIdMask = 0x0000FFFFFFFFFFFFULL;

}  // namespace

void formatPanelIdentity(uint64_t hardwareId,
                         char (&out)[kPanelIdentityCapacity]) {
  std::snprintf(out, sizeof(out), "BLP-%012llX",
                static_cast<unsigned long long>(hardwareId & kHardwareIdMask));
}

void formatPanelSetupSsid(uint64_t hardwareId,
                          char (&out)[kPanelSetupSsidCapacity]) {
  std::snprintf(out, sizeof(out), "Bleep-Setup-%05llX",
                static_cast<unsigned long long>(hardwareId & 0xFFFFFULL));
}

}  // namespace studio
