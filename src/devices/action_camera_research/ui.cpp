#include "devices/action_camera_research/ui.h"

#include <lvgl.h>

#include "core/device_manager.h"
#include "fonts/ui_fonts.h"
#include "haptic_feedback.h"
#include "../../ui.h"

namespace action_camera_research_ui {
namespace {
lv_obj_t* screen = nullptr;
studio::InstanceId instanceId = studio::kInvalidInstanceId;
bool visible = false;

void onBack(lv_event_t*) {
  haptic_feedback::request(haptic_feedback::Pattern::Back);
  hide();
  ui::showDeviceParent();
}

void ensureScreen() {
  if (screen != nullptr) return;
  screen = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(screen, lv_color_hex(0x05070A), 0);
  lv_obj_set_style_text_color(screen, lv_color_hex(0xF3F4F6), 0);
  lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t* back = lv_btn_create(screen);
  lv_obj_set_size(back, 34, 30);
  lv_obj_align(back, LV_ALIGN_TOP_LEFT, 40, 22);
  lv_obj_set_style_bg_color(back, lv_color_hex(0x171B22), 0);
  lv_obj_set_style_shadow_width(back, 0, 0);
  lv_obj_add_event_cb(back, onBack, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* backLabel = lv_label_create(back);
  lv_label_set_text(backLabel, LV_SYMBOL_LEFT);
  lv_obj_set_style_text_font(backLabel, UI_FONT_16, 0);
  lv_obj_center(backLabel);
  lv_obj_t* title = lv_label_create(screen);
  lv_label_set_text(title, "PROTOCOL RESEARCH");
  lv_obj_set_style_text_font(title, UI_FONT_16, 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 16, 29);
  lv_obj_t* body = lv_label_create(screen);
  lv_label_set_text(body, "CAPTURE REQUIRED\n\nThis camera family is listed,\nbut pairing is blocked until its\nBLE protocol is verified.");
  lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(body, 174);
  lv_obj_set_style_text_align(body, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(body, UI_FONT_14, 0);
  lv_obj_set_style_text_color(body, lv_color_hex(0x8A94A6), 0);
  lv_obj_align(body, LV_ALIGN_CENTER, 0, 12);
}
}  // namespace
void show(studio::InstanceId id) {
  ensureScreen();
  instanceId = id;
  visible = studio::devices().acquire(id, studio::ConnectionOwner::Foreground);
  if (!visible) return;
  lv_scr_load(screen);
  ui::releaseInactiveScreens();
}
void hide() {
  if (visible) studio::devices().release(instanceId, studio::ConnectionOwner::Foreground);
  visible = false;
  instanceId = studio::kInvalidInstanceId;
}
void release() {
  if (!visible && screen != nullptr && lv_scr_act() != screen) {
    lv_obj_del(screen);
    screen = nullptr;
  }
}
bool active() { return visible; }
void handleLongPress() { onBack(nullptr); }
}  // namespace action_camera_research_ui
