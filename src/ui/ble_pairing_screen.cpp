#include "ui/ble_pairing_screen.h"

#include <cstdio>
#include <cstring>

#include "fonts/ui_fonts.h"
#include "ui/round_page.h"

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

  RoundPageHeaderOptions headerOptions;
  headerOptions.title = "Bluetooth";
  headerOptions.onBack = onBack;
  headerOptions.panelColor = kPanel;
  headerOptions.textColor = kText;
  title_ = createRoundPageHeader(screen_, headerOptions).title;

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

  candidateList_ = lv_obj_create(screen_);
  lv_obj_set_size(candidateList_, 178, 134);
  lv_obj_align(candidateList_, LV_ALIGN_TOP_MID, 0, 52);
  lv_obj_set_style_bg_opa(candidateList_, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(candidateList_, 0, 0);
  lv_obj_set_style_pad_all(candidateList_, 2, 0);
  lv_obj_set_style_pad_row(candidateList_, 5, 0);
  lv_obj_set_flex_flow(candidateList_, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scroll_dir(candidateList_, LV_DIR_VER);
  lv_obj_add_flag(candidateList_, LV_OBJ_FLAG_HIDDEN);
  for (size_t i = 0; i < 4; ++i) {
    candidateRows_[i] = button(candidateList_, "", nullptr);
    lv_obj_set_size(candidateRows_[i], 170, 43);
    lv_obj_set_flex_grow(candidateRows_[i], 0);
    lv_obj_add_event_cb(candidateRows_[i], onCandidateClicked,
                        LV_EVENT_CLICKED, this);
    candidateLabels_[i] = lv_obj_get_child(candidateRows_[i], 0);
    lv_label_set_long_mode(candidateLabels_[i], LV_LABEL_LONG_DOT);
    lv_obj_set_width(candidateLabels_[i], 154);
    lv_obj_set_style_text_align(candidateLabels_[i], LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_add_flag(candidateRows_[i], LV_OBJ_FLAG_HIDDEN);
  }
}

void BlePairingScreen::destroy() {
  if (screen_ != nullptr) lv_obj_del(screen_);
  screen_ = title_ = spinner_ = mark_ = status_ = detail_ = retry_ =
      retryLabel_ = candidateList_ = nullptr;
  for (size_t i = 0; i < 4; ++i) {
    candidateRows_[i] = candidateLabels_[i] = nullptr;
  }
  candidateCount_ = 0;
  onSelect_ = nullptr;
}

void BlePairingScreen::setCandidates(
    const studio::OnboardingCandidate* candidates, size_t count,
    void (*onSelect)(uint32_t)) {
  if (candidateList_ == nullptr) return;
  if (count > 4) count = 4;
  candidateCount_ = count;
  onSelect_ = onSelect;
  for (size_t i = 0; i < 4; ++i) {
    if (i >= count || candidates == nullptr) {
      lv_obj_add_flag(candidateRows_[i], LV_OBJ_FLAG_HIDDEN);
      continue;
    }
    char suffix[6] = "";
    const size_t addressLength = std::strlen(candidates[i].address);
    const char* tail = addressLength > 5
                           ? candidates[i].address + addressLength - 5
                           : candidates[i].address;
    std::snprintf(suffix, sizeof(suffix), "%s", tail);
    char label[72];
    std::snprintf(label, sizeof(label), "%s  %s\n%d dBm",
                  candidates[i].name[0] != '\0' ? candidates[i].name
                                                : "Compatible light",
                  suffix, static_cast<int>(candidates[i].rssi));
    if (std::strcmp(lv_label_get_text(candidateLabels_[i]), label) != 0) {
      lv_label_set_text(candidateLabels_[i], label);
    }
    lv_obj_set_user_data(
        candidateRows_[i],
        reinterpret_cast<void*>(static_cast<uintptr_t>(candidates[i].token)));
    lv_obj_clear_flag(candidateRows_[i], LV_OBJ_FLAG_HIDDEN);
  }
  if (count == 0) {
    lv_obj_add_flag(candidateList_, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_clear_flag(candidateList_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(spinner_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(mark_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(status_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(detail_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(retry_, LV_OBJ_FLAG_HIDDEN);
  }
}

void BlePairingScreen::onCandidateClicked(lv_event_t* event) {
  auto* self = static_cast<BlePairingScreen*>(lv_event_get_user_data(event));
  if (self == nullptr || self->onSelect_ == nullptr) return;
  const uint32_t token = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(
      lv_obj_get_user_data(lv_event_get_target(event))));
  if (token != 0) self->onSelect_(token);
}

#ifdef UI_SIMULATOR
size_t BlePairingScreen::simCandidateRowCount() const { return candidateCount_; }
lv_obj_t* BlePairingScreen::simCandidateRow(size_t index) const {
  return index < candidateCount_ ? candidateRows_[index] : nullptr;
}
void BlePairingScreen::simScrollCandidates(int16_t delta) {
  if (candidateList_ != nullptr) lv_obj_scroll_by(candidateList_, 0, delta, LV_ANIM_OFF);
}
int32_t BlePairingScreen::simCandidateScrollY() const {
  return candidateList_ != nullptr ? lv_obj_get_scroll_y(candidateList_) : 0;
}
void BlePairingScreen::simClickCandidate(size_t index) {
  if (index < candidateCount_) lv_event_send(candidateRows_[index], LV_EVENT_CLICKED, nullptr);
}
#endif

void BlePairingScreen::setTitle(const char* title) {
  if (title_ != nullptr) {
    lv_label_set_text(title_, title != nullptr ? title : "Bluetooth");
  }
}

void BlePairingScreen::setStatus(const char* status, const char* detail,
                                 bool busy, bool retryVisible,
                                 const char* retryLabel) {
  if (screen_ == nullptr) return;
  lv_obj_clear_flag(status_, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(detail_, LV_OBJ_FLAG_HIDDEN);
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
