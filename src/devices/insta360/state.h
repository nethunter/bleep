#pragma once
#include <cstdint>
namespace insta360 {
struct State {
  enum class Link : uint8_t { Disconnected, Scanning, Connected };
  enum class Recording : uint8_t { Unknown, Stopped, Starting, Recording, Stopping };
  enum class Photo : uint8_t { Unknown, Idle, Starting, Capturing, Saving };
  enum class Power : uint8_t { On, PoweringOff, Off, PoweringOn };
  Link link = Link::Disconnected;
  Recording recording = Recording::Unknown;
  Photo photo = Photo::Unknown;
  Power power = Power::On;
  bool recordingConfirmed = false;
  bool triggerPending = false;
  bool lastTriggerFailed = false;
  bool powerCommandPending = false;
  bool powerCommandFailed = false;
  bool goUltraExperimental = false;
  uint32_t triggerCount = 0;
  char deviceName[40] = "";
};

inline void assumeVideoIdle(State& state) {
  state.recording = State::Recording::Stopped;
  state.recordingConfirmed = false;
  state.photo = State::Photo::Unknown;
}

inline bool canStartRecording(const State& state) {
  return state.recording == State::Recording::Stopped;
}

inline bool canStopRecording(const State& state) {
  return state.recordingConfirmed &&
         state.recording == State::Recording::Recording;
}
}  // namespace insta360
