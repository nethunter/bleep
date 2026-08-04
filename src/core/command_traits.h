#pragma once

#include "core/device_types.h"

namespace studio {

struct CommandTrait {
  CommandType type = CommandType::Refresh;
  Capability required = Capability::None;
  bool sceneAllowed = false;

  constexpr CommandTrait() = default;
  constexpr CommandTrait(CommandType commandType, Capability capability,
                         bool allowed)
      : type(commandType), required(capability), sceneAllowed(allowed) {}
};

const CommandTrait* commandTrait(CommandType type);
bool commandAllowedInScene(CommandType type);
Capability requiredCapability(CommandType type);

}  // namespace studio
