#include "devices/dji_osmo/ui.h"
#include <Arduino.h>
#include <cstdio>
#include "core/device_manager.h"
#include "devices/dji_osmo/state.h"
#include "ui/recorder_shell.h"
#include "../../ui.h"
namespace dji_osmo_ui { namespace {
constexpr auto kOwner = recorder_shell::Owner::DjiOsmo;
studio::InstanceId id = studio::kInvalidInstanceId; bool visible = false; uint32_t refreshed = 0;
char verificationStatus[16] = "";
void send(studio::CommandType type) { studio::DeviceCommand c; c.instanceId = id; c.type = type; studio::devices().enqueue(c); }
void action() {
  if (studio::devices().pendingAddCommitFailed(id)) { studio::devices().retryPendingAdd(id); return; }
  const auto r = studio::devices().runtimeState(id); const auto* s = static_cast<const dji_osmo::State*>(studio::devices().specializedState(id));
  if (r.link != studio::LinkState::Connected || !s || s->commandPending) return;
  send(s->recording == dji_osmo::State::Recording::Recording ? studio::CommandType::RecordStop : studio::CommandType::RecordStart);
}
void back() { hide(); ui::showDeviceParent(); }
void shell() { if (recorder_shell::ownedBy(kOwner)) return; if (recorder_shell::screen() && lv_scr_act() == recorder_shell::screen()) ui::parkForScreenRebuild(); recorder_shell::destroyIdle(); recorder_shell::Options o; recorder_shell::Callbacks c; c.onBack = back; c.onAction = action; recorder_shell::acquire(kOwner, o, c); }
void refresh() {
  if (!recorder_shell::ownedBy(kOwner)) return; recorder_shell::View v; const auto* rec = studio::devices().find(id); v.title = rec ? rec->displayName : "DJI Osmo";
  const auto r = studio::devices().runtimeState(id); const auto* s = static_cast<const dji_osmo::State*>(studio::devices().specializedState(id));
  if (studio::devices().pendingAddCommitFailed(id)) { v.status="COULDN'T SAVE"; v.detail="RETRY TO ADD DEVICE"; v.actionLabel="RETRY"; v.actionEnabled=true; v.actionColor=0x2E7D5B; }
  else if (s && s->verificationPending) { std::snprintf(verificationStatus, sizeof(verificationStatus), "VERIFY %04u", static_cast<unsigned>(s->verificationCode)); v.status=verificationStatus; v.detail="MATCH CAMERA CODE"; }
  else if (r.link != studio::LinkState::Connected || !s) { v.status = r.link==studio::LinkState::Scanning ? "PAIRING..." : r.link==studio::LinkState::Connecting ? "CONNECTING..." : "DISCONNECTED"; v.detail="ENABLE DJI REMOTE"; }
  else if (s->lastCommandFailed) { v.status="COMMAND REJECTED"; v.detail="CHECK CAMERA MODE"; v.actionLabel="START"; v.actionEnabled=true; v.actionColor=0x2E7D5B; }
  else if (s->commandPending) { v.status="WAITING FOR DJI"; v.detail="COMMAND PENDING"; v.actionLabel="WAIT"; }
  else if (s->recording == dji_osmo::State::Recording::Recording) { v.status="RECORDING"; v.detail=s->statusConfirmed ? "CAMERA CONFIRMED" : "COMMAND ACCEPTED"; v.actionLabel="STOP"; v.actionEnabled=true; }
  else { v.status="READY"; v.detail=s->statusConfirmed ? "CAMERA CONFIRMED" : "STATUS PENDING"; v.actionLabel="START"; v.actionEnabled=true; v.actionColor=0x2E7D5B; }
  recorder_shell::apply(v);
}
}
void show(studio::InstanceId value) { shell(); id=value; visible=studio::devices().acquire(id, studio::ConnectionOwner::Foreground); if (!visible) { id=studio::kInvalidInstanceId; return; } refreshed=0; refresh(); lv_scr_load(recorder_shell::screen()); ui::releaseInactiveScreens(); }
void hide() { if (visible) studio::devices().release(id, studio::ConnectionOwner::Foreground); visible=false; id=studio::kInvalidInstanceId; }
void release() { if (!visible) recorder_shell::release(kOwner); }
bool active() { return visible; }
void tick() { uint32_t now=millis(); if (now-refreshed>=200) { refreshed=now; refresh(); } }
void handleShortPress() { action(); } void handleLongPress() { back(); }
}  // namespace dji_osmo_ui
