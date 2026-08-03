#pragma once

#include <cstdint>

namespace canon_ble {

struct CanonBleState {
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
  uint32_t triggerCount = 0;
  char deviceName[40] = "";
};

void resetTransientState(CanonBleState& state);
void markTriggerQueued(CanonBleState& state);
void markTriggerComplete(CanonBleState& state, bool succeeded);

}  // namespace canon_ble
