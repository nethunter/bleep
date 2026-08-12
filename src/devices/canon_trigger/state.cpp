#include "devices/canon_trigger/state.h"

namespace canon_trigger {

void resetTransientState(CanonTriggerState& state) {
  state.triggerPending = false;
  state.lastTriggerSucceeded = false;
  state.claimedPeerVisible = false;
}

void markTriggerQueued(CanonTriggerState& state) {
  state.triggerPending = true;
  state.lastTriggerSucceeded = false;
}

void markTriggerComplete(CanonTriggerState& state, bool succeeded) {
  state.triggerPending = false;
  state.lastTriggerSucceeded = succeeded;
  if (succeeded) {
    ++state.triggerCount;
  }
}

}  // namespace canon_trigger
