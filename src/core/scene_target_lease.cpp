#include "core/scene_target_lease.h"

namespace studio {

bool SceneTargetLease::collect(const SceneRecord& record,
                               SceneTargetSet& out) const {
  out = {};
  const SceneStep* lists[] = {record.startSteps, record.stopSteps};
  const uint8_t counts[] = {record.startCount, record.stopCount};
  for (size_t list = 0; list < 2; ++list) {
    for (uint8_t index = 0; index < counts[list]; ++index) {
      const SceneStep& step = lists[list][index];
      if (step.type != SceneStepType::Action ||
          contains(out, step.targetId)) {
        continue;
      }
      if (out.count >= CONFIG_MAX_ACTIVE_INSTANCES) return false;
      out.ids[out.count++] = step.targetId;
    }
  }
  return true;
}

bool SceneTargetLease::contains(const SceneTargetSet& targets,
                                InstanceId instanceId) const {
  for (uint8_t index = 0; index < targets.count; ++index) {
    if (targets.ids[index] == instanceId) return true;
  }
  return false;
}

bool SceneTargetLease::owns(const SceneTargetSet& targets) const {
  if (targets.count == 0) return false;
  for (uint8_t index = 0; index < targets.count; ++index) {
    if (!devices_.ownedBy(targets.ids[index], ConnectionOwner::Sequence)) {
      return false;
    }
  }
  return true;
}

void SceneTargetLease::release(const SceneTargetSet& targets) {
  for (uint8_t index = 0; index < targets.count; ++index) {
    devices_.release(targets.ids[index], ConnectionOwner::Sequence);
  }
}

void SceneTargetLease::releaseExcept(const SceneTargetSet& current,
                                     const SceneTargetSet& next) {
  for (uint8_t index = 0; index < current.count; ++index) {
    if (!contains(next, current.ids[index])) {
      devices_.release(current.ids[index], ConnectionOwner::Sequence);
    }
  }
}

}  // namespace studio
