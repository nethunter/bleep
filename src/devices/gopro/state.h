#pragma once

#include <cstdint>

namespace gopro {

struct GoProState {
  enum class Link : uint8_t { Disconnected, Scanning, Connecting, Connected };
  enum class Recording : uint8_t { Unknown, Stopped, Starting, Recording, Stopping };
  enum class Power : uint8_t { Awake, Sleeping, Asleep, Waking, SleepFailed };

  Link link = Link::Disconnected;
  Recording recording = Recording::Unknown;
  Power power = Power::Awake;
  bool recordingConfirmed = false;
  bool hasSavedDevice = false;
  bool commandPending = false;
  bool lastCommandFailed = false;
  bool powerCommandPending = false;
  bool powerOffFailed = false;
  char deviceName[40] = "";
};

void resetTransientState(GoProState& state);
void markCommandQueued(GoProState& state, bool start);
void reduceCommandResponse(GoProState& state, uint8_t status);
void reduceEncodingStatus(GoProState& state, bool encoding);
bool reducePendingEncodingStatus(GoProState& state, bool encoding,
                                 bool expectedEncoding);
void markCommandTimeout(GoProState& state);

}  // namespace gopro
