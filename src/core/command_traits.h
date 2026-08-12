#pragma once

#include "core/device_types.h"

namespace studio {

struct CommandTrait {
  CommandType type = CommandType::Refresh;
  uint32_t requiredMask = 0;
  bool sceneAllowed = false;

  constexpr CommandTrait() = default;
  constexpr CommandTrait(CommandType commandType, uint32_t capabilities,
                         bool allowed)
      : type(commandType), requiredMask(capabilities), sceneAllowed(allowed) {}
};

const CommandTrait* commandTrait(CommandType type);
bool commandAllowedInScene(CommandType type);
Capability requiredCapability(CommandType type);
uint32_t requiredCapabilities(CommandType type);

}  // namespace studio
