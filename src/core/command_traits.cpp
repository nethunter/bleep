#include "core/command_traits.h"

namespace studio {
namespace {

constexpr CommandTrait kTraits[] = {
    {CommandType::RecordTrigger, capabilityBit(Capability::RecordTrigger), true},
    {CommandType::RecordStart, capabilityBit(Capability::RecordStart), true},
    {CommandType::RecordStop, capabilityBit(Capability::RecordStop), true},
    {CommandType::TurnOn, capabilityBit(Capability::TurnOn), true},
    {CommandType::TurnOff, capabilityBit(Capability::TurnOff), true},
    {CommandType::Press, capabilityBit(Capability::Press), true},
    {CommandType::Activate, capabilityBit(Capability::Activate), true},
    {CommandType::SetLightCct, capabilityBit(Capability::SetLightCct), true},
    {CommandType::SetLightRgb, capabilityBit(Capability::SetLightRgb), true},
    {CommandType::SetLightCctAndOn,
     capabilityBit(Capability::SetLightCct) |
         capabilityBit(Capability::TurnOn),
     true},
    {CommandType::SetLightRgbAndOn,
     capabilityBit(Capability::SetLightRgb) |
         capabilityBit(Capability::TurnOn),
     true},
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
  if (trait == nullptr || trait->requiredMask == 0 ||
      (trait->requiredMask & (trait->requiredMask - 1)) != 0) {
    return Capability::None;
  }
  return static_cast<Capability>(trait->requiredMask);
}

uint32_t requiredCapabilities(CommandType type) {
  const CommandTrait* trait = commandTrait(type);
  return trait != nullptr ? trait->requiredMask : 0;
}

}  // namespace studio
