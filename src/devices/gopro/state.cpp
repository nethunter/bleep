#include "devices/gopro/state.h"

#include "devices/gopro/protocol.h"

namespace gopro {

void resetTransientState(GoProState& state) {
  state.recording = GoProState::Recording::Unknown;
  state.commandPending = false;
  state.lastCommandFailed = false;
}

void markCommandQueued(GoProState& state, bool start) {
  state.recording = start ? GoProState::Recording::Starting
                          : GoProState::Recording::Stopping;
  state.commandPending = true;
  state.lastCommandFailed = false;
}

void reduceCommandResponse(GoProState& state, bool requestedStart,
                           uint8_t status) {
  state.commandPending = false;
  state.lastCommandFailed = status != kSuccessStatus;
  if (status == kSuccessStatus) {
    state.recording = requestedStart ? GoProState::Recording::Recording
                                     : GoProState::Recording::Stopped;
  } else {
    state.recording = GoProState::Recording::Unknown;
  }
}

}  // namespace gopro
