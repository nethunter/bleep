#pragma once

#include <cstdint>

#include "shark_protocol.h"

namespace shark {

struct SharkState {
  enum class Link : uint8_t { Disconnected, Scanning, Connecting, Connected };

  Link link = Link::Disconnected;
  char deviceName[40] = "";
  bool hasSavedDevice = false;

  int battery = -1;

  bool presenceKnown = false;
  bool present[kKeypointCount] = {false};

  bool timingKnown = false;
  int speed[kKeypointCount];
  int hold[kKeypointCount];

  bool trackingKnown = false;
  bool tracking = false;

  bool loopOn = false;
  bool reverse = false;

  char runText[16] = "idle";
  uint8_t runStateCode = kRunStop;
  bool runProgressKnown = false;
  float runPercent = 0.0f;

  SharkState();
};

// Pure state reduction kept independent of Arduino, NimBLE, and LVGL so
// captured notifications can be regression-tested on the host.
void resetDeviceState(SharkState& state);
void reduceFrame(SharkState& state, const ParsedFrame& frame);

}  // namespace shark
