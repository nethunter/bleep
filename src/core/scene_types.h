#pragma once

#include "core/device_types.h"

namespace studio {

using SceneId = uint32_t;
constexpr SceneId kInvalidSceneId = 0;

enum class SceneStepType : uint8_t {
  Action,
  Wait,
};

struct SceneStep {
  SceneStepType type = SceneStepType::Wait;
  InstanceId targetId = kInvalidInstanceId;
  CommandType command = CommandType::Refresh;
  uint32_t waitMs = 0;
  int32_t value0 = 0;
  int32_t value1 = 0;
  int32_t value2 = 0;

  SceneStep() = default;
  SceneStep(SceneStepType stepType, InstanceId target, CommandType commandType,
            uint32_t milliseconds)
      : type(stepType),
        targetId(target),
        command(commandType),
        waitMs(milliseconds) {}
};

inline SceneStep makeActionStep(InstanceId target, CommandType command) {
  return SceneStep(SceneStepType::Action, target, command, 0);
}

inline SceneStep makeActionStep(InstanceId target, CommandType command,
                                int32_t value0, int32_t value1,
                                int32_t value2 = 0) {
  SceneStep step(SceneStepType::Action, target, command, 0);
  step.value0 = value0;
  step.value1 = value1;
  step.value2 = value2;
  return step;
}

inline SceneStep makeWaitStep(uint32_t milliseconds) {
  return SceneStep(SceneStepType::Wait, kInvalidInstanceId, CommandType::Refresh,
                   milliseconds);
}

enum class SceneStopMode : uint8_t {
  Generated,
  Custom,
};

struct SceneRecord {
  SceneId sceneId = kInvalidSceneId;
  bool enabled = true;
  char name[kDeviceNameCapacity] = "";
  SceneStep startSteps[CONFIG_MAX_SCENE_STEPS] = {};
  uint8_t startCount = 0;
  SceneStep stopSteps[CONFIG_MAX_SCENE_STEPS] = {};
  uint8_t stopCount = 0;
  SceneStopMode stopMode = SceneStopMode::Generated;
};

inline bool makeGeneratedStopStep(const SceneStep& start, SceneStep& stop) {
  if (start.type == SceneStepType::Wait) {
    stop = start;
    return true;
  }
  if (start.type != SceneStepType::Action) {
    return false;
  }
  CommandType inverse = start.command;
  switch (start.command) {
    case CommandType::RecordStart: inverse = CommandType::RecordStop; break;
    case CommandType::RecordStop: inverse = CommandType::RecordStart; break;
    case CommandType::TurnOn: inverse = CommandType::TurnOff; break;
    case CommandType::TurnOff: inverse = CommandType::TurnOn; break;
    case CommandType::RecordTrigger:
    case CommandType::Press:
    case CommandType::Activate:
      break;
    case CommandType::SetLightCct:
    case CommandType::SetLightRgb:
      return false;
    default:
      return false;
  }
  stop = makeActionStep(start.targetId, inverse, start.value0, start.value1,
                        start.value2);
  return true;
}

inline void generateStopSteps(SceneRecord& record) {
  record.stopCount = 0;
  for (uint8_t i = record.startCount; i > 0; --i) {
    SceneStep generated;
    if (makeGeneratedStopStep(record.startSteps[i - 1], generated)) {
      record.stopSteps[record.stopCount++] = generated;
    }
  }
  for (uint8_t i = record.stopCount; i < CONFIG_MAX_SCENE_STEPS; ++i) {
    record.stopSteps[i] = SceneStep{};
  }
}

enum class ScenePhase : uint8_t {
  Idle,
  Connecting,
  Ready,  // Connected and held; waiting for Start
  RunningStart,
  IdleArmed,
  RunningStop,
  Failed,
  Completed,
};

enum class SceneStepResult : uint8_t {
  Pending,
  Running,
  Succeeded,
  Failed,
  Skipped,
};

enum class SceneValidationStatus : uint8_t {
  Ok,
  Empty,
  Full,
  InvalidName,
  InvalidStep,
  MissingTarget,
  DisabledTarget,
  UnsupportedCommand,
  MissingCapability,
  WaitOutOfRange,
  TooManyTargets,
  Busy,
};

enum class SceneRunStatus : uint8_t {
  Ok,
  InvalidScene,
  Disabled,
  ValidationFailed,
  Busy,
  ConnectTimeout,
  ActionFailed,
  ActionTimeout,
  Canceled,
};

struct SceneProgress {
  SceneId sceneId = kInvalidSceneId;
  ScenePhase phase = ScenePhase::Idle;
  SceneRunStatus lastStatus = SceneRunStatus::Ok;
  bool runningStart = false;
  uint8_t stepIndex = 0;
  uint8_t stepCount = 0;
  SceneStepResult stepResult = SceneStepResult::Pending;
  InstanceId connectTarget = kInvalidInstanceId;
  char detail[48] = "";
};

}  // namespace studio
