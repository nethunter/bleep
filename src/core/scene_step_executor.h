#pragma once

#include "core/device_manager.h"
#include "core/scene_types.h"

namespace studio {

class SceneStepExecutor {
 public:
  explicit SceneStepExecutor(DeviceManager& devices) : devices_(devices) {}

  bool enqueue(const SceneStep& step, uint32_t& requestId) {
    DeviceCommand command;
    command.instanceId = step.targetId;
    command.type = step.command;
    command.value0 = step.value0;
    command.value1 = step.value1;
    command.value2 = step.value2;
    return devices_.enqueue(command, &requestId);
  }

  bool cancel(uint32_t requestId, InstanceId instanceId) {
    return devices_.cancelCommand(requestId, instanceId);
  }

 private:
  DeviceManager& devices_;
};

}  // namespace studio
