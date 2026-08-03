#pragma once

#include "devices/canon_ble/state.h"
#include "devices/shark_nano_ii/state.h"
#include "devices/tascam_x8/state.h"

namespace studio {

// Host-only helpers for the LVGL UI simulator.
shark::SharkState& simSharkState();
canon_ble::CanonBleState& simCanonState();
tascam_x8::TascamX8State& simTascamState();
void simSetConnectedDemoState();
void simSetScanningState();
void simSetCanonConnectedState();
void simSetTascamConnectedState(bool recording);

}  // namespace studio
