#include "devices/canon_ble/state.h"

namespace canon_ble {

void resetTransientState(CanonBleState& state) {
  state.triggerPending = false;
  state.lastTriggerSucceeded = false;
}

void markTriggerQueued(CanonBleState& state) {
  state.triggerPending = true;
  state.lastTriggerSucceeded = false;
}

void markTriggerComplete(CanonBleState& state, bool succeeded) {
  state.triggerPending = false;
  state.lastTriggerSucceeded = succeeded;
  if (succeeded) {
    ++state.triggerCount;
  }
}

}  // namespace canon_ble
