#pragma once

#include <cstdint>

#include "devices/tascam_x8/protocol.h"

namespace tascam_x8 {

struct TascamX8State {
  enum class Link : uint8_t {
    Disconnected,
    Scanning,
    Connecting,
    Connected,
  };

  enum class Recording : uint8_t {
    Unknown,
    Stopped,
    Starting,
    Recording,
    Stopping,
  };

  Link link = Link::Disconnected;
  Recording recording = Recording::Unknown;
  bool recordingConfirmed = false;
  bool hasSavedDevice = false;
  bool commandPending = false;
  bool lastCommandFailed = false;
  char deviceName[40] = "";
};

void resetTransientState(TascamX8State& state);
void markCommandQueued(TascamX8State& state, bool start);
void markCommandWriteFailed(TascamX8State& state);
void reduceFrame(TascamX8State& state, const ParsedFrame& frame);

}  // namespace tascam_x8
