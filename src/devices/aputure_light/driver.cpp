#include "devices/aputure_light/driver.h"

#include "devices/aputure_light/runtime.h"

#include <cstring>

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
void AputureLightDriver::cancelPendingCommand(InstanceId instanceId) {
  if (aputure_light::AputureLightRuntime* runtime =
          aputure_light::runtimeIfActive()) {
    runtime->cancelPendingCommand(instanceId);
  }
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
bool AputureLightDriver::lightControlState(InstanceId instanceId,
                                          LightControlState& out) const {
  aputure_light::AputureLightRuntime* runtime = aputure_light::runtimeIfActive();
  const aputure_light::AputureLightState* state =
      runtime != nullptr ? runtime->state(instanceId) : nullptr;
  if (state == nullptr) return false;
  const DeviceRuntimeState runtimeState = runtime->runtimeState(instanceId);
  out.available = state->phase == aputure_light::AputureLightState::Phase::Ready;
  out.supportsPower = true;
  out.supportsCct = out.supportsRgb = out.supportsTint = true;
  out.on = state->on;
  out.stateKnown = state->powerConfirmed && state->nodeReachable;
  out.commandPending = state->commandPending;
  out.commandFailed = state->lastCommandFailed;
  out.quality = runtimeState.quality;
  out.minKelvin = 2300;
  out.maxKelvin = 10000;
  out.kelvin = state->kelvin;
  out.tintPermille = state->tintPermille;
  out.brightness = state->mode == aputure_light::AputureLightState::Mode::Rgb
                       ? state->rgbBrightness
                       : state->cctBrightness;
  out.cctBrightness = state->cctBrightness;
  out.rgbBrightness = state->rgbBrightness;
  out.rgb = state->rgb;
  out.rgbMode = state->mode == aputure_light::AputureLightState::Mode::Rgb;
  std::strncpy(out.status, state->error[0] != '\0' ? state->error
                                                    : (out.stateKnown ? "Ready / confirmed"
                                                                      : "Ready / optimistic"),
               sizeof(out.status) - 1);
  return true;
}
void AputureLightDriver::forgetPairing(const DeviceRecord& record) {
  aputure_light::AputureLightRuntime* runtime = aputure_light::runtime();
  if (runtime == nullptr) return;
  runtime->forgetLocal(record.instanceId);
  aputure_light::releaseRuntimeIfIdle();
}
bool AputureLightDriver::cancelOnboarding(const DeviceRecord& record) {
  if (aputure_light::AputureLightRuntime* runtime =
          aputure_light::runtimeIfActive()) {
    return runtime->cancelOnboarding(record.instanceId);
  }
  return true;
}
size_t AputureLightDriver::onboardingCandidateCount(
    InstanceId instanceId) const {
  aputure_light::AputureLightRuntime* runtime =
      aputure_light::runtimeIfActive();
  return runtime != nullptr ? runtime->onboardingCandidateCount(instanceId) : 0;
}
bool AputureLightDriver::onboardingCandidate(
    InstanceId instanceId, size_t index,
    OnboardingCandidate& candidate) const {
  aputure_light::AputureLightRuntime* runtime =
      aputure_light::runtimeIfActive();
  return runtime != nullptr &&
         runtime->onboardingCandidate(instanceId, index, candidate);
}
bool AputureLightDriver::selectOnboardingCandidate(InstanceId instanceId,
                                                   uint32_t token) {
  aputure_light::AputureLightRuntime* runtime =
      aputure_light::runtimeIfActive();
  return runtime != nullptr &&
         runtime->selectOnboardingCandidate(instanceId, token);
}
bool AputureLightDriver::consumePairingUpdate(InstanceId instanceId,
                                             DeviceRecord& record) {
  aputure_light::AputureLightRuntime* runtime = aputure_light::runtimeIfActive();
  return runtime != nullptr &&
         runtime->consumePairingUpdate(instanceId, record);
}

}  // namespace studio
