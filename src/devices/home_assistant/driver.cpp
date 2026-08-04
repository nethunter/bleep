#include "devices/home_assistant/driver.h"

namespace studio {

bool HomeAssistantDriver::activate(const DeviceRecord& record) {
  return client_.activate(record);
}

void HomeAssistantDriver::deactivate(InstanceId instanceId) {
  client_.deactivate(instanceId);
}

void HomeAssistantDriver::loop() { client_.loop(); }

CommandStatus HomeAssistantDriver::dispatch(const DeviceCommand& command) {
  return client_.dispatch(command);
}

DeviceRuntimeState HomeAssistantDriver::runtimeState(
    InstanceId instanceId) const {
  return client_.runtimeState(instanceId);
}

const void* HomeAssistantDriver::specializedState(InstanceId instanceId) const {
  return client_.entityState(instanceId);
}

}  // namespace studio
