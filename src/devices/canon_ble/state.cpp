#include "devices/canon_ble/state.h"

namespace canon_ble {

void resetTransientState(CanonBleState& state) {
  state.phase = CanonBleState::Phase::Idle;
  state.recording = CanonBleState::Recording::Unknown;
  state.recordingConfirmed = false;
  state.commandPending = false;
  state.lastCommandFailed = false;
  state.pairingRejected = false;
}

void markCommandQueued(CanonBleState& state, bool start) {
  state.recording = start ? CanonBleState::Recording::Starting
                          : CanonBleState::Recording::Stopping;
  state.recordingConfirmed = false;
  state.commandPending = true;
  state.lastCommandFailed = false;
}

void markCommandWriteFailed(CanonBleState& state) {
  state.recording = CanonBleState::Recording::Unknown;
  state.recordingConfirmed = false;
  state.commandPending = false;
  state.lastCommandFailed = true;
}

void reduceRecordNotification(CanonBleState& state, const uint8_t* data,
                              size_t len) {
  const RecordEvent event = parseRecordEvent(data, len);
  if (event == RecordEvent::Started) {
    state.recording = CanonBleState::Recording::Recording;
    state.recordingConfirmed = true;
    state.commandPending = false;
    state.lastCommandFailed = false;
  } else if (event == RecordEvent::Stopped) {
    state.recording = CanonBleState::Recording::Stopped;
    state.recordingConfirmed = true;
    state.commandPending = false;
    state.lastCommandFailed = false;
  }
}

}  // namespace canon_ble
