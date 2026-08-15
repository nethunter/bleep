#include "devices/phone_camera/ui.h"

#include "core/device_manager.h"
#include "devices/phone_camera/state.h"
#include "ui/recorder_screen_controller.h"
#include "ui/recorder_shell.h"

namespace phone_camera_ui {
namespace {
constexpr auto kOwner = recorder_shell::Owner::PhoneCamera;
constexpr uint32_t kAccent = 0xE53935;
studio_ui::RecorderScreenController controller(kOwner);

void trigger() {
  const studio::InstanceId instanceId = controller.instanceId();
  const auto runtime = studio::devices().runtimeState(instanceId);
  if (runtime.link != studio::LinkState::Connected || runtime.commandPending) return;
  controller.enqueue(studio::CommandType::RecordTrigger);
}
void onBack() { controller.back(); }
void refresh() {
  if (!recorder_shell::ownedBy(kOwner)) return;
  recorder_shell::View view;
  const studio::InstanceId instanceId = controller.instanceId();
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
  recorder_shell::Options options;
  recorder_shell::Callbacks callbacks;
  callbacks.onBack = onBack;
  callbacks.onAction = trigger;
  controller.show(id, options, callbacks, refresh);
}
void hide() { controller.hide(); }
void release() { controller.release(); }
bool active() { return controller.active(); }
void tick() { controller.tick(); }
void handleShortPress() { trigger(); }
void handleLongPress() { onBack(); }
}  // namespace phone_camera_ui
