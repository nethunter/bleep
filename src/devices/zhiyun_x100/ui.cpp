#include "devices/zhiyun_x100/ui.h"

#include <Arduino.h>
#include <lvgl.h>

#include <cstdio>
#include "core/device_manager.h"
#include "devices/zhiyun_x100/protocol.h"
#include "devices/zhiyun_x100/state.h"
#include "fonts/ui_fonts.h"
#include "haptic_feedback.h"
#include "ui/ble_pairing_screen.h"
#include "ui/round_page.h"
#include "../../ui.h"

namespace zhiyun_x100_ui {
namespace {

constexpr uint32_t kBackground = 0x05070a;
constexpr uint32_t kPanel = 0x12161d;
constexpr uint32_t kAccent = 0x35c7f2;
constexpr uint32_t kText = 0xf3f4f6;
constexpr uint32_t kMuted = 0x8a94a6;
constexpr uint32_t kDanger = 0xf26d6d;

studio::InstanceId instanceId = studio::kInvalidInstanceId;
bool visible = false;
bool rgbMode = false;
bool draftInitialized = false;
bool syncing = false;
bool editPending = false;
bool editDispatched = false;
bool editCommandObserved = false;
uint32_t editDueMs = 0;
uint32_t lastRefreshMs = 0;
uint16_t draftKelvin = 5600;
uint8_t draftCctBrightness = 50;
uint32_t draftRgb = 0xffffff;
uint8_t draftRgbSaturation = 0;
uint8_t draftRgbBrightness = 50;

lv_obj_t* screen = nullptr;
lv_obj_t* title = nullptr;
lv_obj_t* status = nullptr;
lv_obj_t* cctBody = nullptr;
lv_obj_t* rgbBody = nullptr;
lv_obj_t* cctSlider = nullptr;
lv_obj_t* cctBrightness = nullptr;
lv_obj_t* wheel = nullptr;
lv_obj_t* rgbSaturation = nullptr;
lv_obj_t* rgbBrightness = nullptr;
lv_obj_t* modeCct = nullptr;
lv_obj_t* modeRgb = nullptr;
lv_obj_t* powerButton = nullptr;
lv_obj_t* powerLabel = nullptr;
studio_ui::BlePairingScreen pairingScreen;

void enqueue(studio::CommandType type, int value0 = 0, int value1 = 0) {
  studio::DeviceCommand command;
  command.instanceId = instanceId;
  command.type = type;
  command.value0 = value0;
  command.value1 = value1;
  studio::devices().enqueue(command);
}

lv_obj_t* makeButton(lv_obj_t* parent, const char* text,
                     lv_event_cb_t callback, uint32_t color = kPanel) {
  lv_obj_t* button = lv_btn_create(parent);
  lv_obj_set_style_bg_color(button, lv_color_hex(color), 0);
  lv_obj_set_style_radius(button, 8, 0);
  lv_obj_set_style_shadow_width(button, 0, 0);
  lv_obj_t* label = lv_label_create(button);
  lv_label_set_text(label, text);
  lv_obj_set_style_text_font(label, UI_FONT_14, 0);
  lv_obj_center(label);
  if (callback != nullptr)
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, nullptr);
  return button;
}

lv_obj_t* labeledSlider(lv_obj_t* parent, const char* text, int minimum,
                        int maximum, lv_obj_t*& slider,
                        lv_event_cb_t callback) {
  lv_obj_t* row = lv_obj_create(parent);
  lv_obj_set_size(row, 166, 34);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(row, 0, 0);
  lv_obj_set_style_pad_all(row, 0, 0);
  lv_obj_t* label = lv_label_create(row);
  lv_label_set_text(label, text);
  lv_obj_set_style_text_font(label, UI_FONT_14, 0);
  lv_obj_align(label, LV_ALIGN_LEFT_MID, 0, 0);
  slider = lv_slider_create(row);
  lv_obj_set_size(slider, 112, 10);
  lv_obj_align(slider, LV_ALIGN_RIGHT_MID, 0, 0);
  lv_slider_set_range(slider, minimum, maximum);
  lv_obj_add_event_cb(slider, callback, LV_EVENT_VALUE_CHANGED, nullptr);
  return row;
}

void captureDraft() {
  if (syncing) return;
  if (rgbMode) {
    const lv_color_hsv_t hsv = lv_colorwheel_get_hsv(wheel);
    draftRgbSaturation = lv_slider_get_value(rgbSaturation);
    const lv_color_t color =
        lv_color_hsv_to_rgb(hsv.h, draftRgbSaturation, 100);
    draftRgb = lv_color_to32(color) & 0xffffff;
    draftRgbBrightness = lv_slider_get_value(rgbBrightness);
  } else {
    draftKelvin = zhiyun_x100::normalizeCct(
        static_cast<uint16_t>(lv_slider_get_value(cctSlider)));
    draftCctBrightness = lv_slider_get_value(cctBrightness);
  }
}

void restoreDraft() {
  syncing = true;
  if (rgbMode) {
    const lv_color_hsv_t hsv = lv_color_rgb_to_hsv(
        static_cast<uint8_t>(draftRgb >> 16),
        static_cast<uint8_t>(draftRgb >> 8), static_cast<uint8_t>(draftRgb));
    lv_colorwheel_set_hsv(wheel, lv_color_hsv_t{hsv.h, 100, 100});
    lv_slider_set_value(rgbSaturation, draftRgbSaturation, LV_ANIM_OFF);
    lv_slider_set_value(rgbBrightness, draftRgbBrightness, LV_ANIM_OFF);
  } else {
    lv_slider_set_value(cctSlider, draftKelvin, LV_ANIM_OFF);
    lv_slider_set_value(cctBrightness, draftCctBrightness, LV_ANIM_OFF);
  }
  syncing = false;
}

void renderMode() {
  lv_obj_set_style_bg_color(modeCct,
                            lv_color_hex(rgbMode ? kPanel : kAccent), 0);
  lv_obj_set_style_bg_color(modeRgb,
                            lv_color_hex(rgbMode ? kAccent : kPanel), 0);
  if (rgbMode) {
    lv_obj_add_flag(cctBody, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(rgbBody, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(rgbBody, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(cctBody, LV_OBJ_FLAG_HIDDEN);
  }
}

void markDirty(lv_event_t*) {
  if (syncing || instanceId == studio::kInvalidInstanceId) return;
  captureDraft();
  editPending = true;
  editDueMs = millis() + 350;
}

void setMode(bool rgb) {
  if (rgb == rgbMode) return;
  captureDraft();
  rgbMode = rgb;
  renderMode();
  restoreDraft();
  editPending = true;
  editDueMs = millis() + 350;
}

void onCct(lv_event_t*) { setMode(false); }
void onRgb(lv_event_t*) { setMode(true); }

void onBack() {
  haptic_feedback::request(haptic_feedback::Pattern::Back);
  hide();
  ui::showDeviceParent();
}

void onBackEvent(lv_event_t*) { onBack(); }

void onRetryEvent(lv_event_t*) {
  if (studio::devices().pendingAddCommitFailed(instanceId))
    studio::devices().retryPendingAdd(instanceId);
  else
    enqueue(studio::CommandType::Connect);
}

void onPower(lv_event_t*) {
  const auto* state = static_cast<const zhiyun_x100::X100State*>(
      studio::devices().specializedState(instanceId));
  const studio::DeviceRuntimeState runtime =
      studio::devices().runtimeState(instanceId);
  if (state == nullptr || !runtime.protocolReady || runtime.commandPending)
    return;
  enqueue(state->on ? studio::CommandType::TurnOff
                    : studio::CommandType::TurnOn);
}

void ensure() {
  if (screen != nullptr) return;
  screen = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(screen, lv_color_hex(kBackground), 0);
  lv_obj_set_style_text_color(screen, lv_color_hex(kText), 0);
  lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

  studio_ui::RoundPageHeaderOptions headerOptions;
  headerOptions.onBack = onBackEvent;
  headerOptions.panelColor = kPanel;
  headerOptions.textColor = kText;
  title = studio_ui::createRoundPageHeader(screen, headerOptions).title;
  status = lv_label_create(screen);
  lv_obj_set_width(status, 170);
  lv_obj_set_style_text_align(status, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(status, UI_FONT_14, 0);
  lv_obj_align(status, LV_ALIGN_TOP_MID, 0, 51);

  modeCct = makeButton(screen, "CCT", onCct, kAccent);
  lv_obj_set_size(modeCct, 58, 27);
  lv_obj_align(modeCct, LV_ALIGN_TOP_MID, -33, 72);
  modeRgb = makeButton(screen, "RGB", onRgb);
  lv_obj_set_size(modeRgb, 58, 27);
  lv_obj_align(modeRgb, LV_ALIGN_TOP_MID, 33, 72);

  cctBody = lv_obj_create(screen);
  lv_obj_set_size(cctBody, 174, 82);
  lv_obj_align(cctBody, LV_ALIGN_TOP_MID, 0, 105);
  lv_obj_set_style_bg_opa(cctBody, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(cctBody, 0, 0);
  lv_obj_set_style_pad_all(cctBody, 2, 0);
  lv_obj_set_style_pad_row(cctBody, 2, 0);
  lv_obj_set_flex_flow(cctBody, LV_FLEX_FLOW_COLUMN);
  labeledSlider(cctBody, "K", zhiyun_x100::kMinKelvin,
                zhiyun_x100::kMaxKelvin, cctSlider, markDirty);
  labeledSlider(cctBody, "Bri", 0, 100, cctBrightness, markDirty);

  rgbBody = lv_obj_create(screen);
  lv_obj_set_size(rgbBody, 174, 82);
  lv_obj_align(rgbBody, LV_ALIGN_TOP_MID, 0, 105);
  lv_obj_set_style_bg_opa(rgbBody, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(rgbBody, 0, 0);
  lv_obj_set_style_pad_all(rgbBody, 0, 0);
  lv_obj_add_flag(rgbBody, LV_OBJ_FLAG_HIDDEN);
  wheel = lv_colorwheel_create(rgbBody, true);
  lv_obj_set_size(wheel, 72, 72);
  lv_obj_align(wheel, LV_ALIGN_LEFT_MID, 5, 0);
  lv_obj_add_event_cb(wheel, markDirty, LV_EVENT_VALUE_CHANGED, nullptr);
  lv_obj_t* satLabel = lv_label_create(rgbBody);
  lv_label_set_text(satLabel, "Sat");
  lv_obj_set_style_text_font(satLabel, UI_FONT_14, 0);
  lv_obj_align(satLabel, LV_ALIGN_TOP_RIGHT, -55, 6);
  rgbSaturation = lv_slider_create(rgbBody);
  lv_obj_set_size(rgbSaturation, 72, 8);
  lv_obj_align(rgbSaturation, LV_ALIGN_TOP_RIGHT, -3, 25);
  lv_slider_set_range(rgbSaturation, 0, 100);
  lv_obj_add_event_cb(rgbSaturation, markDirty, LV_EVENT_VALUE_CHANGED,
                      nullptr);
  lv_obj_t* brightnessLabel = lv_label_create(rgbBody);
  lv_label_set_text(brightnessLabel, "Bri");
  lv_obj_set_style_text_font(brightnessLabel, UI_FONT_14, 0);
  lv_obj_align(brightnessLabel, LV_ALIGN_TOP_RIGHT, -55, 43);
  rgbBrightness = lv_slider_create(rgbBody);
  lv_obj_set_size(rgbBrightness, 72, 8);
  lv_obj_align(rgbBrightness, LV_ALIGN_TOP_RIGHT, -3, 62);
  lv_slider_set_range(rgbBrightness, 0, 100);
  lv_obj_add_event_cb(rgbBrightness, markDirty, LV_EVENT_VALUE_CHANGED,
                      nullptr);

  powerButton = makeButton(screen, "POWER", onPower, kAccent);
  lv_obj_set_size(powerButton, 94, 28);
  lv_obj_align(powerButton, LV_ALIGN_BOTTOM_MID, 0, -16);
  powerLabel = lv_obj_get_child(powerButton, 0);
  lv_obj_move_background(cctBody);
  lv_obj_move_background(rgbBody);
}

const char* phaseText(const zhiyun_x100::X100State* state) {
  if (state == nullptr) return "Unavailable";
  if (state->phase == zhiyun_x100::X100State::Phase::Failed)
    return state->error[0] != '\0' ? state->error : "Connection failed";
  if (state->link == zhiyun_x100::X100State::Link::Scanning)
    return "Scanning for Zhiyun";
  if (state->link == zhiyun_x100::X100State::Link::Connecting)
    return "Connecting";
  if (state->phase == zhiyun_x100::X100State::Phase::Provisioning)
    return "Creating mesh";
  if (state->phase == zhiyun_x100::X100State::Phase::Initializing)
    return "Initializing control";
  if (state->phase == zhiyun_x100::X100State::Phase::ReadingState)
    return "Reading light state";
  return "Waiting for light";
}

void showForState(const zhiyun_x100::X100State* state) {
  if (studio::devices().pendingAddCommitFailed(instanceId)) {
    pairingScreen.create(onBackEvent, onRetryEvent);
    pairingScreen.setTitle("Zhiyun Light");
    pairingScreen.setStatus("Couldn't save", "Retry to add this device", false,
                            true, "Retry");
    if (lv_scr_act() != pairingScreen.screen())
      lv_scr_load(pairingScreen.screen());
    return;
  }
  if (state != nullptr && state->phase == zhiyun_x100::X100State::Phase::Ready) {
    if (lv_scr_act() != screen) lv_scr_load(screen);
    return;
  }
  pairingScreen.create(onBackEvent, onRetryEvent);
  pairingScreen.setTitle("Zhiyun Light");
  const bool failed = state != nullptr &&
                      state->phase == zhiyun_x100::X100State::Phase::Failed;
  pairingScreen.setStatus(phaseText(state),
                          failed ? "Check the light and retry"
                                 : "Reset or provisioned light nearby",
                          !failed, failed, "Retry");
  if (lv_scr_act() != pairingScreen.screen())
    lv_scr_load(pairingScreen.screen());
}

void processDebouncedEdit(uint32_t now) {
  const studio::DeviceRuntimeState runtime =
      studio::devices().runtimeState(instanceId);
  if (editDispatched) {
    if (runtime.commandPending) {
      editCommandObserved = true;
    } else if (editCommandObserved) {
      editDispatched = false;
      editCommandObserved = false;
    }
  }
  if (!editPending || editDispatched || !runtime.protocolReady ||
      runtime.commandPending || static_cast<int32_t>(now - editDueMs) < 0)
    return;
  captureDraft();
  if (rgbMode)
    enqueue(studio::CommandType::SetLightRgb, draftRgb, draftRgbBrightness);
  else
    enqueue(studio::CommandType::SetLightCct, draftKelvin,
            draftCctBrightness);
  editPending = false;
  editDispatched = true;
}

void refresh() {
  const auto* state = static_cast<const zhiyun_x100::X100State*>(
      studio::devices().specializedState(instanceId));
  showForState(state);
  if (state == nullptr || state->phase != zhiyun_x100::X100State::Phase::Ready)
    return;
  const studio::DeviceRecord* record = studio::devices().find(instanceId);
  lv_label_set_text(title,
                    record != nullptr ? record->displayName : "Zhiyun Light");
  const bool rgbSupported = zhiyun_x100::supportsRgb(state->model);
  if (rgbSupported) {
    lv_obj_clear_flag(modeRgb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_align(modeCct, LV_ALIGN_TOP_MID, -33, 72);
  } else {
    lv_obj_add_flag(modeRgb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_align(modeCct, LV_ALIGN_TOP_MID, 0, 72);
    if (rgbMode) {
      rgbMode = false;
      renderMode();
    }
  }
  if (!draftInitialized) {
    draftKelvin = state->kelvin;
    draftCctBrightness = static_cast<uint8_t>(state->brightness + 0.5f);
    draftRgb = state->rgb;
    draftRgbSaturation = state->saturation;
    draftRgbBrightness = static_cast<uint8_t>(state->brightness + 0.5f);
    rgbMode = rgbSupported && state->mode == zhiyun_x100::X100State::Mode::Rgb;
    draftInitialized = true;
    renderMode();
    restoreDraft();
  }

  char statusText[32];
  if (editPending) {
    std::snprintf(statusText, sizeof(statusText), "ADJUSTING...");
  } else if (state->commandPending || editDispatched) {
    std::snprintf(statusText, sizeof(statusText), "VERIFYING...");
  } else if (state->lastCommandFailed && state->brightnessLimited &&
             state->requestedBrightness > state->maxBrightness) {
    std::snprintf(statusText, sizeof(statusText), "LIMIT %u%%",
                  state->maxBrightness);
  } else if (state->lastCommandFailed && state->verificationField == 1) {
    std::snprintf(statusText, sizeof(statusText), "B %.0f != %.1f",
                  state->requestedBrightness, state->readbackBrightness);
  } else if (state->lastCommandFailed && state->verificationField == 2) {
    std::snprintf(statusText, sizeof(statusText), "%u != %u K",
                  state->requestedKelvin, state->readbackKelvin);
  } else if (state->lastCommandFailed && state->verificationField == 3) {
    std::snprintf(statusText, sizeof(statusText), "H %u != %u",
                  state->requestedHue, state->readbackHue);
  } else if (state->lastCommandFailed && state->verificationField == 4) {
    std::snprintf(statusText, sizeof(statusText), "S %u != %u",
                  state->requestedSaturation, state->readbackSaturation);
  } else {
    std::snprintf(statusText, sizeof(statusText), "%s",
                  state->lastCommandFailed ? "NOT CONFIRMED" : "CONFIRMED");
  }
  lv_label_set_text(status, statusText);
  lv_obj_set_style_text_color(
      status, lv_color_hex(state->lastCommandFailed ? kDanger : kAccent), 0);
  lv_label_set_text(powerLabel, state->on ? "TURN OFF" : "TURN ON");
  lv_obj_set_style_bg_color(powerButton,
                            lv_color_hex(state->on ? kDanger : kAccent), 0);
}

}  // namespace

void init() { ensure(); }

void show(studio::InstanceId id) {
  ensure();
  instanceId = id;
  editPending = false;
  editDispatched = false;
  editCommandObserved = false;
  draftInitialized = false;
  visible = studio::devices().acquire(id, studio::ConnectionOwner::Foreground);
  if (!visible) {
    instanceId = studio::kInvalidInstanceId;
    return;
  }
  lastRefreshMs = 0;
  refresh();
  ui::releaseInactiveScreens();
}

void hide() {
  if (visible)
    studio::devices().release(instanceId, studio::ConnectionOwner::Foreground);
  visible = false;
  instanceId = studio::kInvalidInstanceId;
  editPending = false;
  editDispatched = false;
  editCommandObserved = false;
  draftInitialized = false;
}

void release() {
  if (visible) return;
  if (screen != nullptr) {
    lv_obj_del(screen);
    screen = title = status = cctBody = rgbBody = cctSlider = cctBrightness =
        wheel = rgbSaturation = rgbBrightness = modeCct = modeRgb =
            powerButton = powerLabel = nullptr;
  }
  pairingScreen.destroy();
}

bool active() { return visible; }

void tick() {
  if (!visible) return;
  const uint32_t now = millis();
  processDebouncedEdit(now);
  if (now - lastRefreshMs >= 200) {
    lastRefreshMs = now;
    refresh();
  }
}

void handleShortPress() { onPower(nullptr); }
void handleLongPress() { onBack(); }

#ifdef UI_SIMULATOR
void simShowRgb() {
  rgbMode = true;
  renderMode();
  restoreDraft();
  editPending = false;
}
#endif

}  // namespace zhiyun_x100_ui
