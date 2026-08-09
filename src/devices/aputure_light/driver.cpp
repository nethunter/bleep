#include "devices/aputure_light/driver.h"

#include "devices/aputure_light/runtime.h"

namespace studio {

bool AputureLightDriver::activate(const DeviceRecord& record) {
  aputure_light::AputureLightRuntime* runtime = aputure_light::runtime();
  if (runtime == nullptr) return false;
  if (runtime->activate(record)) return true;
  aputure_light::releaseRuntimeIfIdle();
  return false;
}
void AputureLightDriver::deactivate(InstanceId instanceId) {
  if (aputure_light::AputureLightRuntime* runtime = aputure_light::runtimeIfActive()) {
    runtime->deactivate(instanceId);
    aputure_light::releaseRuntimeIfIdle();
  }
}
void AputureLightDriver::loop() {
  if (aputure_light::AputureLightRuntime* runtime = aputure_light::runtimeIfActive()) {
    runtime->loop();
  }
}
CommandStatus AputureLightDriver::dispatch(const DeviceCommand& command) {
  aputure_light::AputureLightRuntime* runtime = aputure_light::runtimeIfActive();
  return runtime != nullptr ? runtime->dispatch(command)
                            : CommandStatus::Unavailable;
}
DeviceRuntimeState AputureLightDriver::runtimeState(InstanceId instanceId) const {
  aputure_light::AputureLightRuntime* runtime = aputure_light::runtimeIfActive();
  return runtime != nullptr ? runtime->runtimeState(instanceId)
                            : DeviceRuntimeState{};
}
const void* AputureLightDriver::specializedState(InstanceId instanceId) const {
  aputure_light::AputureLightRuntime* runtime = aputure_light::runtimeIfActive();
  return runtime != nullptr ? runtime->state(instanceId) : nullptr;
}
void AputureLightDriver::forgetPairing(const DeviceRecord& record) {
  aputure_light::AputureLightRuntime* runtime = aputure_light::runtime();
  if (runtime == nullptr) return;
  runtime->forgetLocal(record.instanceId);
  aputure_light::releaseRuntimeIfIdle();
}
bool AputureLightDriver::consumePairingUpdate(InstanceId instanceId,
                                             DeviceRecord& record) {
  aputure_light::AputureLightRuntime* runtime = aputure_light::runtimeIfActive();
  return runtime != nullptr &&
         runtime->consumePairingUpdate(instanceId, record);
}

}  // namespace studio
