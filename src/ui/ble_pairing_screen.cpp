#include "ui/ble_pairing_screen.h"

#include "fonts/ui_fonts.h"

namespace studio_ui {
namespace {
constexpr uint32_t kBg = 0x05070a;
constexpr uint32_t kPanel = 0x12161d;
constexpr uint32_t kPanelRaised = 0x1a2029;
constexpr uint32_t kAccent = 0x35c7f2;
constexpr uint32_t kText = 0xf3f4f6;
constexpr uint32_t kMuted = 0x8a94a6;

lv_obj_t* button(lv_obj_t* parent, const char* text, lv_event_cb_t callback) {
  lv_obj_t* result = lv_btn_create(parent);
  lv_obj_set_style_bg_color(result, lv_color_hex(kPanel), 0);
  lv_obj_set_style_radius(result, 8, 0);
  lv_obj_set_style_shadow_width(result, 0, 0);
  lv_obj_set_style_border_color(result, lv_color_hex(kPanelRaised), 0);
  lv_obj_set_style_border_width(result, 1, 0);
  lv_obj_t* label = lv_label_create(result);
  lv_label_set_text(label, text);
  lv_obj_set_style_text_font(label, UI_FONT_14, 0);
  lv_obj_set_style_text_color(label, lv_color_hex(kText), 0);
  lv_obj_center(label);
  if (callback != nullptr) {
    lv_obj_add_event_cb(result, callback, LV_EVENT_CLICKED, nullptr);
  }
  return result;
}
}  // namespace

void BlePairingScreen::create(lv_event_cb_t onBack, lv_event_cb_t onRetry) {
  if (screen_ != nullptr) return;
  screen_ = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(screen_, lv_color_hex(kBg), 0);
  lv_obj_set_style_text_color(screen_, lv_color_hex(kText), 0);
  lv_obj_clear_flag(screen_, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* back = button(screen_, "<", onBack);
  lv_obj_set_size(back, 34, 30);
  lv_obj_align(back, LV_ALIGN_TOP_LEFT, 40, 36);

  title_ = lv_label_create(screen_);
  lv_label_set_long_mode(title_, LV_LABEL_LONG_DOT);
  lv_obj_set_width(title_, 126);
  lv_obj_set_style_text_align(title_, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(title_, UI_FONT_20, 0);
  lv_obj_align(title_, LV_ALIGN_TOP_MID, 20, 39);

  spinner_ = lv_spinner_create(screen_, 900, 75);
  lv_obj_set_size(spinner_, 68, 68);
  lv_obj_align(spinner_, LV_ALIGN_CENTER, 0, -22);
  lv_obj_set_style_arc_color(spinner_, lv_color_hex(kPanelRaised), LV_PART_MAIN);
  lv_obj_set_style_arc_width(spinner_, 7, LV_PART_MAIN);
  lv_obj_set_style_arc_color(spinner_, lv_color_hex(kAccent), LV_PART_INDICATOR);
  lv_obj_set_style_arc_width(spinner_, 7, LV_PART_INDICATOR);

  mark_ = lv_label_create(screen_);
  lv_label_set_text(mark_, "BT");
  lv_obj_set_style_text_font(mark_, UI_FONT_16, 0);
  lv_obj_align_to(mark_, spinner_, LV_ALIGN_CENTER, 0, 0);

  status_ = lv_label_create(screen_);
  lv_obj_set_width(status_, 180);
  lv_obj_set_style_text_align(status_, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(status_, UI_FONT_16, 0);
  lv_obj_set_style_text_color(status_, lv_color_hex(kAccent), 0);
  lv_obj_align(status_, LV_ALIGN_CENTER, 0, 27);

  detail_ = lv_label_create(screen_);
  lv_label_set_long_mode(detail_, LV_LABEL_LONG_DOT);
  lv_obj_set_width(detail_, 172);
  lv_obj_set_style_text_align(detail_, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(detail_, UI_FONT_14, 0);
  lv_obj_set_style_text_color(detail_, lv_color_hex(kMuted), 0);
  lv_obj_align(detail_, LV_ALIGN_CENTER, 0, 52);

  retry_ = button(screen_, "Retry", onRetry);
  retryLabel_ = lv_obj_get_child(retry_, 0);
  lv_obj_set_size(retry_, 116, 34);
  lv_obj_align(retry_, LV_ALIGN_BOTTOM_MID, 0, -16);
}

void BlePairingScreen::destroy() {
  if (screen_ != nullptr) lv_obj_del(screen_);
  screen_ = title_ = spinner_ = mark_ = status_ = detail_ = retry_ =
      retryLabel_ = nullptr;
}

void BlePairingScreen::setTitle(const char* title) {
  if (title_ != nullptr) lv_label_set_text(title_, title != nullptr ? title : "Bluetooth");
}

void BlePairingScreen::setStatus(const char* status, const char* detail,
                                 bool busy, bool retryVisible,
                                 const char* retryLabel) {
  if (screen_ == nullptr) return;
  lv_label_set_text(status_, status != nullptr ? status : "Scanning");
  lv_label_set_text(detail_, detail != nullptr ? detail : "");
  if (busy) {
    lv_obj_clear_flag(spinner_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(mark_, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(spinner_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(mark_, LV_OBJ_FLAG_HIDDEN);
  }
  if (retryVisible) lv_obj_clear_flag(retry_, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(retry_, LV_OBJ_FLAG_HIDDEN);
  lv_label_set_text(retryLabel_, retryLabel != nullptr ? retryLabel : "Retry");
}

}  // namespace studio_ui
