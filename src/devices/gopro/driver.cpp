#include "devices/gopro/driver.h"

#include <cstring>
#include <new>

namespace studio {

GoProDriver::Session* GoProDriver::sessionFor(InstanceId id) {
  for (Session* session : sessions_)
    if (session != nullptr && session->instanceId == id) return session;
  return nullptr;
}

const GoProDriver::Session* GoProDriver::sessionFor(InstanceId id) const {
  for (const Session* session : sessions_)
    if (session != nullptr && session->instanceId == id) return session;
  return nullptr;
}

bool GoProDriver::activate(const DeviceRecord& record) {
  if (sessionFor(record.instanceId) != nullptr) return true;
  for (Session*& session : sessions_) {
    if (session != nullptr) continue;
    session = new (std::nothrow) Session;
    if (session == nullptr) return false;
    session->instanceId = record.instanceId;
    session->client.activate(record.bleAddress, record.bleAddressType,
                             record.bleName[0] != '\0' ? record.bleName
                                                        : record.displayName,
                             record.paired);
    return true;
  }
  return false;
}

bool GoProDriver::resume(const DeviceRecord& record) {
  Session* session = sessionFor(record.instanceId);
  if (session == nullptr) return false;
  const auto power = session->client.state().power;
  if (power == gopro::GoProState::Power::Asleep ||
      power == gopro::GoProState::Power::SleepFailed)
    return session->client.powerOn();
  return true;
}

bool GoProDriver::retainWhileDisconnected(InstanceId id) const {
  const Session* session = sessionFor(id);
  if (session == nullptr) return false;
  const auto power = session->client.state().power;
  return power == gopro::GoProState::Power::Sleeping ||
         power == gopro::GoProState::Power::Asleep ||
         power == gopro::GoProState::Power::SleepFailed;
}

void GoProDriver::deactivate(InstanceId id) {
  Session* session = sessionFor(id);
  if (session == nullptr) return;
  session->client.deactivate();
  for (Session*& candidate : sessions_) {
    if (candidate == session) {
      delete candidate;
      candidate = nullptr;
      return;
    }
  }
}

void GoProDriver::loop() {
  for (Session* session : sessions_)
    if (session != nullptr) session->client.loop();
}

CommandStatus GoProDriver::dispatch(const DeviceCommand& command) {
  Session* session = sessionFor(command.instanceId);
  if (session == nullptr) return CommandStatus::Unavailable;
  switch (command.type) {
    case CommandType::Connect:
      session->client.startScan();
      return CommandStatus::Succeeded;
    case CommandType::ForgetPairing:
      session->client.forgetDevice();
      return CommandStatus::Succeeded;
    case CommandType::RecordStart:
      return session->client.setShutter(true) ? CommandStatus::Succeeded
                                              : CommandStatus::Unavailable;
    case CommandType::RecordStop:
      return session->client.setShutter(false) ? CommandStatus::Succeeded
                                               : CommandStatus::Unavailable;
    case CommandType::CameraPowerOn:
      return session->client.powerOn() ? CommandStatus::Succeeded
                                       : CommandStatus::Unavailable;
    case CommandType::CameraPowerOff:
      return session->client.powerOff() ? CommandStatus::Succeeded
                                        : CommandStatus::Unavailable;
    default:
      return CommandStatus::Unsupported;
  }
}

DeviceRuntimeState GoProDriver::runtimeState(InstanceId id) const {
  DeviceRuntimeState runtime;
  const Session* session = sessionFor(id);
  if (session == nullptr) return runtime;
  switch (session->client.state().link) {
    case gopro::GoProState::Link::Disconnected: runtime.link = LinkState::Disconnected; break;
    case gopro::GoProState::Link::Scanning: runtime.link = LinkState::Scanning; break;
    case gopro::GoProState::Link::Connecting: runtime.link = LinkState::Connecting; break;
    case gopro::GoProState::Link::Connected: runtime.link = LinkState::Connected; break;
  }
  runtime.protocolReady = session->client.protocolReady();
  runtime.commandPending = session->client.state().commandPending ||
                           session->client.state().powerCommandPending;
  runtime.commandFailed = session->client.state().lastCommandFailed;
  runtime.recordingConfirmed = session->client.state().recordingConfirmed;
  runtime.recording = session->client.state().recording ==
                      gopro::GoProState::Recording::Recording;
  runtime.quality = session->client.state().recordingConfirmed
                        ? StateQuality::Confirmed
                        : StateQuality::Unknown;
  return runtime;
}

const void* GoProDriver::specializedState(InstanceId id) const {
  const Session* session = sessionFor(id);
  return session != nullptr ? &session->client.state() : nullptr;
}

void GoProDriver::forgetPairing(const DeviceRecord& record) {
  Session* session = sessionFor(record.instanceId);
  if (session != nullptr) session->client.forgetBond(record.bleAddress, record.bleAddressType);
  else {
    gopro::GoProClient transient;
    transient.forgetBond(record.bleAddress, record.bleAddressType);
  }
}

bool GoProDriver::cancelOnboarding(const DeviceRecord& record) {
  Session* session = sessionFor(record.instanceId);
  if (session != nullptr) session->client.forgetDevice();
  else forgetPairing(record);
  return true;
}

bool GoProDriver::consumePairingUpdate(InstanceId id, DeviceRecord& record) {
  Session* session = sessionFor(id);
  char address[kBleAddressCapacity] = "";
  char name[kBleNameCapacity] = "";
  uint8_t addressType = 0;
  bool paired = false;
  if (session == nullptr || !session->client.consumePairingUpdate(
                                address, sizeof(address), addressType, name,
                                sizeof(name), paired)) return false;
  record.paired = paired;
  record.bleAddressType = addressType;
  std::strncpy(record.bleAddress, address, sizeof(record.bleAddress) - 1);
  std::strncpy(record.bleName, name, sizeof(record.bleName) - 1);
  return true;
}

}  // namespace studio
