#include "devices/dji_osmo/ui.h"
#include <cstdio>
#include "core/device_manager.h"
#include "devices/dji_osmo/state.h"
#include "ui/recorder_screen_controller.h"
#include "ui/recorder_shell.h"
namespace dji_osmo_ui { namespace {
constexpr auto kOwner = recorder_shell::Owner::DjiOsmo;
studio_ui::RecorderScreenController controller(kOwner);
char verificationStatus[16] = "";
void send(studio::CommandType type) { controller.enqueue(type); }
void action() {
  const studio::InstanceId id = controller.instanceId();
  if (studio::devices().pendingAddCommitFailed(id)) { studio::devices().retryPendingAdd(id); return; }
  const auto r = studio::devices().runtimeState(id); const auto* s = static_cast<const dji_osmo::State*>(studio::devices().specializedState(id));
  if (r.link != studio::LinkState::Connected || !s || s->commandPending) return;
  send(s->recording == dji_osmo::State::Recording::Recording ? studio::CommandType::RecordStop : studio::CommandType::RecordStart);
}
void back() { controller.back(); }
void refresh() {
  const studio::InstanceId id = controller.instanceId();
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
void show(studio::InstanceId value) { recorder_shell::Options o; recorder_shell::Callbacks c; c.onBack=back; c.onAction=action; controller.show(value, o, c, refresh); }
void hide() { controller.hide(); }
void release() { controller.release(); }
bool active() { return controller.active(); }
void tick() { controller.tick(); }
void handleShortPress() { action(); } void handleLongPress() { back(); }
}  // namespace dji_osmo_ui
