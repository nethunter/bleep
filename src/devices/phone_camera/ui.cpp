#include "devices/phone_camera/ui.h"

#include <Arduino.h>

#include "core/device_manager.h"
#include "devices/phone_camera/state.h"
#include "ui/recorder_shell.h"
#include "../../ui.h"

namespace phone_camera_ui {
namespace {
constexpr auto kOwner = recorder_shell::Owner::PhoneCamera;
constexpr uint32_t kAccent = 0xE53935;
studio::InstanceId instanceId = studio::kInvalidInstanceId;
bool visible = false;
uint32_t lastRefreshMs = 0;

void trigger() {
  const auto runtime = studio::devices().runtimeState(instanceId);
  if (runtime.link != studio::LinkState::Connected || runtime.commandPending) return;
  studio::DeviceCommand command;
  command.instanceId = instanceId;
  command.type = studio::CommandType::RecordTrigger;
  studio::devices().enqueue(command);
}
void onBack() { hide(); ui::showDeviceParent(); }
void ensureShell() {
  if (recorder_shell::ownedBy(kOwner)) return;
  if (recorder_shell::screen() != nullptr &&
      lv_scr_act() == recorder_shell::screen()) ui::parkForScreenRebuild();
  recorder_shell::destroyIdle();
  recorder_shell::Options options;
  recorder_shell::Callbacks callbacks;
  callbacks.onBack = onBack;
  callbacks.onAction = trigger;
  recorder_shell::acquire(kOwner, options, callbacks);
}
void refresh() {
  if (!recorder_shell::ownedBy(kOwner)) return;
  recorder_shell::View view;
  const auto* record = studio::devices().find(instanceId);
  view.title = record != nullptr ? record->displayName : "Phone Camera";
  const auto runtime = studio::devices().runtimeState(instanceId);
  const auto* state = static_cast<const phone_camera::PhoneCameraState*>(
      studio::devices().specializedState(instanceId));
  if (runtime.link == studio::LinkState::Connected && state != nullptr) {
    view.status = state->triggerPending ? "SENDING..." : "READY";
    view.detail = state->lastTriggerFailed
                      ? "SHUTTER FAILED"
                      : (state->lastTriggerSucceeded ? "SHUTTER SENT"
                                                     : "VOLUME-UP SHUTTER");
    view.actionLabel = state->triggerPending ? "WAIT" : "SHUTTER";
    view.actionEnabled = !state->triggerPending;
  } else {
    view.status = runtime.link == studio::LinkState::Connecting
                      ? "PAIRING..." : "ADVERTISING...";
    view.detail = "PAIR BLE(E)P SHUTTER\nIN PHONE SETTINGS";
    view.actionLabel = "WAITING";
  }
  view.actionColor = kAccent;
  recorder_shell::apply(view);
}
}  // namespace
void show(studio::InstanceId id) {
  ensureShell();
  instanceId = id;
  visible = studio::devices().acquire(id, studio::ConnectionOwner::Foreground);
  if (!visible) { instanceId = studio::kInvalidInstanceId; return; }
  refresh();
  lv_scr_load(recorder_shell::screen());
  ui::releaseInactiveScreens();
}
void hide() {
  if (visible) studio::devices().release(instanceId, studio::ConnectionOwner::Foreground);
  visible = false;
  instanceId = studio::kInvalidInstanceId;
}
void release() { if (!visible) recorder_shell::release(kOwner); }
bool active() { return visible; }
void tick() { const uint32_t now = millis(); if (now - lastRefreshMs >= 200) { lastRefreshMs = now; refresh(); } }
void handleShortPress() { trigger(); }
void handleLongPress() { onBack(); }
}  // namespace phone_camera_ui
