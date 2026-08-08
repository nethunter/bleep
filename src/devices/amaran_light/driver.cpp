#include "devices/amaran_light/driver.h"

#include "devices/amaran_light/runtime.h"

namespace studio {

bool AmaranLightDriver::activate(const DeviceRecord& record) {
  amaran_light::AmaranRuntime* runtime = amaran_light::runtime();
  if (runtime == nullptr) return false;
  if (runtime->activate(record)) return true;
  amaran_light::releaseRuntimeIfIdle();
  return false;
}
void AmaranLightDriver::deactivate(InstanceId instanceId) {
  if (amaran_light::AmaranRuntime* runtime = amaran_light::runtimeIfActive()) {
    runtime->deactivate(instanceId);
    amaran_light::releaseRuntimeIfIdle();
  }
}
void AmaranLightDriver::loop() {
  if (amaran_light::AmaranRuntime* runtime = amaran_light::runtimeIfActive()) {
    runtime->loop();
  }
}
CommandStatus AmaranLightDriver::dispatch(const DeviceCommand& command) {
  amaran_light::AmaranRuntime* runtime = amaran_light::runtimeIfActive();
  return runtime != nullptr ? runtime->dispatch(command)
                            : CommandStatus::Unavailable;
}
DeviceRuntimeState AmaranLightDriver::runtimeState(InstanceId instanceId) const {
  amaran_light::AmaranRuntime* runtime = amaran_light::runtimeIfActive();
  return runtime != nullptr ? runtime->runtimeState(instanceId)
                            : DeviceRuntimeState{};
}
const void* AmaranLightDriver::specializedState(InstanceId instanceId) const {
  amaran_light::AmaranRuntime* runtime = amaran_light::runtimeIfActive();
  return runtime != nullptr ? runtime->state(instanceId) : nullptr;
}
void AmaranLightDriver::forgetPairing(const DeviceRecord& record) {
  amaran_light::AmaranRuntime* runtime = amaran_light::runtime();
  if (runtime == nullptr) return;
  runtime->forgetLocal(record.instanceId);
  amaran_light::releaseRuntimeIfIdle();
}
bool AmaranLightDriver::consumePairingUpdate(InstanceId instanceId,
                                             DeviceRecord& record) {
  amaran_light::AmaranRuntime* runtime = amaran_light::runtimeIfActive();
  return runtime != nullptr &&
         runtime->consumePairingUpdate(instanceId, record);
}

}  // namespace studio
