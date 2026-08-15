#include "core/scene_validator.h"

#include "core/command_traits.h"

namespace studio {

SceneValidationStatus SceneValidator::validate(const SceneRecord& record) const {
  if (record.name[0] == '\0') return SceneValidationStatus::InvalidName;
  if (record.startCount == 0 && record.stopCount == 0) {
    return SceneValidationStatus::Empty;
  }
  if (record.startCount > CONFIG_MAX_SCENE_STEPS ||
      record.stopCount > CONFIG_MAX_SCENE_STEPS) {
    return SceneValidationStatus::Full;
  }

  InstanceId targets[CONFIG_MAX_ACTIVE_INSTANCES] = {};
  uint8_t targetCount = 0;
  const SceneStep* lists[] = {record.startSteps, record.stopSteps};
  const uint8_t counts[] = {record.startCount, record.stopCount};
  for (size_t list = 0; list < 2; ++list) {
    for (uint8_t index = 0; index < counts[list]; ++index) {
      const SceneStep& step = lists[list][index];
      if (step.type == SceneStepType::Wait) {
        if (step.waitMs < CONFIG_SCENE_MIN_WAIT_MS ||
            step.waitMs > CONFIG_SCENE_MAX_WAIT_MS) {
          return SceneValidationStatus::WaitOutOfRange;
        }
        continue;
      }
      if (step.type != SceneStepType::Action) {
        return SceneValidationStatus::InvalidStep;
      }
      if (!commandAllowedInScene(step.command)) {
        return SceneValidationStatus::UnsupportedCommand;
      }
      const DeviceRecord* device = devices_.find(step.targetId);
      if (device == nullptr) return SceneValidationStatus::MissingTarget;
      if (!device->enabled) return SceneValidationStatus::DisabledTarget;
      const InstanceProfile profile = devices_.profile(device->instanceId);
      if (profile.type == DeviceType::Unknown) {
        return SceneValidationStatus::MissingTarget;
      }
      const uint32_t required = requiredCapabilities(step.command);
      if (required == 0 || (profile.capabilities & required) != required) {
        return SceneValidationStatus::MissingCapability;
      }
      bool known = false;
      for (uint8_t target = 0; target < targetCount; ++target) {
        known = known || targets[target] == step.targetId;
      }
      if (!known) {
        if (targetCount >= CONFIG_MAX_ACTIVE_INSTANCES) {
          return SceneValidationStatus::TooManyTargets;
        }
        targets[targetCount++] = step.targetId;
      }
    }
  }
  return SceneValidationStatus::Ok;
}

}  // namespace studio
