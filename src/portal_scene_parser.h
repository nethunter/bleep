#pragma once

#include <ArduinoJson.h>

#include <cstdint>

#include "core/scene_types.h"

namespace portal {

enum class StepParseStatus : uint8_t {
  Ok,
  TooManySteps,
  InvalidKind,
  WaitOutOfRange,
  InvalidCommand,
  InvalidTarget,
};

struct StepParseResult {
  StepParseStatus status = StepParseStatus::Ok;
  uint8_t index = 0;
};

StepParseResult parseSceneSteps(JsonVariantConst value,
                                studio::SceneStep* destination,
                                uint8_t& count);
const char* stepParseMessage(StepParseStatus status);

}  // namespace portal
