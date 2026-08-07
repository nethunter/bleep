#include "devices/home_assistant/ui.h"

#include <Arduino.h>
#include <lvgl.h>

#include "core/device_manager.h"
#include "devices/home_assistant/client.h"
#include "fonts/ui_fonts.h"
#include "haptic_feedback.h"
#include "../../ui.h"

namespace home_assistant_ui {
namespace {

constexpr uint32_t kBg = 0x05070A;
constexpr uint32_t kPanel = 0x12161D;
constexpr uint32_t kAccent = 0x35C7F2;
constexpr uint32_t kText = 0xF3F4F6;
constexpr uint32_t kMuted = 0x8A94A6;
constexpr uint32_t kOk = 0x3DDC97;
constexpr uint32_t kOff = 0x46505F;
constexpr uint32_t kDanger = 0xF26D6D;

studio::InstanceId instanceId = studio::kInvalidInstanceId;
bool visible = false;
lv_obj_t* screen = nullptr;
lv_obj_t* title = nullptr;
lv_obj_t* status = nullptr;
lv_obj_t* entity = nullptr;
lv_obj_t* primary = nullptr;
lv_obj_t* secondary = nullptr;
uint32_t lastRefresh = 0;

lv_obj_t* button(lv_obj_t* parent, const char* text, lv_event_cb_t callback,
                 uint32_t color) {
  lv_obj_t* object = lv_btn_create(parent);
  lv_obj_set_style_bg_color(object, lv_color_hex(color), 0);
  lv_obj_set_style_radius(object, 10, 0);
  lv_obj_set_style_shadow_width(object, 0, 0);
  lv_obj_t* label = lv_label_create(object);
  lv_label_set_text(label, text);
  lv_obj_set_style_text_font(label, UI_FONT_14, 0);
  lv_obj_set_style_text_color(label, lv_color_hex(kText), 0);
  lv_obj_center(label);
  if (callback != nullptr) lv_obj_add_event_cb(object, callback, LV_EVENT_CLICKED, nullptr);
  return object;
}

void enqueue(studio::CommandType type) {
  studio::DeviceCommand command;
  command.instanceId = instanceId;
  command.type = type;
  studio::devices().enqueue(command);
}

void onBack(lv_event_t*) {
  haptic_feedback::request(haptic_feedback::Pattern::Back);
  hide();
  ui::showDeviceParent();
}
void onPrimary(lv_event_t*) {
  const studio::DeviceRecord* record = studio::devices().find(instanceId);
  if (record == nullptr) return;
  switch (record->homeAssistantDomain) {
    case studio::HomeAssistantDomain::Light:
    case studio::HomeAssistantDomain::Switch:
      enqueue(studio::CommandType::TurnOn);
      break;
    case studio::HomeAssistantDomain::InputBoolean:
      {
        const auto* state = static_cast<const home_assistant::EntityState*>(
            studio::devices().specializedState(instanceId));
        if (state == nullptr || !state->stateKnown) break;
        enqueue(state->on ? studio::CommandType::TurnOff
                          : studio::CommandType::TurnOn);
      }
      break;
    case studio::HomeAssistantDomain::Button:
      enqueue(studio::CommandType::Press);
      break;
    case studio::HomeAssistantDomain::Scene:
    case studio::HomeAssistantDomain::Script:
      enqueue(studio::CommandType::Activate);
      break;
    case studio::HomeAssistantDomain::None:
      break;
  }
}
void onSecondary(lv_event_t*) { enqueue(studio::CommandType::TurnOff); }

void build() {
  if (screen != nullptr) return;
  screen = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(screen, lv_color_hex(kBg), 0);
  lv_obj_set_style_text_color(screen, lv_color_hex(kText), 0);
  lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t* back = button(screen, LV_SYMBOL_LEFT, onBack, kPanel);
  lv_obj_set_size(back, 34, 30);
  lv_obj_align(back, LV_ALIGN_TOP_LEFT, 40, 36);
  title = lv_label_create(screen);
  lv_obj_set_width(title, 126);
  lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(title, UI_FONT_16, 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 10, 42);
  status = lv_label_create(screen);
  lv_obj_set_width(status, 180);
  lv_obj_set_style_text_align(status, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(status, UI_FONT_20, 0);
  lv_obj_align(status, LV_ALIGN_TOP_MID, 0, 83);
  entity = lv_label_create(screen);
  lv_obj_set_width(entity, 178);
  lv_label_set_long_mode(entity, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_align(entity, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(entity, UI_FONT_14, 0);
  lv_obj_set_style_text_color(entity, lv_color_hex(kMuted), 0);
  lv_obj_align(entity, LV_ALIGN_TOP_MID, 0, 113);
  primary = button(screen, "ON", onPrimary, kOk);
  lv_obj_set_size(primary, 76, 38);
  lv_obj_align(primary, LV_ALIGN_BOTTOM_MID, -42, -29);
  secondary = button(screen, "OFF", onSecondary, kOff);
  lv_obj_set_size(secondary, 76, 38);
  lv_obj_align(secondary, LV_ALIGN_BOTTOM_MID, 42, -29);
}

void setPrimaryLabel(const char* text) {
  lv_label_set_text(lv_obj_get_child(primary, 0), text);
}

void refresh() {
  if (screen == nullptr) return;
  const studio::DeviceRecord* record = studio::devices().find(instanceId);
  const studio::DeviceRuntimeState runtime = studio::devices().runtimeState(instanceId);
  const auto* state = static_cast<const home_assistant::EntityState*>(
      studio::devices().specializedState(instanceId));
  const bool stateful = record != nullptr &&
      (record->homeAssistantDomain == studio::HomeAssistantDomain::Light ||
       record->homeAssistantDomain == studio::HomeAssistantDomain::Switch ||
       record->homeAssistantDomain == studio::HomeAssistantDomain::InputBoolean);
  const bool toggleOnly = record != nullptr &&
      record->homeAssistantDomain == studio::HomeAssistantDomain::InputBoolean;
  lv_label_set_text(title, record != nullptr ? record->displayName : "HA Entity");
  lv_label_set_text(entity, record != nullptr ? record->homeAssistantEntityId : "");
  if (runtime.link == studio::LinkState::Connecting) {
    lv_label_set_text(status, "CONNECTING");
    lv_obj_set_style_text_color(status, lv_color_hex(kAccent), 0);
  } else if (runtime.link != studio::LinkState::Connected || state == nullptr) {
    lv_label_set_text(status, "OFFLINE");
    lv_obj_set_style_text_color(status, lv_color_hex(kDanger), 0);
  } else if (!state->present) {
    lv_label_set_text(status, "MISSING");
    lv_obj_set_style_text_color(status, lv_color_hex(kDanger), 0);
  } else if (!state->available) {
    lv_label_set_text(status, "UNAVAILABLE");
    lv_obj_set_style_text_color(status, lv_color_hex(kDanger), 0);
  } else if (state->commandPending) {
    lv_label_set_text(status, "SENDING...");
    lv_obj_set_style_text_color(status, lv_color_hex(kAccent), 0);
  } else if (state->commandAttempted &&
             state->lastCommand != studio::CommandStatus::Succeeded &&
             state->lastCommand != studio::CommandStatus::Queued) {
    lv_label_set_text(status, "ACTION FAILED");
    lv_obj_set_style_text_color(status, lv_color_hex(kDanger), 0);
  } else {
    lv_label_set_text(status, state->stateKnown ? (state->on ? "ON" : "OFF")
                                                  : (stateful ? "UNKNOWN" : "READY"));
    lv_obj_set_style_text_color(status, lv_color_hex(state->on ? kOk : kText), 0);
  }
  if (toggleOnly) {
    setPrimaryLabel(state != nullptr && state->stateKnown && state->on ? "OFF" : "ON");
    lv_obj_add_flag(secondary, LV_OBJ_FLAG_HIDDEN);
    lv_obj_align(primary, LV_ALIGN_BOTTOM_MID, 0, -29);
  } else if (stateful) {
    setPrimaryLabel("ON");
    lv_obj_clear_flag(secondary, LV_OBJ_FLAG_HIDDEN);
    lv_obj_align(primary, LV_ALIGN_BOTTOM_MID, -42, -29);
  } else {
    setPrimaryLabel(record != nullptr &&
                            record->homeAssistantDomain == studio::HomeAssistantDomain::Button
                        ? "PRESS" : "ACTIVATE");
    lv_obj_add_flag(secondary, LV_OBJ_FLAG_HIDDEN);
    lv_obj_align(primary, LV_ALIGN_BOTTOM_MID, 0, -29);
  }
  const bool enabled = runtime.protocolReady && state != nullptr &&
                       state->available && !state->commandPending &&
                       (!toggleOnly || state->stateKnown);
  if (enabled) {
    lv_obj_clear_state(primary, LV_STATE_DISABLED);
    lv_obj_clear_state(secondary, LV_STATE_DISABLED);
  } else {
    lv_obj_add_state(primary, LV_STATE_DISABLED);
    lv_obj_add_state(secondary, LV_STATE_DISABLED);
  }
}

}  // namespace

void show(studio::InstanceId id) {
  build();
  instanceId = id;
  visible = studio::devices().acquire(id, studio::ConnectionOwner::Foreground);
  if (!visible) { instanceId = studio::kInvalidInstanceId; return; }
  refresh();
  lv_scr_load(screen);
  ui::releaseInactiveScreens();
}
void hide() {
  if (visible) studio::devices().release(instanceId, studio::ConnectionOwner::Foreground);
  visible = false;
  instanceId = studio::kInvalidInstanceId;
}
void release() {
  if (!visible && screen != nullptr) {
    lv_obj_del(screen);
    screen = title = status = entity = primary = secondary = nullptr;
  }
}
bool active() { return visible; }
void tick() {
  const uint32_t now = millis();
  if (now - lastRefresh >= 200) { lastRefresh = now; refresh(); }
}
void handleShortPress() { onPrimary(nullptr); }
void handleLongPress() { onBack(nullptr); }

}  // namespace home_assistant_ui
