#pragma once

#include "devices/canon_ble/state.h"
#include "devices/shark_nano_ii/state.h"

namespace studio {

// Host-only helpers for the LVGL UI simulator.
shark::SharkState& simSharkState();
canon_ble::CanonBleState& simCanonState();
void simSetConnectedDemoState();
void simSetScanningState();
void simSetCanonConnectedState();

}  // namespace studio
