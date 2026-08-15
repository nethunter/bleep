#include "ui/recorder_screen_controller.h"

#include <Arduino.h>

#include "core/device_manager.h"
#include "ui.h"

namespace studio_ui {

bool RecorderScreenController::show(
    studio::InstanceId id, const recorder_shell::Options& options,
    const recorder_shell::Callbacks& callbacks, Refresh refresh) {
  if (!recorder_shell::ownedBy(owner_)) {
    if (recorder_shell::screen() != nullptr &&
        lv_scr_act() == recorder_shell::screen()) {
      ui::parkForScreenRebuild();
    }
    recorder_shell::destroyIdle();
    recorder_shell::acquire(owner_, options, callbacks);
  }

  instanceId_ = id;
  refresh_ = refresh;
  visible_ = studio::devices().acquire(
      instanceId_, studio::ConnectionOwner::Foreground);
  if (!visible_) {
    instanceId_ = studio::kInvalidInstanceId;
    refresh_ = nullptr;
    return false;
  }

  lastRefreshMs_ = 0;
  if (refresh_ != nullptr) {
    refresh_();
  }
  lv_scr_load(recorder_shell::screen());
  ui::releaseInactiveScreens();
  return true;
}

void RecorderScreenController::hide() {
  if (visible_) {
    studio::devices().release(instanceId_,
                              studio::ConnectionOwner::Foreground);
  }
  visible_ = false;
  instanceId_ = studio::kInvalidInstanceId;
  refresh_ = nullptr;
}

void RecorderScreenController::release() {
  if (!visible_) {
    recorder_shell::release(owner_);
  }
}

void RecorderScreenController::tick() {
  if (!visible_ || refresh_ == nullptr) {
    return;
  }
  const uint32_t now = millis();
  if (now - lastRefreshMs_ >= 200) {
    lastRefreshMs_ = now;
    refresh_();
  }
}

void RecorderScreenController::back() {
  hide();
  ui::showDeviceParent();
}

bool RecorderScreenController::enqueue(studio::CommandType type) const {
  if (instanceId_ == studio::kInvalidInstanceId) {
    return false;
  }
  studio::DeviceCommand command;
  command.instanceId = instanceId_;
  command.type = type;
  return studio::devices().enqueue(command);
}

}  // namespace studio_ui
