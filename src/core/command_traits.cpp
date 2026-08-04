#include "core/command_traits.h"

namespace studio {
namespace {

constexpr CommandTrait kTraits[] = {
    {CommandType::RecordStart, Capability::RecordStart, true},
    {CommandType::RecordStop, Capability::RecordStop, true},
};

}  // namespace

const CommandTrait* commandTrait(CommandType type) {
  for (const CommandTrait& trait : kTraits) {
    if (trait.type == type) {
      return &trait;
    }
  }
  return nullptr;
}

bool commandAllowedInScene(CommandType type) {
  const CommandTrait* trait = commandTrait(type);
  return trait != nullptr && trait->sceneAllowed;
}

Capability requiredCapability(CommandType type) {
  const CommandTrait* trait = commandTrait(type);
  return trait != nullptr ? trait->required : Capability::None;
}

}  // namespace studio
