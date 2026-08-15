#include "devices/zhiyun_light/ui.h"

#include <Arduino.h>
#include <lvgl.h>

#include "../../ui.h"
#include "core/ble/onboarding_auto_select.h"
#include "core/device_manager.h"
#include "devices/zhiyun_light/state.h"
#include "haptic_feedback.h"
#include "ui/ble_pairing_screen.h"
#include "ui/light_control.h"

namespace zhiyun_light_ui {
namespace {

studio::InstanceId instanceId = studio::kInvalidInstanceId;
bool visible = false;
uint32_t lastRefreshMs = 0;
studio_ui::BlePairingScreen pairingScreen;
studio::ble::OnboardingAutoSelect autoSelect;

void enqueueConnect() {
  studio::DeviceCommand command;
  command.instanceId = instanceId;
  command.type = studio::CommandType::Connect;
  studio::devices().enqueue(command);
}

void onBack() {
  haptic_feedback::request(haptic_feedback::Pattern::Back);
  if (studio::devices().isPendingAdd(instanceId) &&
      studio::devices().cancelPendingAdd(instanceId) !=
          studio::RegistryStatus::Ok) {
    return;
  }
  hide();
  ui::showDeviceParent();
}

void onSharedBack() { onBack(); }
void onBackEvent(lv_event_t*) { onBack(); }

void onRetryEvent(lv_event_t*) {
  if (studio::devices().pendingAddCommitFailed(instanceId))
    studio::devices().retryPendingAdd(instanceId);
  else
    enqueueConnect();
}

void onCandidate(uint32_t token) {
  studio::devices().selectOnboardingCandidate(instanceId, token);
}

void updateCandidates(bool show) {
  studio::OnboardingCandidate candidates[4];
  size_t count = 0;
  if (show) {
    const size_t available =
        studio::devices().onboardingCandidateCount(instanceId);
    while (count < available && count < 4 &&
           studio::devices().onboardingCandidate(instanceId, count,
                                                 candidates[count])) {
      ++count;
    }
  }
  const auto decision = autoSelect.update(
      count, count == 1 ? candidates[0].token : 0, millis());
  if (decision == studio::ble::OnboardingAutoSelect::Decision::Select) {
    if (!studio::devices().selectOnboardingCandidate(instanceId,
                                                      candidates[0].token)) {
      autoSelect.selectionFailed(candidates[0].token);
      pairingScreen.setCandidates(candidates, count, onCandidate);
      return;
    }
    pairingScreen.setCandidates(nullptr, 0, onCandidate);
    return;
  }
  if (decision == studio::ble::OnboardingAutoSelect::Decision::Wait) {
    pairingScreen.setCandidates(nullptr, 0, onCandidate);
    return;
  }
  pairingScreen.setCandidates(candidates, count, onCandidate);
}

const char* phaseText(const zhiyun_light::ZhiyunLightState* state) {
  if (state == nullptr) return "Unavailable";
  if (state->phase == zhiyun_light::ZhiyunLightState::Phase::Failed)
    return state->error[0] != '\0' ? state->error : "Connection failed";
  if (state->link == zhiyun_light::ZhiyunLightState::Link::Scanning)
    return "Scanning for Zhiyun";
  if (state->link == zhiyun_light::ZhiyunLightState::Link::Connecting)
    return "Connecting";
  if (state->phase == zhiyun_light::ZhiyunLightState::Phase::Provisioning)
    return "Creating mesh";
  if (state->phase == zhiyun_light::ZhiyunLightState::Phase::Initializing)
    return "Initializing control";
  if (state->phase == zhiyun_light::ZhiyunLightState::Phase::ReadingState)
    return "Reading light state";
  return "Waiting for light";
}

void showForState(const zhiyun_light::ZhiyunLightState* state) {
  if (studio::devices().pendingAddCommitFailed(instanceId)) {
    pairingScreen.create(onBackEvent, onRetryEvent);
    pairingScreen.setTitle("Zhiyun Light");
    pairingScreen.setStatus("Couldn't save", "Retry to add this device", false,
                            true, "Retry");
    if (lv_scr_act() != pairingScreen.screen())
      lv_scr_load(pairingScreen.screen());
    return;
  }
  if (state != nullptr &&
      state->phase == zhiyun_light::ZhiyunLightState::Phase::Ready) {
    if (!light_control_ui::active())
      light_control_ui::show(instanceId, onSharedBack);
    return;
  }

  light_control_ui::hide();
  pairingScreen.create(onBackEvent, onRetryEvent);
  pairingScreen.setTitle("Zhiyun Light");
  const bool failed = state != nullptr &&
                      state->phase == zhiyun_light::ZhiyunLightState::Phase::Failed;
  pairingScreen.setStatus(phaseText(state),
                          failed ? "Check the light and retry"
                                 : "Reset or provisioned light nearby",
                          !failed, failed, "Retry");
  const bool scanning = state == nullptr ||
                        state->link == zhiyun_light::ZhiyunLightState::Link::Scanning;
  updateCandidates(scanning && !failed);
  if (lv_scr_act() != pairingScreen.screen())
    lv_scr_load(pairingScreen.screen());
}

}  // namespace

void show(studio::InstanceId id) {
  autoSelect.reset();
  instanceId = id;
  visible =
      studio::devices().acquire(id, studio::ConnectionOwner::Foreground);
  if (!visible) {
    instanceId = studio::kInvalidInstanceId;
    return;
  }
  lastRefreshMs = 0;
  showForState(static_cast<const zhiyun_light::ZhiyunLightState*>(
      studio::devices().specializedState(id)));
  ui::releaseInactiveScreens();
}

void hide() {
  autoSelect.reset();
  light_control_ui::hide();
  if (visible)
    studio::devices().release(instanceId,
                              studio::ConnectionOwner::Foreground);
  visible = false;
  instanceId = studio::kInvalidInstanceId;
}

void release() {
  if (visible) return;
  light_control_ui::release();
  pairingScreen.destroy();
}

bool active() { return visible; }

void tick() {
  if (!visible) return;
  if (light_control_ui::active()) {
    light_control_ui::tick();
    return;
  }
  const uint32_t now = millis();
  if (now - lastRefreshMs < 250) return;
  lastRefreshMs = now;
  showForState(static_cast<const zhiyun_light::ZhiyunLightState*>(
      studio::devices().specializedState(instanceId)));
}

void handleShortPress() {
  if (light_control_ui::active()) light_control_ui::handleShortPress();
}

void handleLongPress() {
  if (light_control_ui::active())
    light_control_ui::handleLongPress();
  else
    onBack();
}

#ifdef UI_SIMULATOR
void simShowRgb() { light_control_ui::simShowRgb(); }
#endif

}  // namespace zhiyun_light_ui
