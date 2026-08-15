#include "devices/gopro/ui.h"

#include "core/device_manager.h"
#include "devices/gopro/state.h"
#include "ui/recorder_screen_controller.h"
#include "ui/recorder_shell.h"

namespace gopro_ui {
namespace {
constexpr uint32_t kAccent = 0xE53935;
constexpr uint32_t kReady = 0x2E7D5B;
constexpr auto kOwner = recorder_shell::Owner::GoPro;
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
  const auto runtime = studio::devices().runtimeState(instanceId);
  const auto* state = static_cast<const gopro::GoProState*>(
      studio::devices().specializedState(instanceId));
  if (runtime.link != studio::LinkState::Connected || state == nullptr ||
      state->commandPending) return;
  enqueue(state->recording == gopro::GoProState::Recording::Recording
              ? studio::CommandType::RecordStop
              : studio::CommandType::RecordStart);
}

void onBack() { controller.back(); }
void onAction() { performPrimaryAction(); }

void onPower() {
  const studio::InstanceId instanceId = controller.instanceId();
  const auto* state = static_cast<const gopro::GoProState*>(
      studio::devices().specializedState(instanceId));
  enqueue(state != nullptr &&
                  (state->power == gopro::GoProState::Power::Asleep ||
                   state->power == gopro::GoProState::Power::SleepFailed)
              ? studio::CommandType::CameraPowerOn
              : studio::CommandType::CameraPowerOff);
}

void refresh() {
  if (!recorder_shell::ownedBy(kOwner)) return;
  recorder_shell::View view;
  const studio::InstanceId instanceId = controller.instanceId();
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
  } else if (state != nullptr &&
             state->power == gopro::GoProState::Power::Asleep) {
    view.status = "CAMERA ASLEEP";
    view.detail = "PRESS POWER TO WAKE";
    view.actionLabel = "WAITING";
    view.actionColor = kAccent;
    view.powerEnabled = true;
  } else if (state != nullptr &&
             state->power == gopro::GoProState::Power::SleepFailed) {
    view.status = "SLEEP FAILED";
    view.detail = "PRESS POWER TO RECONNECT";
    view.actionLabel = "WAITING";
    view.actionColor = kAccent;
    view.powerEnabled = true;
  } else if (state != nullptr &&
             state->power == gopro::GoProState::Power::Waking) {
    view.status = "WAKING...";
    view.detail = "CONNECTING TO GOPRO";
    view.actionLabel = "WAIT";
    view.actionColor = kAccent;
  } else if (runtime.link != studio::LinkState::Connected || state == nullptr) {
    view.status = runtime.link == studio::LinkState::Scanning
                      ? "PAIRING..."
                      : runtime.link == studio::LinkState::Connecting
                            ? "CONNECTING..." : "DISCONNECTED";
    view.detail = "OPEN GOPRO BLE";
    view.actionLabel = "WAITING";
    view.actionColor = kAccent;
  } else if (state->power == gopro::GoProState::Power::Sleeping) {
    view.status = "GOING TO SLEEP...";
    view.detail = "WAITING FOR GOPRO";
    view.actionLabel = "WAIT";
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
  if (state != nullptr && state->power == gopro::GoProState::Power::Awake &&
      !state->commandPending && !state->powerCommandPending &&
      !(state->recordingConfirmed &&
        state->recording == gopro::GoProState::Recording::Recording)) {
    view.powerEnabled = true;
  }
  if (state != nullptr && state->powerOffFailed) {
    view.status = "SLEEP FAILED";
    view.detail = state->power == gopro::GoProState::Power::Awake
                      ? "GOPRO STILL AWAKE"
                      : "PRESS POWER TO RECONNECT";
  }
  recorder_shell::apply(view);
}
}  // namespace

void show(studio::InstanceId id) {
  recorder_shell::Options options;
  options.enablePower = true;
  recorder_shell::Callbacks callbacks;
  callbacks.onBack = onBack;
  callbacks.onAction = onAction;
  callbacks.onPower = onPower;
  controller.show(id, options, callbacks, refresh);
}
void hide() { controller.hide(); }
void release() { controller.release(); }
bool active() { return controller.active(); }
void tick() { controller.tick(); }
void handleShortPress() { performPrimaryAction(); }
void handleLongPress() { onBack(); }
}  // namespace gopro_ui
