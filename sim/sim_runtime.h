#pragma once

#include "devices/shark_nano_ii/state.h"

namespace studio {

// Host-only helpers for the LVGL UI simulator.
shark::SharkState& simSharkState();
void simSetConnectedDemoState();
void simSetScanningState();

}  // namespace studio
