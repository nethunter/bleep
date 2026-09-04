#include "devices/canon_ble/ui.h"

#include "core/device_manager.h"
#include "devices/canon_ble/state.h"
#include "ui/recorder_screen_controller.h"
#include "ui/recorder_shell.h"

namespace canon_ble_ui {

namespace {

constexpr uint32_t kAccent = 0xE53935;
constexpr uint32_t kReady = 0x2E7D5B;
constexpr auto kOwner = recorder_shell::Owner::CanonBle;

studio_ui::RecorderScreenController controller(kOwner);

void enqueue(studio::CommandType type) {
  controller.enqueue(type);
}

void performPrimaryAction() {
  const studio::InstanceId instanceId = controller.instanceId();
  if (studio::devices().pendingAddCommitFailed(instanceId)) {
    studio::devices().retryPendingAdd(instanceId);
    return;
  }
  const auto* current = static_cast<const canon_ble::CanonBleState*>(
      studio::devices().specializedState(instanceId));
  if (current != nullptr &&
      (current->pairingRejected || current->protocolFailed)) {
    enqueue(studio::CommandType::Connect);
    return;
  }
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
  controller.back();
}

void onAction() { performPrimaryAction(); }

void onUnknownStart() { enqueue(studio::CommandType::RecordStart); }

void onUnknownStop() { enqueue(studio::CommandType::RecordStop); }

void onPower() {
  const studio::InstanceId instanceId = controller.instanceId();
  const auto* state = static_cast<const canon_ble::CanonBleState*>(
      studio::devices().specializedState(instanceId));
  enqueue(state != nullptr &&
                  state->phase == canon_ble::CanonBleState::Phase::PoweredOff
              ? studio::CommandType::CameraPowerOn
              : studio::CommandType::CameraPowerOff);
}

void refresh() {
  if (!recorder_shell::ownedBy(kOwner)) {
    return;
  }

  recorder_shell::View view;
  const studio::InstanceId instanceId = controller.instanceId();
  const studio::DeviceRecord* record = studio::devices().find(instanceId);
  view.title = record != nullptr ? record->displayName : "";

  const studio::DeviceRuntimeState runtime =
      studio::devices().runtimeState(instanceId);
  const auto* state = static_cast<const canon_ble::CanonBleState*>(
      studio::devices().specializedState(instanceId));

  if (studio::devices().pendingAddCommitFailed(instanceId)) {
    view.status = "COULDN'T SAVE";
    view.detail = "RETRY TO ADD DEVICE";
    view.actionLabel = "RETRY";
    view.actionColor = kReady;
    view.actionEnabled = true;
    recorder_shell::apply(view);
    return;
  }

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
    } else if (state != nullptr && state->retryPending) {
      // A sleeping body answers the first pokes with 0x3E and stays silent
      // for several seconds; keep showing progress instead of DISCONNECTED.
      view.status = "WAKING CAMERA...";
      view.detail = "RETRYING LINK";
    } else if (state != nullptr && state->pairingRejected) {
      view.status = "PAIRING REJECTED";
      view.detail = "TRY AGAIN";
      view.actionLabel = "RETRY";
      view.actionColor = kReady;
      view.actionEnabled = true;
    } else if (state != nullptr && state->protocolFailed) {
      view.status = "CONNECTION FAILED";
      view.detail = "CANON SETUP INCOMPLETE";
      view.actionLabel = "RETRY";
      view.actionColor = kReady;
      view.actionEnabled = true;
    } else {
      view.actionLabel = "WAITING";
      view.actionColor = kAccent;
      view.actionEnabled = false;
    }
    view.powerEnabled =
        state != nullptr &&
        state->phase == canon_ble::CanonBleState::Phase::PoweredOff;
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

void show(studio::InstanceId id) {
  recorder_shell::Options options;
  options.enablePower = true;
  options.enableUnknownControls = true;
  recorder_shell::Callbacks callbacks;
  callbacks.onBack = onBack;
  callbacks.onAction = onAction;
  callbacks.onUnknownStart = onUnknownStart;
  callbacks.onUnknownStop = onUnknownStop;
  callbacks.onPower = onPower;
  controller.show(id, options, callbacks, refresh);
}

void hide() { controller.hide(); }

void release() { controller.release(); }

bool active() { return controller.active(); }

void tick() { controller.tick(); }

void handleShortPress() { performPrimaryAction(); }

void handleLongPress() { onBack(); }

}  // namespace canon_ble_ui
