#include "haptic_feedback.h"

#include <Arduino.h>

#include <cstddef>

namespace haptic_feedback {
namespace {

struct Segment {
  bool enabled;
  uint16_t durationMs;
};

constexpr Segment kPress[] = {{true, 20}};
constexpr Segment kConnected[] = {{true, 12}, {false, 24}, {true, 12}};
constexpr Segment kBack[] = {{true, 15}, {false, 35}, {true, 30}};
constexpr Segment kError[] = {{true, 60}, {false, 45}, {true, 60}};

OutputFn outputFn = nullptr;
const Segment* segments = nullptr;
size_t segmentCount = 0;
size_t segmentIndex = 0;
uint32_t segmentDeadlineMs = 0;
Pattern activePattern = Pattern::Press;
bool active = false;
bool outputEnabled = false;
bool connectedPending = false;

uint8_t priority(Pattern pattern) {
  switch (pattern) {
    case Pattern::Press:
      return 0;
    case Pattern::Connected:
      return 1;
    case Pattern::Back:
      return 2;
    case Pattern::Error:
      return 3;
  }
  return 0;
}

void setOutput(bool enabled) {
  if (outputEnabled == enabled) {
    return;
  }
  outputEnabled = enabled;
  if (outputFn != nullptr) {
    outputFn(enabled);
  }
}

void selectPattern(Pattern pattern) {
  switch (pattern) {
    case Pattern::Press:
      segments = kPress;
      segmentCount = sizeof(kPress) / sizeof(kPress[0]);
      break;
    case Pattern::Connected:
      segments = kConnected;
      segmentCount = sizeof(kConnected) / sizeof(kConnected[0]);
      break;
    case Pattern::Back:
      segments = kBack;
      segmentCount = sizeof(kBack) / sizeof(kBack[0]);
      break;
    case Pattern::Error:
      segments = kError;
      segmentCount = sizeof(kError) / sizeof(kError[0]);
      break;
  }
}

}  // namespace

void begin(OutputFn output) {
  outputFn = output;
  segments = nullptr;
  segmentCount = 0;
  segmentIndex = 0;
  segmentDeadlineMs = 0;
  active = false;
  outputEnabled = false;
  connectedPending = false;
  if (outputFn != nullptr) {
    outputFn(false);
  }
}

void request(Pattern pattern) {
  // Preserve a stronger pattern already in progress. A Back request replaces
  // the generic Press generated for the same LVGL click.
  if (active && priority(pattern) < priority(activePattern)) {
    if (pattern == Pattern::Connected) {
      connectedPending = true;
    }
    return;
  }

  if (pattern == Pattern::Connected) {
    connectedPending = false;
  }
  activePattern = pattern;
  selectPattern(pattern);
  segmentIndex = 0;
  active = true;
  setOutput(segments[0].enabled);
  segmentDeadlineMs = millis() + segments[0].durationMs;
}

void loop(uint32_t now) {
  while (active && static_cast<int32_t>(now - segmentDeadlineMs) >= 0) {
    ++segmentIndex;
    if (segmentIndex >= segmentCount) {
      active = false;
      setOutput(false);
      if (connectedPending) {
        request(Pattern::Connected);
      }
      return;
    }
    setOutput(segments[segmentIndex].enabled);
    segmentDeadlineMs += segments[segmentIndex].durationMs;
  }
}

}  // namespace haptic_feedback
