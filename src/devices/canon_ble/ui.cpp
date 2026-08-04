#include "devices/canon_ble/ui.h"

#include <Arduino.h>

#include "core/device_manager.h"
#include "devices/canon_ble/state.h"
#include "ui/recorder_shell.h"
#include "../../ui.h"

namespace canon_ble_ui {

namespace {

constexpr uint32_t kAccent = 0xE53935;
constexpr uint32_t kReady = 0x2E7D5B;
constexpr auto kOwner = recorder_shell::Owner::CanonBle;

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
  const auto* state = static_cast<const canon_ble::CanonBleState*>(
      studio::devices().specializedState(instanceId));
  if (state == nullptr ||
      state->phase != canon_ble::CanonBleState::Phase::Ready ||
      state->commandPending) {
    return;
  }
  if (state->recordingConfirmed &&
      state->recording == canon_ble::CanonBleState::Recording::Recording) {
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

void onUnknownStart() { enqueue(studio::CommandType::RecordStart); }

void onUnknownStop() { enqueue(studio::CommandType::RecordStop); }

void onPower() {
  const auto* state = static_cast<const canon_ble::CanonBleState*>(
      studio::devices().specializedState(instanceId));
  enqueue(state != nullptr &&
                  state->phase == canon_ble::CanonBleState::Phase::PoweredOff
              ? studio::CommandType::CameraPowerOn
              : studio::CommandType::CameraPowerOff);
}

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
  options.enablePower = true;
  options.enableUnknownControls = true;
  recorder_shell::Callbacks callbacks;
  callbacks.onBack = onBack;
  callbacks.onAction = onAction;
  callbacks.onUnknownStart = onUnknownStart;
  callbacks.onUnknownStop = onUnknownStop;
  callbacks.onPower = onPower;
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
  const auto* state = static_cast<const canon_ble::CanonBleState*>(
      studio::devices().specializedState(instanceId));

  if (runtime.link != studio::LinkState::Connected || state == nullptr) {
    view.status = "DISCONNECTED";
    view.detail = "SMARTPHONE BLE";
    if (state != nullptr &&
        state->phase == canon_ble::CanonBleState::Phase::PoweredOff) {
      view.status = "CAMERA OFF";
      view.detail = "PRESS POWER TO WAKE";
    } else if (runtime.link == studio::LinkState::Scanning) {
      if (state != nullptr && state->claimedPeerVisible) {
        view.status = "ALREADY ADDED";
        view.detail = "OPEN EXISTING DEVICE";
      } else {
        view.status = "PAIRING...";
        view.detail = "SELECT OK ON CAMERA";
      }
    } else if (runtime.link == studio::LinkState::Connecting) {
      view.status = "CONNECTING...";
      view.detail = "SECURE HANDSHAKE";
    } else if (state != nullptr && state->pairingRejected) {
      view.status = "PAIRING REJECTED";
      view.detail = "TRY AGAIN";
    }
    view.powerEnabled =
        state != nullptr &&
        state->phase == canon_ble::CanonBleState::Phase::PoweredOff;
    view.actionLabel = "WAITING";
    view.actionColor = kAccent;
    view.actionEnabled = false;
    recorder_shell::apply(view);
    return;
  }

  if (state->phase == canon_ble::CanonBleState::Phase::PoweringOff) {
    view.status = "POWERING OFF...";
    view.detail = "WAITING FOR CAMERA";
    view.actionLabel = "WAIT";
    view.actionColor = kAccent;
    recorder_shell::apply(view);
    return;
  }

  if (state->phase == canon_ble::CanonBleState::Phase::AwaitingConfirmation ||
      state->phase == canon_ble::CanonBleState::Phase::Handshaking ||
      state->phase == canon_ble::CanonBleState::Phase::PostPairSetup) {
    view.status = "PAIRING...";
    view.detail = "SELECT OK ON CAMERA";
    view.actionLabel = "WAIT";
    view.actionColor = kAccent;
    recorder_shell::apply(view);
    return;
  }

  if (state->phase != canon_ble::CanonBleState::Phase::Ready) {
    view.status = "OPENING...";
    view.detail = "WAITING FOR SHOOTING MODE";
    view.actionLabel = "WAIT";
    view.actionColor = kAccent;
    recorder_shell::apply(view);
    return;
  }

  using Recording = canon_ble::CanonBleState::Recording;
  const bool recording =
      state->recordingConfirmed && state->recording == Recording::Recording;
  view.powerEnabled = !state->commandPending && !recording;
  if (state->recording == Recording::Starting) {
    view.status = "STARTING...";
    view.detail = "WAITING FOR CAMERA";
    view.actionLabel = "WAIT";
    view.actionColor = kAccent;
  } else if (state->recording == Recording::Stopping) {
    view.status = "STOPPING...";
    view.detail = "WAITING FOR CAMERA";
    view.actionLabel = "WAIT";
    view.actionColor = kAccent;
  } else if (state->recordingConfirmed &&
             state->recording == Recording::Recording) {
    view.status = state->lastCommandFailed ? "STOP FAILED" : "RECORDING";
    view.detail = "CAMERA CONFIRMED";
    view.actionLabel = "STOP";
    view.actionColor = kAccent;
    view.actionEnabled = true;
  } else if (state->recordingConfirmed &&
             state->recording == Recording::Stopped) {
    view.status = state->powerOffFailed
                      ? "POWER OFF FAILED"
                      : (state->lastCommandFailed ? "START FAILED" : "READY");
    view.detail = state->powerOffFailed ? "CAMERA STILL CONNECTED"
                                        : "CAMERA CONFIRMED";
    view.actionLabel = "START";
    view.actionColor = kReady;
    view.actionEnabled = true;
  } else {
    view.status = state->powerOffFailed
                      ? "POWER OFF FAILED"
                      : (state->lastCommandFailed ? "NO CONFIRMATION"
                                                  : "CONNECTED");
    view.detail = state->powerOffFailed ? "CAMERA STILL CONNECTED"
                                        : "STATE UNKNOWN";
    view.showUnknownControls = true;
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

}  // namespace canon_ble_ui
