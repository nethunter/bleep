#pragma once

#include "core/device_manager.h"
#include "core/scene_types.h"

namespace studio {

class SceneValidator {
 public:
  explicit SceneValidator(const DeviceManager& devices) : devices_(devices) {}

  SceneValidationStatus validate(const SceneRecord& record) const;

 private:
  const DeviceManager& devices_;
};

}  // namespace studio
