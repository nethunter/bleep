#include "devices/canon_trigger/ui.h"

#include <Arduino.h>
#include <lvgl.h>

#include <cstdio>

#include "core/device_manager.h"
#include "devices/canon_trigger/state.h"
#include "fonts/ui_fonts.h"
#include "haptic_feedback.h"
#include "ui/title_marquee.h"
#include "../../ui.h"

namespace canon_trigger_ui {

namespace {

constexpr uint32_t kBg = 0x05070A;
constexpr uint32_t kPanel = 0x171B22;
constexpr uint32_t kAccent = 0xE53935;
constexpr uint32_t kText = 0xF3F4F6;
constexpr uint32_t kMuted = 0x8A94A6;

lv_obj_t* screen = nullptr;
lv_obj_t* titleLabel = nullptr;
lv_obj_t* statusLabel = nullptr;
lv_obj_t* triggerRing = nullptr;
lv_obj_t* triggerButton = nullptr;
lv_obj_t* triggerLabel = nullptr;
studio::InstanceId instanceId = studio::kInvalidInstanceId;
bool visible = false;
uint32_t lastRefreshMs = 0;

void enqueue(studio::CommandType type) {
  if (instanceId == studio::kInvalidInstanceId) {
    return;
  }
  studio::DeviceCommand command;
  command.instanceId = instanceId;
  command.type = type;
  studio::devices().enqueue(command);
}

const char* linkText(studio::LinkState link) {
  switch (link) {
    case studio::LinkState::Scanning:
      return "PAIRING...";
    case studio::LinkState::Connecting:
      return "CONNECTING...";
    case studio::LinkState::Connected:
      return "CONNECTED";
    case studio::LinkState::Disconnected:
      return "DISCONNECTED";
  }
  return "DISCONNECTED";
}

void refresh() {
  if (screen == nullptr) {
    return;
  }
  const studio::DeviceRuntimeState runtime =
      studio::devices().runtimeState(instanceId);
  const auto* state = static_cast<const canon_trigger::CanonTriggerState*>(
      studio::devices().specializedState(instanceId));

  if (studio::devices().pendingAddCommitFailed(instanceId)) {
    lv_label_set_text(statusLabel, "COULDN'T SAVE");
    lv_obj_clear_state(triggerButton, LV_STATE_DISABLED);
    lv_obj_set_style_bg_color(triggerButton, lv_color_hex(kAccent), 0);
    lv_label_set_text(triggerLabel, "RETRY");
    return;
  }

  char text[64];
  if (state != nullptr && state->triggerPending) {
    std::snprintf(text, sizeof(text), "SENDING TRIGGER...");
  } else if (state != nullptr && state->lastTriggerSucceeded) {
    std::snprintf(text, sizeof(text), "TRIGGER SENT  #%lu",
                  static_cast<unsigned long>(state->triggerCount));
  } else {
    std::snprintf(text, sizeof(text), "%s", linkText(runtime.link));
  }
  lv_label_set_text(statusLabel, text);

  const bool connected = runtime.link == studio::LinkState::Connected;
  if (connected) {
    lv_obj_clear_state(triggerButton, LV_STATE_DISABLED);
    lv_obj_set_style_bg_color(triggerButton, lv_color_hex(kAccent), 0);
    lv_label_set_text(triggerLabel, "RECORD\nTRIGGER");
  } else {
    lv_obj_add_state(triggerButton, LV_STATE_DISABLED);
    lv_obj_set_style_bg_color(triggerButton, lv_color_hex(kPanel), 0);
    lv_label_set_text(triggerLabel, "WAITING");
  }
}

void onBack(lv_event_t*) {
  haptic_feedback::request(haptic_feedback::Pattern::Back);
  hide();
  ui::showDeviceParent();
}

void triggerRecord() {
  if (studio::devices().pendingAddCommitFailed(instanceId)) {
    studio::devices().retryPendingAdd(instanceId);
    return;
  }
  if (studio::devices().runtimeState(instanceId).link !=
      studio::LinkState::Connected) {
    return;
  }
  enqueue(studio::CommandType::RecordTrigger);
}

void onTrigger(lv_event_t*) { triggerRecord(); }

void clearPointers() {
  screen = nullptr;
  titleLabel = nullptr;
  statusLabel = nullptr;
  triggerRing = nullptr;
  triggerButton = nullptr;
  triggerLabel = nullptr;
}

void destroyScreen() {
  if (screen == nullptr || lv_scr_act() == screen) {
    return;
  }
  lv_obj_del(screen);
  clearPointers();
}

void ensureScreen() {
  if (screen != nullptr) {
    return;
  }

  screen = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(screen, lv_color_hex(kBg), 0);
  lv_obj_set_style_text_color(screen, lv_color_hex(kText), 0);
  lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* back = lv_btn_create(screen);
  lv_obj_set_size(back, 34, 30);
  lv_obj_align(back, LV_ALIGN_TOP_LEFT, 40, 22);
  lv_obj_set_style_bg_color(back, lv_color_hex(kPanel), 0);
  lv_obj_set_style_shadow_width(back, 0, 0);
  lv_obj_add_event_cb(back, onBack, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* backLabel = lv_label_create(back);
  lv_label_set_text(backLabel, LV_SYMBOL_LEFT);
  lv_obj_set_style_text_font(backLabel, UI_FONT_16, 0);
  lv_obj_center(backLabel);

  titleLabel = lv_label_create(screen);
  studio_ui::configureTitleMarquee(titleLabel, 132, UI_FONT_16);
  lv_obj_align(titleLabel, LV_ALIGN_TOP_MID, 15, 29);

  statusLabel = lv_label_create(screen);
  lv_obj_set_style_text_font(statusLabel, UI_FONT_14, 0);
  lv_obj_set_style_text_color(statusLabel, lv_color_hex(kMuted), 0);
  lv_obj_align(statusLabel, LV_ALIGN_TOP_MID, 0, 63);

  lv_obj_t* note = lv_label_create(screen);
  lv_label_set_text(note, "STATE UNKNOWN");
  lv_label_set_long_mode(note, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(note, 184);
  lv_obj_set_style_text_align(note, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(note, UI_FONT_14, 0);
  lv_obj_set_style_text_color(note, lv_color_hex(kMuted), 0);
  lv_obj_align(note, LV_ALIGN_TOP_MID, 0, 78);

  triggerRing = lv_obj_create(screen);
  lv_obj_set_size(triggerRing, 140, 140);
  lv_obj_align(triggerRing, LV_ALIGN_BOTTOM_MID, 0, -8);
  lv_obj_set_style_radius(triggerRing, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(triggerRing, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_color(triggerRing, lv_color_hex(kAccent), 0);
  lv_obj_set_style_border_width(triggerRing, 3, 0);
  lv_obj_set_style_pad_all(triggerRing, 8, 0);
  lv_obj_clear_flag(triggerRing, LV_OBJ_FLAG_SCROLLABLE);

  triggerButton = lv_btn_create(triggerRing);
  lv_obj_set_size(triggerButton, 122, 122);
  lv_obj_center(triggerButton);
  lv_obj_set_style_radius(triggerButton, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_shadow_width(triggerButton, 0, 0);
  lv_obj_add_event_cb(triggerButton, onTrigger, LV_EVENT_CLICKED, nullptr);
  triggerLabel = lv_label_create(triggerButton);
  lv_obj_set_style_text_align(triggerLabel, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(triggerLabel, UI_FONT_16, 0);
  lv_obj_set_style_text_color(triggerLabel, lv_color_hex(kText), 0);
  lv_obj_center(triggerLabel);
}

}  // namespace

void init() {}

void show(studio::InstanceId id) {
  ensureScreen();
  instanceId = id;
  const studio::DeviceRecord* record = studio::devices().find(id);
  lv_label_set_text(titleLabel, record != nullptr ? record->displayName : "");
  visible = studio::devices().acquire(id, studio::ConnectionOwner::Foreground);
  if (!visible) {
    instanceId = studio::kInvalidInstanceId;
    return;
  }
  lastRefreshMs = 0;
  refresh();
  lv_scr_load(screen);
  ui::releaseInactiveScreens();
}

void hide() {
  if (visible) {
    studio::devices().release(instanceId, studio::ConnectionOwner::Foreground);
  }
  visible = false;
  instanceId = studio::kInvalidInstanceId;
}

void release() {
  if (visible) {
    return;
  }
  destroyScreen();
}

bool active() { return visible; }

void tick() {
  const uint32_t now = millis();
  if (now - lastRefreshMs >= 200) {
    lastRefreshMs = now;
    refresh();
  }
}

void handleShortPress() {
  triggerRecord();
}

void handleLongPress() { onBack(nullptr); }

}  // namespace canon_trigger_ui
