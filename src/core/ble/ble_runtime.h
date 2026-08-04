#pragma once

#include <cstdint>

#include "core/ble/ble_central.h"

namespace studio::ble {

BleCentral& bleCentral();
void loopBleRuntime(uint32_t nowMs);

}  // namespace studio::ble
