#include "devices/gopro/state.h"

#include "devices/gopro/protocol.h"

namespace gopro {

void resetTransientState(GoProState& state) {
  state.recording = GoProState::Recording::Unknown;
  state.recordingConfirmed = false;
  state.commandPending = false;
  state.lastCommandFailed = false;
}

void markCommandQueued(GoProState& state, bool start) {
  state.recording = start ? GoProState::Recording::Starting
                          : GoProState::Recording::Stopping;
  state.commandPending = true;
  state.lastCommandFailed = false;
}

void reduceCommandResponse(GoProState& state, uint8_t status) {
  if (status == kSuccessStatus) return;
  markCommandTimeout(state);
}

void reduceEncodingStatus(GoProState& state, bool encoding) {
  state.recording = encoding ? GoProState::Recording::Recording
                             : GoProState::Recording::Stopped;
  state.recordingConfirmed = true;
  state.commandPending = false;
  state.lastCommandFailed = false;
}

bool reducePendingEncodingStatus(GoProState& state, bool encoding,
                                 bool expectedEncoding) {
  if (!state.commandPending || encoding != expectedEncoding) return false;
  reduceEncodingStatus(state, encoding);
  return true;
}

void markCommandTimeout(GoProState& state) {
  state.recording = GoProState::Recording::Unknown;
  state.recordingConfirmed = false;
  state.commandPending = false;
  state.lastCommandFailed = true;
}

}  // namespace gopro
