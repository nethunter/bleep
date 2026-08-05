#include "core/command_traits.h"

namespace studio {
namespace {

constexpr CommandTrait kTraits[] = {
    {CommandType::RecordStart, Capability::RecordStart, true},
    {CommandType::RecordStop, Capability::RecordStop, true},
    {CommandType::TurnOn, Capability::TurnOn, true},
    {CommandType::TurnOff, Capability::TurnOff, true},
    {CommandType::Press, Capability::Press, true},
    {CommandType::Activate, Capability::Activate, true},
    {CommandType::SetLightCct, Capability::SetLightCct, true},
    {CommandType::SetLightRgb, Capability::SetLightRgb, true},
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
