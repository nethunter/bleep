#pragma once

#include "devices/canon_ble/state.h"
#include "devices/canon_trigger/state.h"
#include "devices/shark_nano_ii/state.h"
#include "devices/tascam_x8/state.h"
#include "devices/zhiyun_x100/state.h"

namespace studio {

// Host-only helpers for the LVGL UI simulator.
shark::SharkState& simSharkState();
canon_ble::CanonBleState& simCanonState();
canon_trigger::CanonTriggerState& simCanonTriggerState();
tascam_x8::TascamX8State& simTascamState();
zhiyun_x100::X100State& simZhiyunState();
void simSetConnectedDemoState();
void simSetScanningState();
void simSetCanonConnectedState(bool recording, bool confirmed = true);
void simSetCanonTriggerConnectedState();
void simSetTascamConnectedState(bool recording);
void simSetSequenceConnectedState();

}  // namespace studio
