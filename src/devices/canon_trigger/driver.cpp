#include "devices/canon_trigger/driver.h"

#include <cstring>

namespace studio {

void CanonTriggerDriver::activate(const DeviceRecord& record) {
  activeInstance_ = record.instanceId;
  active_ = true;
  client_.activate(record.bleAddress, record.bleAddressType,
                   record.bleName[0] != '\0' ? record.bleName
                                             : record.displayName,
                   record.paired);
}

void CanonTriggerDriver::deactivate() {
  if (active_) {
    client_.deactivate();
  }
  active_ = false;
  activeInstance_ = kInvalidInstanceId;
}

void CanonTriggerDriver::loop() {
  if (active_) {
    client_.loop();
  }
}

CommandStatus CanonTriggerDriver::dispatch(const DeviceCommand& command) {
  if (!active_ || command.instanceId != activeInstance_) {
    return CommandStatus::Unavailable;
  }
  switch (command.type) {
    case CommandType::Connect:
      client_.startScan();
      return CommandStatus::Succeeded;
    case CommandType::ForgetPairing:
      client_.forgetDevice();
      return CommandStatus::Succeeded;
    case CommandType::RecordTrigger:
      return client_.triggerRecord() ? CommandStatus::Succeeded
                                     : CommandStatus::Unavailable;
    default:
      return CommandStatus::Unsupported;
  }
}

DeviceRuntimeState CanonTriggerDriver::runtimeState() const {
  DeviceRuntimeState state;
  switch (client_.state().link) {
    case canon_trigger::CanonTriggerState::Link::Disconnected:
      state.link = LinkState::Disconnected;
      break;
    case canon_trigger::CanonTriggerState::Link::Scanning:
      state.link = LinkState::Scanning;
      break;
    case canon_trigger::CanonTriggerState::Link::Connecting:
      state.link = LinkState::Connecting;
      break;
    case canon_trigger::CanonTriggerState::Link::Connected:
      state.link = LinkState::Connected;
      break;
  }
  state.protocolReady = client_.protocolReady();
  state.quality = StateQuality::Unknown;
  return state;
}

void CanonTriggerDriver::forgetPairing(const DeviceRecord& record) {
  client_.forgetBond(record.bleAddress, record.bleAddressType);
}

bool CanonTriggerDriver::consumePairingUpdate(DeviceRecord& record) {
  char address[kBleAddressCapacity] = "";
  char name[kBleNameCapacity] = "";
  uint8_t addressType = 0;
  bool paired = false;
  if (!client_.consumePairingUpdate(address, sizeof(address), addressType, name,
                                    sizeof(name), paired)) {
    return false;
  }
  record.paired = paired;
  record.bleAddressType = addressType;
  std::strncpy(record.bleAddress, address, sizeof(record.bleAddress) - 1);
  std::strncpy(record.bleName, name, sizeof(record.bleName) - 1);
  return true;
}

}  // namespace studio
