#pragma once

#include <cstdint>

namespace gopro {

struct GoProState {
  enum class Link : uint8_t { Disconnected, Scanning, Connecting, Connected };
  enum class Recording : uint8_t { Unknown, Stopped, Starting, Recording, Stopping };

  Link link = Link::Disconnected;
  Recording recording = Recording::Unknown;
  bool hasSavedDevice = false;
  bool commandPending = false;
  bool lastCommandFailed = false;
  char deviceName[40] = "";
};

void resetTransientState(GoProState& state);
void markCommandQueued(GoProState& state, bool start);
void reduceCommandResponse(GoProState& state, bool requestedStart,
                           uint8_t status);

}  // namespace gopro
