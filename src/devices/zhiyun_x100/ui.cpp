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
bool syncing = false;
bool editPending = false;
bool editDispatched = false;
bool editCommandObserved = false;
bool localTargetActive = false;
uint32_t editDueMs = 0;
uint32_t lastRefreshMs = 0;
lv_obj_t* screen = nullptr;
lv_obj_t* title = nullptr;
lv_obj_t* status = nullptr;
lv_obj_t* cctValue = nullptr;
lv_obj_t* brightnessValue = nullptr;
lv_obj_t* cctSlider = nullptr;
lv_obj_t* brightnessSlider = nullptr;
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

void onBack() {
  haptic_feedback::request(haptic_feedback::Pattern::Back);
  hide();
  ui::showDeviceParent();
}

void onRetry() {
  if (studio::devices().pendingAddCommitFailed(instanceId)) {
    studio::devices().retryPendingAdd(instanceId);
  } else {
    enqueue(studio::CommandType::Connect);
  }
}

void onBackEvent(lv_event_t*) { onBack(); }
void onRetryEvent(lv_event_t*) { onRetry(); }

void onSliderChange(lv_event_t*) {
  if (syncing || instanceId == studio::kInvalidInstanceId) return;
  editPending = true;
  localTargetActive = true;
  editDueMs = millis() + 350;
  char text[20];
  std::snprintf(text, sizeof(text), "%ld K",
                static_cast<long>(lv_slider_get_value(cctSlider)));
  lv_label_set_text(cctValue, text);
  std::snprintf(text, sizeof(text), "%ld%%",
                static_cast<long>(lv_slider_get_value(brightnessSlider)));
  lv_label_set_text(brightnessValue, text);
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
      if (!editPending && !runtime.commandFailed) localTargetActive = false;
    }
  }
  if (!editPending || editDispatched || !runtime.protocolReady ||
      runtime.commandPending || static_cast<int32_t>(now - editDueMs) < 0)
    return;
  const uint16_t kelvin = zhiyun_x100::normalizeCct(
      static_cast<uint16_t>(lv_slider_get_value(cctSlider)));
  syncing = true;
  lv_slider_set_value(cctSlider, kelvin, LV_ANIM_OFF);
  syncing = false;
  enqueue(studio::CommandType::SetLightCct, kelvin,
          lv_slider_get_value(brightnessSlider));
  editPending = false;
  editDispatched = true;
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

lv_obj_t* makeLabel(lv_obj_t* parent, const char* text, lv_coord_t y,
                    const lv_font_t* font = UI_FONT_14,
                    uint32_t color = kText) {
  lv_obj_t* label = lv_label_create(parent);
  lv_label_set_text(label, text);
  lv_obj_set_style_text_font(label, font, 0);
  lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
  lv_obj_align(label, LV_ALIGN_TOP_MID, 0, y);
  return label;
}

lv_obj_t* makeButton(lv_obj_t* parent, const char* text, lv_event_cb_t callback,
                     uint32_t color) {
  lv_obj_t* button = lv_btn_create(parent);
  lv_obj_set_style_bg_color(button, lv_color_hex(color), 0);
  lv_obj_set_style_radius(button, 9, 0);
  lv_obj_set_style_shadow_width(button, 0, 0);
  lv_obj_t* label = lv_label_create(button);
  lv_label_set_text(label, text);
  lv_obj_set_style_text_font(label, UI_FONT_14, 0);
  lv_obj_center(label);
  lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, nullptr);
  return button;
}

void ensure() {
  if (screen != nullptr) return;
  screen = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(screen, lv_color_hex(kBackground), 0);
  lv_obj_set_style_text_color(screen, lv_color_hex(kText), 0);
  lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* back = makeButton(screen, LV_SYMBOL_LEFT, onBackEvent, kPanel);
  lv_obj_set_size(back, 32, 28);
  lv_obj_align(back, LV_ALIGN_TOP_LEFT, 38, 28);
  title = makeLabel(screen, "MOLUS X100", 28, UI_FONT_16);
  status = makeLabel(screen, "CONFIRMED", 51, UI_FONT_14, kAccent);

  lv_obj_t* cctLabel = makeLabel(screen, "CCT", 76, UI_FONT_14, kMuted);
  lv_obj_align(cctLabel, LV_ALIGN_TOP_LEFT, 50, 76);
  cctValue = makeLabel(screen, "5600 K", 74, UI_FONT_14);
  lv_obj_align(cctValue, LV_ALIGN_TOP_RIGHT, -47, 74);
  cctSlider = lv_slider_create(screen);
  lv_obj_set_size(cctSlider, 142, 10);
  lv_obj_align(cctSlider, LV_ALIGN_TOP_MID, 0, 98);
  lv_slider_set_range(cctSlider, zhiyun_x100::kMinKelvin,
                      zhiyun_x100::kMaxKelvin);
  lv_obj_add_event_cb(cctSlider, onSliderChange, LV_EVENT_VALUE_CHANGED,
                      nullptr);

  lv_obj_t* brightnessLabel =
      makeLabel(screen, "BRIGHTNESS", 120, UI_FONT_14, kMuted);
  lv_obj_align(brightnessLabel, LV_ALIGN_TOP_LEFT, 50, 120);
  brightnessValue = makeLabel(screen, "0%", 118, UI_FONT_14);
  lv_obj_align(brightnessValue, LV_ALIGN_TOP_RIGHT, -49, 118);
  brightnessSlider = lv_slider_create(screen);
  lv_obj_set_size(brightnessSlider, 142, 10);
  lv_obj_align(brightnessSlider, LV_ALIGN_TOP_MID, 0, 143);
  lv_slider_set_range(brightnessSlider, 0, 100);
  lv_obj_add_event_cb(brightnessSlider, onSliderChange,
                      LV_EVENT_VALUE_CHANGED, nullptr);

  powerButton = makeButton(screen, "POWER", onPower, kAccent);
  lv_obj_set_size(powerButton, 96, 30);
  lv_obj_align(powerButton, LV_ALIGN_BOTTOM_MID, 0, -27);
  powerLabel = lv_obj_get_child(powerButton, 0);
}

