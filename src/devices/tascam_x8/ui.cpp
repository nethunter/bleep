#include "devices/tascam_x8/ui.h"

#include <Arduino.h>

#include "core/device_manager.h"
#include "devices/tascam_x8/state.h"
#include "ui/recorder_shell.h"
#include "../../ui.h"

namespace tascam_x8_ui {

namespace {

constexpr uint32_t kAccent = 0xE53935;
constexpr uint32_t kReady = 0x2E7D5B;
constexpr auto kOwner = recorder_shell::Owner::TascamX8;

studio::InstanceId instanceId = studio::kInvalidInstanceId;
bool visible = false;
bool borrowedActivation = false;
uint32_t lastRefreshMs = 0;

void enqueue(studio::CommandType type) {
  if (instanceId == studio::kInvalidInstanceId) {
    return;
  }
  studio::DeviceCommand command;
  command.instanceId = instanceId;
  command.type = type;
  studio::devices().enqueue(command);
}

void performPrimaryAction() {
  if (studio::devices().runtimeState(instanceId).link !=
      studio::LinkState::Connected) {
    return;
  }
  const auto* state = static_cast<const tascam_x8::TascamX8State*>(
      studio::devices().specializedState(instanceId));
  if (state == nullptr || state->commandPending) {
    return;
  }
  if (state->recordingConfirmed &&
      state->recording == tascam_x8::TascamX8State::Recording::Recording) {
    enqueue(studio::CommandType::RecordStop);
  } else {
    enqueue(studio::CommandType::RecordStart);
  }
}

void onBack() {
  hide();
  ui::showDeviceParent();
}

void onAction() { performPrimaryAction(); }

void ensureShell() {
  if (recorder_shell::ownedBy(kOwner)) {
    return;
  }
  if (recorder_shell::screen() != nullptr &&
      lv_scr_act() == recorder_shell::screen()) {
    ui::parkForScreenRebuild();
  }
  recorder_shell::destroyIdle();

  recorder_shell::Options options;
  recorder_shell::Callbacks callbacks;
  callbacks.onBack = onBack;
  callbacks.onAction = onAction;
  recorder_shell::acquire(kOwner, options, callbacks);
}

void refresh() {
  if (!recorder_shell::ownedBy(kOwner)) {
    return;
  }

  recorder_shell::View view;
  const studio::DeviceRecord* record = studio::devices().find(instanceId);
  view.title = record != nullptr ? record->displayName : "";

  const studio::DeviceRuntimeState runtime =
      studio::devices().runtimeState(instanceId);
  const auto* state = static_cast<const tascam_x8::TascamX8State*>(
      studio::devices().specializedState(instanceId));

  if (runtime.link != studio::LinkState::Connected || state == nullptr) {
    view.status = "DISCONNECTED";
    if (runtime.link == studio::LinkState::Scanning) {
      view.status = "PAIRING...";
    } else if (runtime.link == studio::LinkState::Connecting) {
      view.status = "CONNECTING...";
    }
    view.detail = "AK-BT1";
    view.actionLabel = "WAITING";
    view.actionColor = kAccent;
    view.actionEnabled = false;
    recorder_shell::apply(view);
    return;
  }

  using Recording = tascam_x8::TascamX8State::Recording;
  if (state->lastCommandFailed) {
    view.status = "NO CONFIRMATION";
    view.detail = "RECORDER STATE UNKNOWN";
    view.actionLabel = "START";
    view.actionColor = kReady;
    view.actionEnabled = true;
  } else if (state->recording == Recording::Starting) {
    view.status = "STARTING...";
    view.detail = "WAITING FOR RECORDER";
    view.actionLabel = "WAIT";
    view.actionColor = kAccent;
  } else if (state->recording == Recording::Stopping) {
    view.status = "STOPPING...";
    view.detail = "WAITING FOR RECORDER";
    view.actionLabel = "WAIT";
    view.actionColor = kAccent;
  } else if (state->recordingConfirmed &&
             state->recording == Recording::Recording) {
    view.status = "RECORDING";
    view.detail = "RECORDER CONFIRMED";
    view.actionLabel = "STOP";
    view.actionColor = kAccent;
    view.actionEnabled = true;
  } else if (state->recordingConfirmed &&
             state->recording == Recording::Stopped) {
    view.status = "READY";
    view.detail = "RECORDER CONFIRMED";
    view.actionLabel = "START";
    view.actionColor = kReady;
    view.actionEnabled = true;
  } else {
    view.status = "CONNECTED";
    view.detail = "STATE UNKNOWN";
    view.actionLabel = "START";
    view.actionColor = kReady;
    view.actionEnabled = true;
  }
  recorder_shell::apply(view);
}

}  // namespace

void init() {}

void show(studio::InstanceId id, bool preserveActivation) {
  ensureShell();
  instanceId = id;
  borrowedActivation = preserveActivation;
  visible = preserveActivation ? studio::devices().isActive(id)
                               : studio::devices().activate(id);
  if (!visible) {
    borrowedActivation = false;
    instanceId = studio::kInvalidInstanceId;
    return;
  }
  lastRefreshMs = 0;
  refresh();
  lv_scr_load(recorder_shell::screen());
  ui::releaseInactiveScreens();
}

void hide() {
  if (visible && !borrowedActivation) {
    studio::devices().deactivate();
  }
  visible = false;
  borrowedActivation = false;
  instanceId = studio::kInvalidInstanceId;
}

void release() {
  if (visible) {
    return;
  }
  recorder_shell::release(kOwner);
}

bool active() { return visible; }

void tick() {
  const uint32_t now = millis();
  if (now - lastRefreshMs >= 200) {
    lastRefreshMs = now;
    refresh();
  }
}

void handleShortPress() { performPrimaryAction(); }

void handleLongPress() { onBack(); }

}  // namespace tascam_x8_ui
