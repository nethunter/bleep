#include "portal_scene_parser.h"

#include <cstring>

namespace portal {
namespace {

bool parseCommand(const char* value, studio::CommandType& out) {
  if (value == nullptr) return false;
  struct Entry {
    const char* id;
    studio::CommandType command;
  };
  const Entry entries[] = {
      {"record_trigger", studio::CommandType::RecordTrigger},
      {"record_start", studio::CommandType::RecordStart},
      {"record_stop", studio::CommandType::RecordStop},
      {"turn_on", studio::CommandType::TurnOn},
      {"turn_off", studio::CommandType::TurnOff},
      {"press", studio::CommandType::Press},
      {"activate", studio::CommandType::Activate},
      {"set_light_cct", studio::CommandType::SetLightCct},
      {"set_light_rgb", studio::CommandType::SetLightRgb},
      {"set_light_cct_and_on", studio::CommandType::SetLightCctAndOn},
      {"set_light_rgb_and_on", studio::CommandType::SetLightRgbAndOn},
  };
  for (const Entry& entry : entries) {
    if (std::strcmp(value, entry.id) == 0) {
      out = entry.command;
      return true;
    }
  }
  return false;
}

StepParseResult failure(StepParseStatus status, size_t index) {
  StepParseResult result;
  result.status = status;
  result.index = static_cast<uint8_t>(index);
  return result;
}

}  // namespace

StepParseResult parseSceneSteps(JsonVariantConst value,
                                studio::SceneStep* destination,
                                uint8_t& count) {
  const JsonArrayConst steps = value.as<JsonArrayConst>();
  if (steps.isNull()) {
    count = 0;
    return {};
  }
  if (steps.size() > CONFIG_MAX_SCENE_STEPS) {
    return failure(StepParseStatus::TooManySteps, CONFIG_MAX_SCENE_STEPS);
  }
  count = 0;
  size_t index = 0;
  for (JsonObjectConst step : steps) {
    const char* kind = step["kind"].as<const char*>();
    if (kind != nullptr && std::strcmp(kind, "wait") == 0) {
      const int64_t wait = step["wait_ms"] | -1;
      if (wait < CONFIG_SCENE_MIN_WAIT_MS || wait > CONFIG_SCENE_MAX_WAIT_MS) {
        return failure(StepParseStatus::WaitOutOfRange, index);
      }
      destination[count++] = studio::makeWaitStep(static_cast<uint32_t>(wait));
    } else if (kind != nullptr && std::strcmp(kind, "action") == 0) {
      studio::CommandType command;
      if (!parseCommand(step["command"].as<const char*>(), command)) {
        return failure(StepParseStatus::InvalidCommand, index);
      }
      const studio::InstanceId target = step["target_id"] | studio::kInvalidInstanceId;
      if (target == studio::kInvalidInstanceId) {
        return failure(StepParseStatus::InvalidTarget, index);
      }
      destination[count++] = studio::makeActionStep(
          target, command, step["value0"] | 0, step["value1"] | 0,
          step["value2"] | 0);
    } else {
      return failure(StepParseStatus::InvalidKind, index);
    }
    ++index;
  }
  return {};
}

const char* stepParseMessage(StepParseStatus status) {
  switch (status) {
    case StepParseStatus::TooManySteps: return "exceeds the step limit";
    case StepParseStatus::InvalidKind: return "has an invalid type";
    case StepParseStatus::WaitOutOfRange: return "has a wait outside 0 to 60000 ms";
    case StepParseStatus::InvalidCommand: return "has an invalid command";
    case StepParseStatus::InvalidTarget: return "has an invalid target device";
    case StepParseStatus::Ok: return "is valid";
  }
  return "is invalid";
}

}  // namespace portal
