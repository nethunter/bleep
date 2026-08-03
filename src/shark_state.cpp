#include "shark_state.h"

#include <cstring>

namespace shark {

SharkState::SharkState() {
  for (int i = 0; i < kKeypointCount; ++i) {
    speed[i] = -1;
    hold[i] = -1;
  }
}

void resetDeviceState(SharkState& state) {
  state.battery = -1;
  state.presenceKnown = false;
  for (int i = 0; i < kKeypointCount; ++i) {
    state.present[i] = false;
    state.speed[i] = -1;
    state.hold[i] = -1;
  }
  state.timingKnown = false;
  state.trackingKnown = false;
  state.tracking = false;
  state.runProgressKnown = false;
  state.runPercent = 0.0f;
  state.runStateCode = kRunStop;
  std::strncpy(state.runText, "idle", sizeof(state.runText) - 1);
  state.runText[sizeof(state.runText) - 1] = '\0';
}

void reduceFrame(SharkState& state, const ParsedFrame& frame) {
  if (frame.code == 0x08 && frame.data != nullptr && frame.dataLen == kTimingDataLen) {
    state.timingKnown = true;
    for (int i = 1; i < kKeypointCount; ++i) {
      const int base = 1 + (i - 1) * 4;
      state.speed[i] = frame.data[base + kTimingSpeedOffset];
      state.hold[i] = frame.data[base + kTimingHoldOffset];
    }
  }

  if (frame.family == 0x06 && frame.code == 0x00 && frame.kind == 0x0029 &&
      frame.data != nullptr && frame.dataLen > 16 && frame.data[2] <= 100) {
    state.battery = frame.data[2];
  }

  if (frame.family == 0x06 && frame.code == 0x03 && frame.data != nullptr &&
      frame.dataLen >= 2) {
    const size_t flags = frame.dataLen - 1;
    const uint8_t* present = frame.data + 1;
    for (int i = 0; i < kKeypointCount; ++i) {
      state.present[i] = static_cast<size_t>(i) < flags && present[i] != 0;
    }
    state.presenceKnown = true;
  }

  RunProgress progress;
  if (!parseRunProgress(frame, progress)) {
    return;
  }

  state.runProgressKnown = true;
  const bool running = progress.stateCode == kRunStart || progress.stateCode == 0x06;
  if (running) {
    state.runStateCode = kRunStart;
  } else if (progress.stateCode == kRunStandby) {
    state.runStateCode = kRunStandby;
  } else if (progress.stateCode == kRunStop &&
             (state.runStateCode == kRunStart || state.runStateCode == 0x06)) {
    state.runStateCode = kRunStop;
  }

  if (running) {
    int presentCount = 0;
    for (int i = 0; i < kKeypointCount; ++i) {
      if (state.present[i]) {
        ++presentCount;
      }
    }
    const int totalSegments = presentCount - 1;
    if (totalSegments > 0) {
      float whole =
          (progress.segment + progress.progressPercent / 100.0f) / totalSegments * 100.0f;
      if (whole < 0.0f) {
        whole = 0.0f;
      } else if (whole > 100.0f) {
        whole = 100.0f;
      }
      state.runPercent = whole;
    } else {
      state.runPercent = progress.progressPercent;
    }
  } else {
    state.runPercent = 0.0f;
  }

  std::strncpy(state.runText, runStateLabel(progress.stateCode), sizeof(state.runText) - 1);
  state.runText[sizeof(state.runText) - 1] = '\0';
}

}  // namespace shark
