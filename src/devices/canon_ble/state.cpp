#include "devices/canon_ble/state.h"

namespace canon_ble {

void resetTransientState(CanonBleState& state) {
  state.phase = CanonBleState::Phase::Idle;
  state.recording = CanonBleState::Recording::Unknown;
  state.recordingConfirmed = false;
  state.commandPending = false;
  state.lastCommandFailed = false;
  state.powerOffFailed = false;
  state.pairingRejected = false;
  state.protocolFailed = false;
  state.claimedPeerVisible = false;
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

bool completeStopIfAlreadyStopped(CanonBleState& state) {
  if (!state.recordingConfirmed ||
      state.recording != CanonBleState::Recording::Stopped) {
    return false;
  }
  state.lastCommandFailed = false;
  return true;
}

void reduceRecordNotification(CanonBleState& state, const uint8_t* data,
                              size_t len) {
  const RecordEvent event = parseRecordEvent(data, len);
  const bool contradictedStart =
      state.commandPending &&
      state.recording == CanonBleState::Recording::Starting &&
      event == RecordEvent::Stopped;
  const bool contradictedStop =
      state.commandPending &&
      state.recording == CanonBleState::Recording::Stopping &&
      event == RecordEvent::Started;
  if (event == RecordEvent::Started) {
    state.recording = CanonBleState::Recording::Recording;
    state.recordingConfirmed = true;
    state.commandPending = false;
    state.lastCommandFailed = contradictedStop;
  } else if (event == RecordEvent::Stopped) {
    state.recording = CanonBleState::Recording::Stopped;
    state.recordingConfirmed = true;
    state.commandPending = false;
    state.lastCommandFailed = contradictedStart;
  }
}

}  // namespace canon_ble
