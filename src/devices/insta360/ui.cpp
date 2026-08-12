#include "devices/insta360/ui.h"

#include <Arduino.h>

#include "core/device_manager.h"
#include "devices/insta360/state.h"
#include "ui/recorder_shell.h"
#include "../../ui.h"

namespace insta360_ui {
namespace {

constexpr uint32_t kAccent = 0xE53935;
constexpr uint32_t kReady = 0x2E7D5B;
constexpr auto kOwner = recorder_shell::Owner::Insta360;
studio::InstanceId instanceId = studio::kInvalidInstanceId;
bool visible = false;
uint32_t lastRefreshMs = 0;

void enqueue(studio::CommandType type) {
  if (instanceId == studio::kInvalidInstanceId) return;
  studio::DeviceCommand command;
  command.instanceId = instanceId;
  command.type = type;
  studio::devices().enqueue(command);
}

void action() {
  if (studio::devices().pendingAddCommitFailed(instanceId)) {
    studio::devices().retryPendingAdd(instanceId);
    return;
  }
  const auto runtime = studio::devices().runtimeState(instanceId);
  const auto* state = static_cast<const insta360::State*>(
      studio::devices().specializedState(instanceId));
  if (runtime.link != studio::LinkState::Connected || state == nullptr ||
      runtime.commandPending) {
    return;
  }
  if (insta360::canStopRecording(*state)) {
    enqueue(studio::CommandType::RecordStop);
  } else if (insta360::canStartRecording(*state)) {
    enqueue(studio::CommandType::RecordStart);
  }
}

void power() {
  const auto runtime = studio::devices().runtimeState(instanceId);
  const auto* state = static_cast<const insta360::State*>(
      studio::devices().specializedState(instanceId));
  if (state == nullptr || state->powerCommandPending) return;
  enqueue(runtime.link != studio::LinkState::Connected ||
                  state->power == insta360::State::Power::Off
              ? studio::CommandType::CameraPowerOn
              : studio::CommandType::CameraPowerOff);
}

void back() {
  hide();
  ui::showDeviceParent();
}

void ensureShell() {
  if (recorder_shell::ownedBy(kOwner)) return;
  if (recorder_shell::screen() != nullptr &&
      lv_scr_act() == recorder_shell::screen()) {
    ui::parkForScreenRebuild();
  }
  recorder_shell::destroyIdle();
  recorder_shell::Options options;
  options.enablePower = true;
  recorder_shell::Callbacks callbacks;
  callbacks.onBack = back;
  callbacks.onAction = action;
  callbacks.onPower = power;
  recorder_shell::acquire(kOwner, options, callbacks);
}

void refresh() {
  if (!recorder_shell::ownedBy(kOwner)) return;
  recorder_shell::View view;
  const auto* record = studio::devices().find(instanceId);
  view.title = record != nullptr ? record->displayName : "Insta360";
  const auto runtime = studio::devices().runtimeState(instanceId);
  const auto* state = static_cast<const insta360::State*>(
      studio::devices().specializedState(instanceId));

  if (studio::devices().pendingAddCommitFailed(instanceId)) {
    view.status = "COULDN'T SAVE";
    view.detail = "RETRY TO ADD DEVICE";
    view.actionLabel = "RETRY";
    view.actionEnabled = true;
    view.actionColor = kReady;
    recorder_shell::apply(view);
    return;
  }
  if (state != nullptr && state->power == insta360::State::Power::Off) {
    view.status = state->powerCommandFailed ? "WAKE FAILED" : "CAMERA OFF";
    view.detail = state->powerCommandFailed ? "TRY POWER AGAIN"
                                            : "PRESS POWER TO WAKE";
    view.actionLabel = "WAITING";
    view.powerEnabled = !state->powerCommandPending;
    recorder_shell::apply(view);
    return;
  }
  if (state != nullptr &&
      state->power == insta360::State::Power::PoweringOn) {
    view.status = "POWERING ON...";
    view.detail = "WAITING FOR CAMERA";
    view.actionLabel = "WAIT";
    recorder_shell::apply(view);
    return;
  }
  if (state != nullptr &&
      state->power == insta360::State::Power::PoweringOff) {
    view.status = "POWERING OFF...";
    view.detail = "WAITING FOR CAMERA";
    view.actionLabel = "WAIT";
    recorder_shell::apply(view);
    return;
  }
  if (runtime.link != studio::LinkState::Connected || state == nullptr) {
    view.status = runtime.link == studio::LinkState::Scanning
                      ? "PAIRING..."
                      : "DISCONNECTED";
    view.detail = state != nullptr ? "PRESS POWER TO WAKE"
                                   : "CONNECT GPS REMOTE";
    view.actionLabel = "WAITING";
    view.powerEnabled = state != nullptr && !state->powerCommandPending;
    recorder_shell::apply(view);
    return;
  }

  const bool recording = insta360::canStopRecording(*state);
  const bool photoBusy = state->photo != insta360::State::Photo::Unknown &&
                         state->photo != insta360::State::Photo::Idle;
  view.powerEnabled = !runtime.commandPending && !recording && !photoBusy;
  if (state->powerCommandFailed) {
    view.status = "POWER OFF FAILED";
    view.detail = "CAMERA STILL CONNECTED";
    view.actionLabel = "WAIT";
    view.actionEnabled = false;
    view.actionColor = kReady;
  } else if (state->recording == insta360::State::Recording::Starting) {
    view.status = "STARTING...";
    view.detail = "WAITING FOR CAMERA";
    view.actionLabel = "WAIT";
    view.actionColor = kAccent;
  } else if (state->recording == insta360::State::Recording::Stopping) {
    view.status = "STOPPING...";
    view.detail = "WAITING FOR CAMERA";
    view.actionLabel = "WAIT";
    view.actionColor = kAccent;
  } else if (recording) {
    view.status = state->lastTriggerFailed ? "STOP FAILED" : "RECORDING";
    view.detail = "CAMERA CONFIRMED";
    view.actionLabel = "STOP";
    view.actionEnabled = !runtime.commandPending;
    view.actionColor = kAccent;
  } else if (insta360::canStartRecording(*state)) {
    view.status = state->lastTriggerFailed ? "START FAILED" : "READY";
    view.detail = state->recordingConfirmed ? "CAMERA CONFIRMED"
                                            : "STATE PENDING";
    view.actionLabel = runtime.commandPending ? "WAIT" : "START";
    view.actionEnabled = !runtime.commandPending;
    view.actionColor = kReady;
  } else if (state->photo == insta360::State::Photo::Starting ||
             state->photo == insta360::State::Photo::Capturing) {
    view.status = "CAPTURING...";
    view.detail = "CAMERA CONFIRMED";
    view.actionLabel = "WAIT";
    view.actionColor = kAccent;
  } else if (state->photo == insta360::State::Photo::Saving) {
    view.status = "SAVING...";
    view.detail = "CAMERA CONFIRMED";
    view.actionLabel = "WAIT";
    view.actionColor = kAccent;
  } else if (state->photo == insta360::State::Photo::Idle) {
    view.status = "PHOTO READY";
    view.detail = "CAMERA CONFIRMED";
    view.actionLabel = "VIDEO MODE NEEDED";
    view.actionEnabled = false;
    view.actionColor = kReady;
  } else {
    view.status = "SYNCING...";
    view.detail = "WAITING FOR CAMERA STATE";
    view.actionLabel = "WAIT";
    view.actionEnabled = false;
    view.actionColor = kReady;
  }
  recorder_shell::apply(view);
}

}  // namespace

void show(studio::InstanceId id) {
  ensureShell();
  instanceId = id;
  visible = studio::devices().acquire(id, studio::ConnectionOwner::Foreground);
  if (!visible) {
    instanceId = studio::kInvalidInstanceId;
    return;
  }
  lastRefreshMs = 0;
  refresh();
  lv_scr_load(recorder_shell::screen());
  ui::releaseInactiveScreens();
}

void hide() {
  if (visible) {
    studio::devices().release(instanceId,
                              studio::ConnectionOwner::Foreground);
  }
  visible = false;
  instanceId = studio::kInvalidInstanceId;
}

void release() {
  if (!visible) recorder_shell::release(kOwner);
}

bool active() { return visible; }

void tick() {
  const uint32_t now = millis();
  if (now - lastRefreshMs >= 200) {
    lastRefreshMs = now;
    refresh();
  }
}

void handleShortPress() { action(); }
void handleLongPress() { back(); }

}  // namespace insta360_ui
