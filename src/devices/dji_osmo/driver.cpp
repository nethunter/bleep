#include "devices/dji_osmo/driver.h"

#include <cstring>
#include <new>

namespace studio {

DjiOsmoDriver::Session* DjiOsmoDriver::sessionFor(InstanceId id) {
  for (Session* session : sessions_) if (session != nullptr && session->instanceId == id) return session;
  return nullptr;
}
const DjiOsmoDriver::Session* DjiOsmoDriver::sessionFor(InstanceId id) const {
  for (const Session* session : sessions_) if (session != nullptr && session->instanceId == id) return session;
  return nullptr;
}
bool DjiOsmoDriver::activate(const DeviceRecord& record) {
  if (sessionFor(record.instanceId) != nullptr) return true;
  for (Session*& session : sessions_) {
    if (session != nullptr) continue;
    session = new (std::nothrow) Session;
    if (session == nullptr) return false;
    session->instanceId = record.instanceId;
    session->client.activate(record.bleAddress, record.bleAddressType,
                             record.bleName[0] ? record.bleName : record.displayName,
                             record.paired, 0xB1EE0000UL | (record.instanceId & 0xffff));
    return true;
  }
  return false;
}
void DjiOsmoDriver::deactivate(InstanceId id) {
  Session* session = sessionFor(id); if (session == nullptr) return;
  session->client.deactivate();
  for (Session*& slot : sessions_) if (slot == session) { delete slot; slot = nullptr; return; }
}
void DjiOsmoDriver::loop() { for (Session* session : sessions_) if (session != nullptr) session->client.loop(); }
CommandStatus DjiOsmoDriver::dispatch(const DeviceCommand& command) {
  Session* session = sessionFor(command.instanceId); if (session == nullptr) return CommandStatus::Unavailable;
  switch (command.type) {
    case CommandType::Connect: session->client.startScan(); return CommandStatus::Succeeded;
    case CommandType::ForgetPairing: session->client.forgetDevice(); return CommandStatus::Succeeded;
    case CommandType::RecordStart: return session->client.setRecording(true) ? CommandStatus::Succeeded : CommandStatus::Unavailable;
    case CommandType::RecordStop: return session->client.setRecording(false) ? CommandStatus::Succeeded : CommandStatus::Unavailable;
    default: return CommandStatus::Unsupported;
  }
}
DeviceRuntimeState DjiOsmoDriver::runtimeState(InstanceId id) const {
  DeviceRuntimeState runtime; const Session* session = sessionFor(id); if (session == nullptr) return runtime;
  const auto& state = session->client.state();
  switch (state.link) {
    case dji_osmo::State::Link::Disconnected: runtime.link = LinkState::Disconnected; break;
    case dji_osmo::State::Link::Scanning: runtime.link = LinkState::Scanning; break;
    case dji_osmo::State::Link::Connecting: runtime.link = LinkState::Connecting; break;
    case dji_osmo::State::Link::Connected: runtime.link = LinkState::Connected; break;
  }
  runtime.protocolReady = session->client.protocolReady();
  runtime.commandPending = state.commandPending; runtime.commandFailed = state.lastCommandFailed;
  runtime.recordingConfirmed = state.statusConfirmed;
  runtime.recording = state.recording == dji_osmo::State::Recording::Recording;
  runtime.quality = state.statusConfirmed ? StateQuality::Confirmed : StateQuality::Optimistic;
  return runtime;
}
const void* DjiOsmoDriver::specializedState(InstanceId id) const { const Session* s = sessionFor(id); return s ? &s->client.state() : nullptr; }
void DjiOsmoDriver::forgetPairing(const DeviceRecord& record) { Session* s = sessionFor(record.instanceId); if (s) s->client.forgetBond(record.bleAddress, record.bleAddressType); }
void DjiOsmoDriver::cancelOnboarding(const DeviceRecord& record) { Session* s = sessionFor(record.instanceId); if (s) s->client.forgetDevice(); else forgetPairing(record); }
bool DjiOsmoDriver::consumePairingUpdate(InstanceId id, DeviceRecord& record) {
  Session* s = sessionFor(id); char address[kBleAddressCapacity] = ""; char name[kBleNameCapacity] = ""; uint8_t type = 0; bool paired = false;
  if (!s || !s->client.consumePairingUpdate(address, sizeof(address), type, name, sizeof(name), paired)) return false;
  record.paired = paired; record.bleAddressType = type;
  std::strncpy(record.bleAddress, address, sizeof(record.bleAddress) - 1);
  std::strncpy(record.bleName, name, sizeof(record.bleName) - 1);
  return true;
}
}  // namespace studio
