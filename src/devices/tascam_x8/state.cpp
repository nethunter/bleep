#include "devices/tascam_x8/state.h"

namespace tascam_x8 {

void resetTransientState(TascamX8State& state) {
  state.recording = TascamX8State::Recording::Unknown;
  state.recordingConfirmed = false;
  state.commandPending = false;
  state.lastCommandFailed = false;
}

void markCommandQueued(TascamX8State& state, bool start) {
  state.recording =
      start ? TascamX8State::Recording::Starting
            : TascamX8State::Recording::Stopping;
  state.recordingConfirmed = false;
  state.commandPending = true;
  state.lastCommandFailed = false;
}

void markCommandWriteFailed(TascamX8State& state) {
  state.recording = TascamX8State::Recording::Unknown;
  state.recordingConfirmed = false;
  state.commandPending = false;
  state.lastCommandFailed = true;
}

void reduceFrame(TascamX8State& state, const ParsedFrame& frame) {
  const RecordEvent event = parseRecordEvent(frame);
  if (event == RecordEvent::Started) {
    state.recording = TascamX8State::Recording::Recording;
    state.recordingConfirmed = true;
    state.commandPending = false;
    state.lastCommandFailed = false;
  } else if (event == RecordEvent::Stopped) {
    state.recording = TascamX8State::Recording::Stopped;
    state.recordingConfirmed = true;
    state.commandPending = false;
    state.lastCommandFailed = false;
  }
}

}  // namespace tascam_x8
