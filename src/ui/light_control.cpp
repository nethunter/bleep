#include "ui/light_control.h"

#include <Arduino.h>
#include <lvgl.h>

#include "core/device_manager.h"
#include "fonts/ui_fonts.h"
#include "haptic_feedback.h"
#include "ui/round_page.h"

namespace light_control_ui {
namespace {

constexpr uint32_t kBg = 0x05070a;
constexpr uint32_t kPanel = 0x12161d;
constexpr uint32_t kAccent = 0x35c7f2;
constexpr uint32_t kText = 0xf3f4f6;
constexpr uint32_t kMuted = 0x8a94a6;
constexpr uint32_t kDanger = 0xf26d6d;

studio::InstanceId instanceId = studio::kInvalidInstanceId;
BackFn backFn = nullptr;
bool visible = false;
bool rgbMode = false;
bool syncing = false;
bool dirty = false;
bool initialized = false;
uint32_t applyAt = 0;
uint32_t lastRefresh = 0;
uint16_t draftKelvin = 5600;
int16_t draftTint = 0;
uint8_t draftCctBrightness = 50;
uint8_t draftRgbBrightness = 50;
uint32_t draftRgb = 0xffffff;
uint8_t draftSaturation = 100;

lv_obj_t* screen = nullptr;
lv_obj_t* title = nullptr;
lv_obj_t* status = nullptr;
lv_obj_t* cctBody = nullptr;
lv_obj_t* rgbBody = nullptr;
lv_obj_t* kelvin = nullptr;
lv_obj_t* tintRow = nullptr;
lv_obj_t* tint = nullptr;
lv_obj_t* cctBrightness = nullptr;
lv_obj_t* wheel = nullptr;
lv_obj_t* saturation = nullptr;
lv_obj_t* rgbBrightness = nullptr;
lv_obj_t* cctButton = nullptr;
lv_obj_t* rgbButton = nullptr;
lv_obj_t* powerButton = nullptr;
lv_obj_t* powerLabel = nullptr;

bool enqueue(studio::CommandType type, int value0 = 0, int value1 = 0,
             int value2 = 0) {
  studio::DeviceCommand command;
  command.instanceId = instanceId;
  command.type = type;
  command.value0 = value0;
  command.value1 = value1;
  command.value2 = value2;
  return studio::devices().enqueue(command);
}

lv_obj_t* button(lv_obj_t* parent, const char* text, lv_event_cb_t callback,
                 uint32_t color = kPanel) {
  lv_obj_t* object = lv_btn_create(parent);
  lv_obj_set_style_bg_color(object, lv_color_hex(color), 0);
  lv_obj_set_style_radius(object, 8, 0);
  lv_obj_set_style_shadow_width(object, 0, 0);
  lv_obj_t* label = lv_label_create(object);
  lv_label_set_text(label, text);
  lv_obj_set_style_text_font(label, UI_FONT_14, 0);
  lv_obj_center(label);
  if (callback != nullptr)
    lv_obj_add_event_cb(object, callback, LV_EVENT_CLICKED, nullptr);
  return object;
}

lv_obj_t* sliderRow(lv_obj_t* parent, const char* text, int minimum,
                    int maximum, lv_obj_t*& slider, lv_event_cb_t callback) {
  lv_obj_t* row = lv_obj_create(parent);
  lv_obj_set_size(row, 166, 28);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(row, 0, 0);
  lv_obj_set_style_pad_all(row, 0, 0);
  lv_obj_t* label = lv_label_create(row);
  lv_label_set_text(label, text);
  lv_obj_set_style_text_font(label, UI_FONT_14, 0);
  lv_obj_align(label, LV_ALIGN_LEFT_MID, 0, 0);
  slider = lv_slider_create(row);
  lv_obj_set_size(slider, 104, 10);
  lv_obj_align(slider, LV_ALIGN_RIGHT_MID, 0, 0);
  lv_slider_set_range(slider, minimum, maximum);
  lv_obj_add_event_cb(slider, callback, LV_EVENT_VALUE_CHANGED, nullptr);
  return row;
}

void capture() {
  if (syncing) return;
  if (rgbMode) {
    draftRgbBrightness = lv_slider_get_value(rgbBrightness);
    const lv_color_hsv_t hsv = lv_colorwheel_get_hsv(wheel);
    draftSaturation = lv_slider_get_value(saturation);
    draftRgb = lv_color_to32(
        lv_color_hsv_to_rgb(hsv.h, draftSaturation, 100)) & 0xffffff;
  } else {
    draftCctBrightness = lv_slider_get_value(cctBrightness);
    draftKelvin = lv_slider_get_value(kelvin);
    draftTint = lv_slider_get_value(tint);
  }
}

void restore() {
  syncing = true;
  if (rgbMode) {
    const lv_color_hsv_t hsv = lv_color_rgb_to_hsv(
        static_cast<uint8_t>(draftRgb >> 16),
        static_cast<uint8_t>(draftRgb >> 8), static_cast<uint8_t>(draftRgb));
    lv_colorwheel_set_hsv(wheel, lv_color_hsv_t{hsv.h, 100, 100});
    lv_slider_set_value(saturation, draftSaturation, LV_ANIM_OFF);
    lv_slider_set_value(rgbBrightness, draftRgbBrightness, LV_ANIM_OFF);
  } else {
    lv_slider_set_value(kelvin, draftKelvin, LV_ANIM_OFF);
    lv_slider_set_value(tint, draftTint, LV_ANIM_OFF);
    lv_slider_set_value(cctBrightness, draftCctBrightness, LV_ANIM_OFF);
  }
  syncing = false;
}

void renderMode(const studio::LightControlState& state) {
  const bool hasLooks = state.supportsCct || state.supportsRgb;
  if (hasLooks) {
    lv_obj_clear_flag(cctButton, LV_OBJ_FLAG_HIDDEN);
    if (state.supportsRgb) lv_obj_clear_flag(rgbButton, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(rgbButton, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(cctButton, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(rgbButton, LV_OBJ_FLAG_HIDDEN);
  }
  lv_obj_set_style_bg_color(cctButton,
                            lv_color_hex(rgbMode ? kPanel : kAccent), 0);
  lv_obj_set_style_bg_color(rgbButton,
                            lv_color_hex(rgbMode ? kAccent : kPanel), 0);
  if (rgbMode && state.supportsRgb) {
    lv_obj_add_flag(cctBody, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(rgbBody, LV_OBJ_FLAG_HIDDEN);
  } else if (state.supportsCct) {
    lv_obj_add_flag(rgbBody, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(cctBody, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(cctBody, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(rgbBody, LV_OBJ_FLAG_HIDDEN);
  }
  if (state.supportsTint) lv_obj_clear_flag(tintRow, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(tintRow, LV_OBJ_FLAG_HIDDEN);
}

void markDirty(lv_event_t*) {
  if (syncing) return;
  capture();
  dirty = true;
  applyAt = millis() + 350;
}

void setMode(bool rgb) {
  studio::LightControlState state;
  if (!studio::devices().lightControlState(instanceId, state) ||
      (rgb && !state.supportsRgb)) return;
  capture();
  rgbMode = rgb;
  renderMode(state);
  restore();
  dirty = true;
  applyAt = millis() + 350;
}

void onCct(lv_event_t*) { setMode(false); }
void onRgb(lv_event_t*) { setMode(true); }

void onPower(lv_event_t*) {
  studio::LightControlState state;
  const studio::DeviceRuntimeState runtime =
      studio::devices().runtimeState(instanceId);
  if (!studio::devices().lightControlState(instanceId, state) ||
      !state.supportsPower || !state.available || !runtime.protocolReady ||
      state.commandPending) return;
  enqueue(state.on ? studio::CommandType::TurnOff : studio::CommandType::TurnOn);
}

void goBack(lv_event_t*) {
  haptic_feedback::request(haptic_feedback::Pattern::Back);
  if (backFn != nullptr) backFn();
}

void ensure() {
  if (screen != nullptr) return;
  screen = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(screen, lv_color_hex(kBg), 0);
  lv_obj_set_style_text_color(screen, lv_color_hex(kText), 0);
  lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
  studio_ui::RoundPageHeaderOptions header;
  header.onBack = goBack;
  header.panelColor = kPanel;
  header.textColor = kText;
  title = studio_ui::createRoundPageHeader(screen, header).title;
  status = lv_label_create(screen);
  lv_obj_set_width(status, 178);
  lv_obj_set_style_text_align(status, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(status, UI_FONT_14, 0);
  lv_obj_set_style_text_color(status, lv_color_hex(kMuted), 0);
  lv_obj_align(status, LV_ALIGN_TOP_MID, 0, 51);
  cctButton = button(screen, "CCT", onCct, kAccent);
  lv_obj_set_size(cctButton, 58, 27);
  lv_obj_align(cctButton, LV_ALIGN_TOP_MID, -33, 72);
  rgbButton = button(screen, "RGB", onRgb);
  lv_obj_set_size(rgbButton, 58, 27);
  lv_obj_align(rgbButton, LV_ALIGN_TOP_MID, 33, 72);
  cctBody = lv_obj_create(screen);
  lv_obj_set_size(cctBody, 174, 88);
  lv_obj_align(cctBody, LV_ALIGN_TOP_MID, 0, 103);
  lv_obj_set_style_bg_opa(cctBody, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(cctBody, 0, 0);
  lv_obj_set_style_pad_all(cctBody, 2, 0);
  lv_obj_set_style_pad_row(cctBody, 0, 0);
  lv_obj_set_flex_flow(cctBody, LV_FLEX_FLOW_COLUMN);
  sliderRow(cctBody, "K", 2300, 10000, kelvin, markDirty);
  tintRow = sliderRow(cctBody, "Tint", -1000, 1000, tint, markDirty);
  sliderRow(cctBody, "Bri", 0, 100, cctBrightness, markDirty);
  rgbBody = lv_obj_create(screen);
  lv_obj_set_size(rgbBody, 174, 88);
  lv_obj_align(rgbBody, LV_ALIGN_TOP_MID, 0, 103);
  lv_obj_set_style_bg_opa(rgbBody, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(rgbBody, 0, 0);
  lv_obj_set_style_pad_all(rgbBody, 0, 0);
  wheel = lv_colorwheel_create(rgbBody, true);
  lv_obj_set_size(wheel, 72, 72);
  lv_obj_align(wheel, LV_ALIGN_LEFT_MID, 5, 0);
  lv_obj_add_event_cb(wheel, markDirty, LV_EVENT_VALUE_CHANGED, nullptr);
  lv_obj_t* satLabel = lv_label_create(rgbBody);
  lv_label_set_text(satLabel, "Sat");
  lv_obj_align(satLabel, LV_ALIGN_TOP_RIGHT, -55, 6);
  saturation = lv_slider_create(rgbBody);
  lv_obj_set_size(saturation, 72, 8);
  lv_obj_align(saturation, LV_ALIGN_TOP_RIGHT, -3, 25);
  lv_slider_set_range(saturation, 0, 100);
  lv_obj_add_event_cb(saturation, markDirty, LV_EVENT_VALUE_CHANGED, nullptr);
  lv_obj_t* briLabel = lv_label_create(rgbBody);
  lv_label_set_text(briLabel, "Bri");
  lv_obj_align(briLabel, LV_ALIGN_TOP_RIGHT, -55, 43);
  rgbBrightness = lv_slider_create(rgbBody);
  lv_obj_set_size(rgbBrightness, 72, 8);
  lv_obj_align(rgbBrightness, LV_ALIGN_TOP_RIGHT, -3, 62);
  lv_slider_set_range(rgbBrightness, 0, 100);
  lv_obj_add_event_cb(rgbBrightness, markDirty, LV_EVENT_VALUE_CHANGED, nullptr);
  powerButton = button(screen, "POWER", onPower, kAccent);
  powerLabel = lv_obj_get_child(powerButton, 0);
  lv_obj_set_size(powerButton, 94, 28);
  lv_obj_align(powerButton, LV_ALIGN_BOTTOM_MID, 0, -16);
}

void refresh() {
  const studio::DeviceRecord* record = studio::devices().find(instanceId);
  const studio::DeviceRuntimeState runtime =
      studio::devices().runtimeState(instanceId);
  studio::LightControlState state;
  if (!studio::devices().lightControlState(instanceId, state)) return;
  lv_label_set_text(title, record != nullptr ? record->displayName : "Light");
  lv_label_set_text(status, state.commandPending ? "Sending..."
                            : state.commandFailed ? "Action failed"
                                                  : state.status);
  lv_obj_set_style_text_color(
      status, lv_color_hex(state.commandFailed ? kDanger : kMuted), 0);
  lv_slider_set_range(kelvin, state.minKelvin, state.maxKelvin);
  if (!initialized) {
    draftKelvin = state.kelvin;
    draftTint = state.tintPermille;
    draftCctBrightness = state.brightness;
    draftRgbBrightness = state.brightness;
    draftRgb = state.rgb;
    const lv_color_hsv_t hsv = lv_color_rgb_to_hsv(
        static_cast<uint8_t>(draftRgb >> 16),
        static_cast<uint8_t>(draftRgb >> 8), static_cast<uint8_t>(draftRgb));
    draftSaturation = hsv.s;
    rgbMode = state.rgbMode && state.supportsRgb;
    initialized = true;
    restore();
  }
  renderMode(state);
  lv_obj_set_style_bg_color(powerButton,
                            lv_color_hex(state.on ? kDanger : kAccent), 0);
  lv_label_set_text(powerLabel, state.on ? "TURN OFF" : "TURN ON");
  const bool enabled = state.available && runtime.protocolReady &&
                       !state.commandPending;
  lv_obj_t* controls[] = {kelvin, tint, cctBrightness, wheel, saturation,
                          rgbBrightness, cctButton, rgbButton, powerButton};
  for (lv_obj_t* control : controls) {
    if (enabled) lv_obj_clear_state(control, LV_STATE_DISABLED);
    else lv_obj_add_state(control, LV_STATE_DISABLED);
  }
}

void apply() {
  studio::LightControlState state;
  const studio::DeviceRuntimeState runtime = studio::devices().runtimeState(instanceId);
  if (!studio::devices().lightControlState(instanceId, state) ||
      !runtime.protocolReady || runtime.commandPending) return;
  capture();
  const bool queued = rgbMode
      ? enqueue(studio::CommandType::SetLightRgb, draftRgb, draftRgbBrightness)
      : enqueue(studio::CommandType::SetLightCct, draftKelvin, draftCctBrightness,
                state.supportsTint ? draftTint : 0);
  if (queued) dirty = false;
}

}  // namespace

void show(studio::InstanceId id, BackFn onBack, bool applyDisplayedLook) {
  ensure();
  instanceId = id;
  backFn = onBack;
  visible = true;
  dirty = false;
  initialized = applyDisplayedLook;
  if (applyDisplayedLook) {
    rgbMode = false;
    draftKelvin = 5600;
    draftTint = 0;
    draftCctBrightness = 50;
    draftRgbBrightness = 50;
    draftRgb = 0xffffff;
    draftSaturation = 100;
  }
  refresh();
  if (applyDisplayedLook) {
    restore();
    dirty = true;
    applyAt = millis() + 350;
  }
  lv_scr_load(screen);
}

void hide() { visible = false; instanceId = studio::kInvalidInstanceId; }
void release() {
  if (visible || screen == nullptr) return;
  lv_obj_del(screen);
  screen = title = status = cctBody = rgbBody = kelvin = tintRow = tint =
      cctBrightness = wheel = saturation = rgbBrightness = cctButton =
      rgbButton = powerButton = powerLabel = nullptr;
}
bool active() { return visible; }
void tick() {
  if (!visible) return;
  const uint32_t now = millis();
  if (dirty && static_cast<int32_t>(now - applyAt) >= 0) apply();
  if (now - lastRefresh >= 250) { lastRefresh = now; refresh(); }
}
void handleShortPress() { onPower(nullptr); }
void handleLongPress() { goBack(nullptr); }

#ifdef UI_SIMULATOR
void simShowCct() { setMode(false); }
void simShowRgb() { setMode(true); }
void simSetCctLook(int value, int tintValue, int brightness) {
  setMode(false); syncing = true;
  lv_slider_set_value(kelvin, value, LV_ANIM_OFF);
  lv_slider_set_value(tint, tintValue, LV_ANIM_OFF);
  lv_slider_set_value(cctBrightness, brightness, LV_ANIM_OFF);
  syncing = false; markDirty(nullptr);
}
void simSetRgbLook(uint32_t value, int brightness) {
  setMode(true);
  const lv_color_hsv_t hsv = lv_color_rgb_to_hsv(
      static_cast<uint8_t>(value >> 16), static_cast<uint8_t>(value >> 8),
      static_cast<uint8_t>(value));
  syncing = true;
  lv_colorwheel_set_hsv(wheel, lv_color_hsv_t{hsv.h, 100, 100});
  lv_slider_set_value(saturation, hsv.s, LV_ANIM_OFF);
  lv_slider_set_value(rgbBrightness, brightness, LV_ANIM_OFF);
  syncing = false; markDirty(nullptr);
}
int simRgbSaturation() { return lv_slider_get_value(saturation); }
#endif

}  // namespace light_control_ui
