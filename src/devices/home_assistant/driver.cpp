#include "devices/home_assistant/driver.h"

#include <new>
#include <cstring>

namespace studio {

bool HomeAssistantDriver::activate(const DeviceRecord& record) {
  if (client_ == nullptr) {
    client_ = new (std::nothrow) home_assistant::HomeAssistantClient;
    if (client_ == nullptr) return false;
  }
  const bool alreadyActive = client_->entityState(record.instanceId) != nullptr;
  if (!client_->activate(record)) {
    if (activeCount_ == 0) {
      delete client_;
      client_ = nullptr;
    }
    return false;
  }
  if (!alreadyActive) ++activeCount_;
  return true;
}

void HomeAssistantDriver::deactivate(InstanceId instanceId) {
  if (client_ == nullptr) return;
  const bool active = client_->entityState(instanceId) != nullptr;
  client_->deactivate(instanceId);
  if (active && activeCount_ > 0) --activeCount_;
  if (activeCount_ == 0) {
    delete client_;
    client_ = nullptr;
  }
}

void HomeAssistantDriver::loop() {
  if (client_ != nullptr) client_->loop();
}

CommandStatus HomeAssistantDriver::dispatch(const DeviceCommand& command) {
  return client_ != nullptr ? client_->dispatch(command)
                            : CommandStatus::Unavailable;
}

DeviceRuntimeState HomeAssistantDriver::runtimeState(
    InstanceId instanceId) const {
  return client_ != nullptr ? client_->runtimeState(instanceId)
                            : DeviceRuntimeState{};
}

const void* HomeAssistantDriver::specializedState(InstanceId instanceId) const {
  return client_ != nullptr ? client_->entityState(instanceId) : nullptr;
}

bool HomeAssistantDriver::lightControlState(InstanceId instanceId,
                                            LightControlState& out) const {
  const home_assistant::EntityState* state =
      client_ != nullptr ? client_->entityState(instanceId) : nullptr;
  if (state == nullptr) return false;
  out.available = state->available;
  out.supportsPower = true;
  out.on = state->on;
  out.stateKnown = state->stateKnown;
  out.commandPending = state->commandPending;
  out.commandFailed = state->commandAttempted &&
                      state->lastCommand != CommandStatus::Succeeded &&
                      state->lastCommand != CommandStatus::Queued;
  out.quality = state->stateKnown ? StateQuality::Confirmed : StateQuality::Unknown;
  std::strncpy(out.status, state->available ? state->stateText : "Unavailable",
               sizeof(out.status) - 1);
  return true;
}

}  // namespace studio
