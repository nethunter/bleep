#pragma once

#include <cstddef>
#include <cstdint>

namespace studio {

constexpr size_t kPanelIdentityCapacity = 17;
constexpr size_t kPanelSetupSsidCapacity = 31;

void formatPanelIdentity(uint64_t hardwareId,
                         char (&out)[kPanelIdentityCapacity]);
void formatPanelSetupSsid(uint64_t hardwareId,
                          char (&out)[kPanelSetupSsidCapacity]);

}  // namespace studio
