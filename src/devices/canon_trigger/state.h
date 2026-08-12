#pragma once

#include <cstdint>

namespace canon_trigger {

struct CanonTriggerState {
  enum class Link : uint8_t {
    Disconnected,
    Scanning,
    Connecting,
    Connected,
  };

  Link link = Link::Disconnected;
  bool hasSavedDevice = false;
  bool triggerPending = false;
  bool lastTriggerSucceeded = false;
  bool claimedPeerVisible = false;
  uint32_t triggerCount = 0;
  char deviceName[40] = "";
};

void resetTransientState(CanonTriggerState& state);
void markTriggerQueued(CanonTriggerState& state);
void markTriggerComplete(CanonTriggerState& state, bool succeeded);

}  // namespace canon_trigger
