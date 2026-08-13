#include "devices/gopro/ui.h"

#include <Arduino.h>

#include "core/device_manager.h"
#include "devices/gopro/state.h"
#include "ui/recorder_shell.h"
#include "../../ui.h"

namespace gopro_ui {
namespace {
constexpr uint32_t kAccent = 0xE53935;
constexpr uint32_t kReady = 0x2E7D5B;
constexpr auto kOwner = recorder_shell::Owner::GoPro;
studio::InstanceId instanceId = studio::kInvalidInstanceId;
bool visible = false;
uint32_t lastRefreshMs = 0;

void enqueue(studio::CommandType type) {
  studio::DeviceCommand command;
  command.instanceId = instanceId;
  command.type = type;
  studio::devices().enqueue(command);
}

void performPrimaryAction() {
  if (studio::devices().pendingAddCommitFailed(instanceId)) {
    studio::devices().retryPendingAdd(instanceId);
    return;
  }
  const auto runtime = studio::devices().runtimeState(instanceId);
  const auto* state = static_cast<const gopro::GoProState*>(
      studio::devices().specializedState(instanceId));
  if (runtime.link != studio::LinkState::Connected || state == nullptr ||
      state->commandPending) return;
  enqueue(state->recording == gopro::GoProState::Recording::Recording
              ? studio::CommandType::RecordStop
              : studio::CommandType::RecordStart);
}

void onBack() { hide(); ui::showDeviceParent(); }
void onAction() { performPrimaryAction(); }

void ensureShell() {
  if (recorder_shell::ownedBy(kOwner)) return;
  if (recorder_shell::screen() != nullptr &&
      lv_scr_act() == recorder_shell::screen()) ui::parkForScreenRebuild();
  recorder_shell::destroyIdle();
  recorder_shell::Options options;
  recorder_shell::Callbacks callbacks;
  callbacks.onBack = onBack;
  callbacks.onAction = onAction;
  recorder_shell::acquire(kOwner, options, callbacks);
}

void refresh() {
  if (!recorder_shell::ownedBy(kOwner)) return;
  recorder_shell::View view;
  const auto* record = studio::devices().find(instanceId);
  view.title = record != nullptr ? record->displayName : "GoPro";
  const auto runtime = studio::devices().runtimeState(instanceId);
  const auto* state = static_cast<const gopro::GoProState*>(
      studio::devices().specializedState(instanceId));
  if (studio::devices().pendingAddCommitFailed(instanceId)) {
    view.status = "COULDN'T SAVE";
    view.detail = "RETRY TO ADD DEVICE";
    view.actionLabel = "RETRY";
    view.actionColor = kReady;
    view.actionEnabled = true;
  } else if (runtime.link != studio::LinkState::Connected || state == nullptr) {
    view.status = runtime.link == studio::LinkState::Scanning
                      ? "PAIRING..."
                      : runtime.link == studio::LinkState::Connecting
                            ? "CONNECTING..." : "DISCONNECTED";
    view.detail = "OPEN GOPRO BLE";
    view.actionLabel = "WAITING";
    view.actionColor = kAccent;
  } else if (state->lastCommandFailed) {
    view.status = "NO CONFIRMATION";
    view.detail = "CAMERA STATE UNKNOWN";
    view.actionLabel = "START";
    view.actionColor = kReady;
    view.actionEnabled = true;
  } else if (state->commandPending) {
    view.status = state->recording == gopro::GoProState::Recording::Starting
                      ? "STARTING..." : "STOPPING...";
    view.detail = "WAITING FOR GOPRO";
    view.actionLabel = "WAIT";
    view.actionColor = kAccent;
  } else if (state->recording == gopro::GoProState::Recording::Recording) {
    view.status = "RECORDING";
    view.detail = "GOPRO CONFIRMED";
    view.actionLabel = "STOP";
    view.actionColor = kAccent;
    view.actionEnabled = true;
  } else {
    view.status = "READY";
    view.detail = state->recordingConfirmed ? "GOPRO CONFIRMED" : "STATE UNKNOWN";
    view.actionLabel = "START";
    view.actionColor = kReady;
    view.actionEnabled = true;
  }
  recorder_shell::apply(view);
}
}  // namespace

void show(studio::InstanceId id) {
  ensureShell();
  instanceId = id;
  visible = studio::devices().acquire(id, studio::ConnectionOwner::Foreground);
  if (!visible) { instanceId = studio::kInvalidInstanceId; return; }
  lastRefreshMs = 0;
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
void handleShortPress() { performPrimaryAction(); }
void handleLongPress() { onBack(); }
}  // namespace gopro_ui
