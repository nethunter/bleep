#pragma once

#include <cstdint>

namespace dji_osmo {

struct State {
  enum class Link : uint8_t { Disconnected, Scanning, Connecting, Connected };
  enum class Recording : uint8_t { Unknown, Stopped, Starting, Recording, Stopping };
  Link link = Link::Disconnected;
  Recording recording = Recording::Unknown;
  bool commandPending = false;
  bool lastCommandFailed = false;
  bool statusConfirmed = false;
  uint8_t battery = 0xff;
  char deviceName[40] = "";
};

}  // namespace dji_osmo