const char* phaseText(const zhiyun_x100::X100State* state) {
  if (state == nullptr) return "Unavailable";
  if (state->phase == zhiyun_x100::X100State::Phase::Failed)
    return state->error[0] != '\0' ? state->error : "Connection failed";
  if (state->link == zhiyun_x100::X100State::Link::Scanning)
    return "Scanning for PL105";
  if (state->link == zhiyun_x100::X100State::Link::Connecting)
    return "Connecting";
  if (state->phase == zhiyun_x100::X100State::Phase::Provisioning)
    return "Creating mesh";
  if (state->phase == zhiyun_x100::X100State::Phase::Initializing)
    return "Initializing control";
  if (state->phase == zhiyun_x100::X100State::Phase::ReadingState)
    return "Reading light state";
  return "Waiting for X100";
}

void showForState(const zhiyun_x100::X100State* state) {
  if (studio::devices().pendingAddCommitFailed(instanceId)) {
    pairingScreen.create(onBackEvent, onRetryEvent);
    pairingScreen.setTitle("MOLUS X100");
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
  pairingScreen.setTitle("MOLUS X100");
  const bool failed = state != nullptr &&
                      state->phase == zhiyun_x100::X100State::Phase::Failed;
  pairingScreen.setStatus(phaseText(state),
                          failed ? "Check the light and retry"
                                 : "Reset or provisioned X100 nearby",
                          !failed, failed, "Retry");
  if (lv_scr_act() != pairingScreen.screen()) lv_scr_load(pairingScreen.screen());
}

void refresh() {
  const auto* state = static_cast<const zhiyun_x100::X100State*>(
      studio::devices().specializedState(instanceId));
  showForState(state);
  if (state == nullptr || state->phase != zhiyun_x100::X100State::Phase::Ready)
    return;
  const studio::DeviceRecord* record = studio::devices().find(instanceId);
  lv_label_set_text(title, record != nullptr ? record->displayName : "MOLUS X100");
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
  } else {
    std::snprintf(statusText, sizeof(statusText), "%s",
                  state->lastCommandFailed ? "NOT CONFIRMED" : "CONFIRMED");
  }
  lv_label_set_text(status, statusText);
  lv_obj_set_style_text_color(
      status,
      lv_color_hex(state->lastCommandFailed ? kDanger : kAccent), 0);
  if (!localTargetActive && !state->commandPending) {
    syncing = true;
    lv_slider_set_value(cctSlider, state->kelvin, LV_ANIM_OFF);
    lv_slider_set_value(brightnessSlider,
                        static_cast<int>(state->brightness + 0.5f), LV_ANIM_OFF);
    syncing = false;
  }
  char text[20];
  const uint16_t shownKelvin = localTargetActive
                                   ? static_cast<uint16_t>(
                                         lv_slider_get_value(cctSlider))
                                   : state->kelvin;
  const float shownBrightness =
      localTargetActive ? lv_slider_get_value(brightnessSlider)
                        : state->brightness;
  std::snprintf(text, sizeof(text), "%u K", shownKelvin);
  lv_label_set_text(cctValue, text);
  std::snprintf(text, sizeof(text), "%.1f%%", shownBrightness);
  lv_label_set_text(brightnessValue, text);
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
  localTargetActive = false;
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
  localTargetActive = false;
}

void release() {
  if (visible) return;
  if (screen != nullptr) {
    lv_obj_del(screen);
    screen = title = status = cctValue = brightnessValue = cctSlider =
        brightnessSlider = powerButton = powerLabel = nullptr;
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

}  // namespace zhiyun_x100_ui
