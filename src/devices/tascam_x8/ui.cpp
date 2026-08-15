#include "devices/tascam_x8/ui.h"

#include "core/device_manager.h"
#include "devices/tascam_x8/state.h"
#include "ui/recorder_screen_controller.h"
#include "ui/recorder_shell.h"

namespace tascam_x8_ui {

namespace {

constexpr uint32_t kAccent = 0xE53935;
constexpr uint32_t kReady = 0x2E7D5B;
constexpr auto kOwner = recorder_shell::Owner::TascamX8;

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
  controller.back();
}

void onAction() { performPrimaryAction(); }

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
  const auto* state = static_cast<const tascam_x8::TascamX8State*>(
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

void show(studio::InstanceId id) {
  recorder_shell::Options options;
  recorder_shell::Callbacks callbacks;
  callbacks.onBack = onBack;
  callbacks.onAction = onAction;
  controller.show(id, options, callbacks, refresh);
}

void hide() { controller.hide(); }

void release() { controller.release(); }

bool active() { return controller.active(); }

void tick() { controller.tick(); }

void handleShortPress() { performPrimaryAction(); }

void handleLongPress() { onBack(); }

}  // namespace tascam_x8_ui
