#pragma once

#include "core/device_manager.h"
#include "core/scene_types.h"

namespace studio {

struct SceneTargetSet {
  InstanceId ids[CONFIG_MAX_ACTIVE_INSTANCES] = {};
  uint8_t count = 0;
};

class SceneTargetLease {
 public:
  explicit SceneTargetLease(DeviceManager& devices) : devices_(devices) {}

  bool collect(const SceneRecord& record, SceneTargetSet& out) const;
  bool contains(const SceneTargetSet& targets, InstanceId instanceId) const;
  bool owns(const SceneTargetSet& targets) const;
  void release(const SceneTargetSet& targets);
  void releaseExcept(const SceneTargetSet& current,
                     const SceneTargetSet& next);

 private:
  DeviceManager& devices_;
};

}  // namespace studio
