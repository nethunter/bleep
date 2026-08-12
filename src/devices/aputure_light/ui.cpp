#include "devices/aputure_light/ui.h"

#include <Arduino.h>
#include <lvgl.h>

#include <cstring>

#include "../../ui.h"
#include "core/ble/onboarding_auto_select.h"
#include "core/device_manager.h"
#include "devices/aputure_light/runtime.h"
#include "devices/aputure_light/state.h"
#include "fonts/ui_fonts.h"
#include "haptic_feedback.h"
#include "ui/ble_pairing_screen.h"
#include "ui/light_control.h"

namespace aputure_light_ui {
namespace {

constexpr uint32_t kPanel = 0x12161d;
constexpr uint32_t kText = 0xf3f4f6;

studio::InstanceId instanceId = studio::kInvalidInstanceId;
bool visible = false;
uint32_t lastRefreshMs = 0;
studio_ui::BlePairingScreen pairingScreen;
studio::ble::OnboardingAutoSelect autoSelect;
lv_obj_t* identifyAce = nullptr;
lv_obj_t* identifyPano60 = nullptr;
lv_obj_t* identifyPano120 = nullptr;
lv_obj_t* identifyMcPro = nullptr;

lv_obj_t* identifyButton(const char* text, lv_event_cb_t callback) {
  lv_obj_t* button = lv_btn_create(pairingScreen.screen());
  lv_obj_set_style_bg_color(button, lv_color_hex(kPanel), 0);
  lv_obj_set_style_radius(button, 8, 0);
  lv_obj_set_style_shadow_width(button, 0, 0);
  lv_obj_t* label = lv_label_create(button);
  lv_label_set_text(label, text);
  lv_obj_set_style_text_font(label, UI_FONT_14, 0);
  lv_obj_set_style_text_color(label, lv_color_hex(kText), 0);
  lv_obj_center(label);
  lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, nullptr);
  return button;
}

const char* phaseText(const aputure_light::AputureLightState* state) {
  if (state == nullptr) return "Unavailable";
  switch (state->phase) {
    case aputure_light::AputureLightState::Phase::Unprovisioned:
      return "Not provisioned";
    case aputure_light::AputureLightState::Phase::Scanning:
      return "Scanning for light";
    case aputure_light::AputureLightState::Phase::ConnectingProvisioning:
      return "Connecting to light";
    case aputure_light::AputureLightState::Phase::Provisioning:
      return "Provisioning";
    case aputure_light::AputureLightState::Phase::PendingConfig:
      return "Configuring mesh";
    case aputure_light::AputureLightState::Phase::ConnectingProxy:
      return "Connecting proxy";
    case aputure_light::AputureLightState::Phase::Ready:
      return state->powerConfirmed && state->nodeReachable
                 ? "Ready / confirmed"
                 : (state->optimistic ? "Ready / optimistic"
                                      : "Ready / state unknown");
    case aputure_light::AputureLightState::Phase::Failed:
      return state->error[0] != '\0' ? state->error : "Failed";
  }
  return "Unknown";
}

bool needsVendorIdentity() {
  const auto* state = static_cast<const aputure_light::AputureLightState*>(
      studio::devices().specializedState(instanceId));
  const aputure_light::AputureLightRuntime* runtime =
      aputure_light::runtimeIfActive();
  return state != nullptr &&
         state->phase == aputure_light::AputureLightState::Phase::Failed &&
         runtime != nullptr && runtime->canIdentifyVendorModel(instanceId);
}

void identify(uint16_t companyId, uint16_t modelId, const char* productName) {
  if (aputure_light::AputureLightRuntime* runtime =
          aputure_light::runtimeIfActive()) {
    runtime->identifyVendorModel(instanceId, companyId, modelId, productName);
  }
}

void onIdentifyAce(lv_event_t*) {
  identify(0x0211, 0x0000, "amaran Ace 25c");
}
void onIdentifyPano60(lv_event_t*) {
  identify(0x0211, 0x0000, "amaran Pano 60c");
}
void onIdentifyPano120(lv_event_t*) {
  identify(0x0211, 0x0000, "amaran Pano 120c");
}
void onIdentifyMcPro(lv_event_t*) {
  identify(0x03f6, 0x1000, "Aputure MC Pro");
}

void showIdentificationButtons(bool show) {
  if (show && identifyAce == nullptr) {
    identifyAce = identifyButton("Ace 25c", onIdentifyAce);
    lv_obj_set_size(identifyAce, 82, 30);
    lv_obj_align(identifyAce, LV_ALIGN_BOTTOM_MID, -44, -50);
    identifyPano60 = identifyButton("Pano 60c", onIdentifyPano60);
    lv_obj_set_size(identifyPano60, 82, 30);
    lv_obj_align(identifyPano60, LV_ALIGN_BOTTOM_MID, 44, -50);
    identifyPano120 = identifyButton("Pano 120c", onIdentifyPano120);
    lv_obj_set_size(identifyPano120, 82, 30);
    lv_obj_align(identifyPano120, LV_ALIGN_BOTTOM_MID, -44, -14);
    identifyMcPro = identifyButton("MC Pro", onIdentifyMcPro);
    lv_obj_set_size(identifyMcPro, 82, 30);
    lv_obj_align(identifyMcPro, LV_ALIGN_BOTTOM_MID, 44, -14);
  }
  lv_obj_t* buttons[] = {identifyAce, identifyPano60, identifyPano120,
                         identifyMcPro};
  for (lv_obj_t* button : buttons) {
    if (button == nullptr) continue;
    if (show)
      lv_obj_clear_flag(button, LV_OBJ_FLAG_HIDDEN);
    else
      lv_obj_add_flag(button, LV_OBJ_FLAG_HIDDEN);
  }
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

void onBack(lv_event_t*) {
  haptic_feedback::request(haptic_feedback::Pattern::Back);
  if (studio::devices().isPendingAdd(instanceId) &&
      studio::devices().cancelPendingAdd(instanceId) !=
          studio::RegistryStatus::Ok) {
    return;
  }
  hide();
  ui::showDeviceParent();
}

void onSharedBack() { onBack(nullptr); }

void enqueueConnect() {
  studio::DeviceCommand command;
  command.instanceId = instanceId;
  command.type = studio::CommandType::Connect;
  studio::devices().enqueue(command);
}

void onRetry(lv_event_t*) {
  if (studio::devices().pendingAddCommitFailed(instanceId))
    studio::devices().retryPendingAdd(instanceId);
  else
    enqueueConnect();
}

void showForState(const aputure_light::AputureLightState* state) {
  if (studio::devices().pendingAddCommitFailed(instanceId)) {
    pairingScreen.create(onBack, onRetry);
    showIdentificationButtons(false);
    const auto* record = studio::devices().find(instanceId);
    pairingScreen.setTitle(record != nullptr ? record->displayName
                                             : "Aputure Light");
    pairingScreen.setStatus("Couldn't save", "Retry to add this device", false,
                            true, "Retry");
    if (lv_scr_act() != pairingScreen.screen())
      lv_scr_load(pairingScreen.screen());
    return;
  }
  if (state != nullptr &&
      state->phase == aputure_light::AputureLightState::Phase::Ready) {
    if (!light_control_ui::active())
      light_control_ui::show(instanceId, onSharedBack, true);
    return;
  }

  light_control_ui::hide();
  pairingScreen.create(onBack, onRetry);
  const auto* record = studio::devices().find(instanceId);
  pairingScreen.setTitle(record != nullptr ? record->displayName
                                           : "Aputure Light");
  const bool failed = state != nullptr &&
                      state->phase ==
                          aputure_light::AputureLightState::Phase::Failed;
  const bool scanning = state == nullptr ||
                        state->phase ==
                            aputure_light::AputureLightState::Phase::Unprovisioned ||
                        state->phase ==
                            aputure_light::AputureLightState::Phase::Scanning;
  const bool unknownVendor = needsVendorIdentity();
  showIdentificationButtons(unknownVendor);
  const char* detail = scanning ? "Factory-reset light nearby"
                       : unknownVendor ? ""
                       : failed ? "Check light and try again"
                                : "Keep the light powered on";
  pairingScreen.setStatus(unknownVendor ? "Identify fixture" : phaseText(state),
                          detail, !failed,
                          unknownVendor ? false : (scanning || failed),
                          "Retry");
  updateCandidates(scanning && !unknownVendor);
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
  showForState(static_cast<const aputure_light::AputureLightState*>(
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
  identifyAce = identifyPano60 = identifyPano120 = identifyMcPro = nullptr;
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
  showForState(static_cast<const aputure_light::AputureLightState*>(
      studio::devices().specializedState(instanceId)));
}

void handleShortPress() {
  if (light_control_ui::active()) light_control_ui::handleShortPress();
}

void handleLongPress() {
  if (light_control_ui::active())
    light_control_ui::handleLongPress();
  else
    onBack(nullptr);
}

#ifdef UI_SIMULATOR
void simSetCctLook(int kelvin, int tintPermille, int brightness) {
  light_control_ui::simSetCctLook(kelvin, tintPermille, brightness);
}
void simSetRgbLook(uint32_t rgb, int brightness) {
  light_control_ui::simSetRgbLook(rgb, brightness);
}
void simShowCct() { light_control_ui::simShowCct(); }
void simShowRgb() { light_control_ui::simShowRgb(); }
int simRgbSaturation() { return light_control_ui::simRgbSaturation(); }
size_t simCandidateRowCount() {
  return pairingScreen.simCandidateRowCount();
}
lv_obj_t* simCandidateRow(size_t index) {
  return pairingScreen.simCandidateRow(index);
}
void simScrollCandidates(int16_t delta) {
  pairingScreen.simScrollCandidates(delta);
}
int32_t simCandidateScrollY() {
  return pairingScreen.simCandidateScrollY();
}
void simClickCandidate(size_t index) {
  pairingScreen.simClickCandidate(index);
}
#endif

}  // namespace aputure_light_ui
