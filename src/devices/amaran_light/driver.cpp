#include "devices/amaran_light/driver.h"

#include "devices/amaran_light/runtime.h"

namespace studio {

bool AmaranLightDriver::activate(const DeviceRecord& record) {
  return amaran_light::runtime().activate(record);
}
void AmaranLightDriver::deactivate(InstanceId instanceId) {
  amaran_light::runtime().deactivate(instanceId);
}
void AmaranLightDriver::loop() { amaran_light::runtime().loop(); }
CommandStatus AmaranLightDriver::dispatch(const DeviceCommand& command) {
  return amaran_light::runtime().dispatch(command);
}
DeviceRuntimeState AmaranLightDriver::runtimeState(InstanceId instanceId) const {
  return amaran_light::runtime().runtimeState(instanceId);
}
const void* AmaranLightDriver::specializedState(InstanceId instanceId) const {
  return amaran_light::runtime().state(instanceId);
}
void AmaranLightDriver::forgetPairing(const DeviceRecord& record) {
  amaran_light::runtime().forgetLocal(record.instanceId);
}
bool AmaranLightDriver::consumePairingUpdate(InstanceId instanceId,
                                             DeviceRecord& record) {
  return amaran_light::runtime().consumePairingUpdate(instanceId, record);
}

}  // namespace studio
