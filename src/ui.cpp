#include "ui.h"

#include <Arduino.h>
#include <lvgl.h>

#include <cstdio>

#include "fonts/ui_fonts.h"
#include "shark_client.h"
#include "shark_protocol.h"

namespace ui {

namespace {

using shark::gShark;
using Link = shark::SharkClient::Link;

constexpr uint32_t kRefreshIntervalMs = 150;
const char* const kSlotLetters[shark::kKeypointCount] = {"A", "B", "C", "D", "E", "F", "G", "H"};

// Palette.
constexpr uint32_t kColBg = 0x05070A;
constexpr uint32_t kColPanel = 0x12161D;
constexpr uint32_t kColAccent = 0x35C7F2;
constexpr uint32_t kColAccentDim = 0x1E5A6B;
constexpr uint32_t kColText = 0xF3F4F6;
constexpr uint32_t kColMuted = 0x8A94A6;
constexpr uint32_t kColPresent = 0x4ADE80;
constexpr uint32_t kColAbsent = 0x3A3F4A;
constexpr uint32_t kColDanger = 0xF26D6D;

// Screens.
lv_obj_t* scrConnect = nullptr;
lv_obj_t* scrKeys = nullptr;
lv_obj_t* scrRun = nullptr;

// Splash / connect screen widgets.
lv_obj_t* connTitle = nullptr;
lv_obj_t* connArc = nullptr;
lv_obj_t* connMark = nullptr;
lv_obj_t* connStatus = nullptr;
lv_obj_t* connDevice = nullptr;

// Keypoints screen widgets.
lv_obj_t* keysHeader = nullptr;
lv_obj_t* keyButtons[shark::kKeypointCount] = {nullptr};
lv_obj_t* keyLabels[shark::kKeypointCount] = {nullptr};
lv_obj_t* gearButtons[shark::kKeypointCount] = {nullptr};
lv_obj_t* keyList = nullptr;

// Run screen widgets.
lv_obj_t* runHeader = nullptr;
lv_obj_t* runBar = nullptr;
lv_obj_t* runPercent = nullptr;
lv_obj_t* runActionBtn = nullptr;
lv_obj_t* runActionLabel = nullptr;
lv_obj_t* loopBtn = nullptr;
lv_obj_t* dirBtn = nullptr;

// Per-keypoint modal (lives on the top layer so it overlays any screen).
lv_obj_t* modal = nullptr;
lv_obj_t* modalTitle = nullptr;
lv_obj_t* modalPresence = nullptr;
lv_obj_t* speedRow = nullptr;
lv_obj_t* speedSlider = nullptr;
lv_obj_t* speedValue = nullptr;
lv_obj_t* holdRow = nullptr;
lv_obj_t* holdValue = nullptr;

int currentMain = 0;  // 0 = keypoints, 1 = run
int modalSlot = -1;
int modalHold = 0;
bool modalOpen = false;

// Slot whose "set" is being armed: manual tracking is on and the next tap on
// the same row commits the keypoint. -1 = nothing armed.
int armedSetSlot = -1;

// Timestamp of the last handled swipe. A horizontal swipe over a full-width
// row can still raise a button CLICKED on release, so taps that land within
// this window are ignored.
uint32_t lastSwipeMs = 0;
constexpr uint32_t kSwipeClickGuardMs = 350;

uint32_t lastRefreshMs = 0;
Link lastLinkShown = Link::Disconnected;

void styleScreen(lv_obj_t* scr) {
  lv_obj_set_style_bg_color(scr, lv_color_hex(kColBg), 0);
  lv_obj_set_style_text_color(scr, lv_color_hex(kColText), 0);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
}

lv_obj_t* makeButton(lv_obj_t* parent, const char* text, lv_event_cb_t cb, void* userData,
                     uint32_t color, const lv_font_t* font = UI_FONT_16) {
  lv_obj_t* btn = lv_btn_create(parent);
  lv_obj_set_style_bg_color(btn, lv_color_hex(color), 0);
  lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(btn, 8, 0);
  lv_obj_set_style_shadow_width(btn, 0, 0);
  lv_obj_set_style_pad_hor(btn, 6, 0);
  lv_obj_t* label = lv_label_create(btn);
  lv_label_set_text(label, text);
  lv_obj_set_style_text_font(label, font, 0);
  lv_obj_set_style_text_color(label, lv_color_hex(kColText), 0);
  lv_obj_center(label);
  if (cb != nullptr) {
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, userData);
  }
  return btn;
}

// ---- event handlers -------------------------------------------------------

// Drop any armed "set": turn manual tracking back off and clear the slot.
void cancelArmedSet() {
  if (armedSetSlot >= 0) {
    gShark.setManualTracking(false);
    armedSetSlot = -1;
  }
}

// Horizontal swipes move between the Keypoints and Run views. Swipe right on
// the keypoint list reveals the large run controls; swipe left returns.
void onScreenGesture(lv_event_t* e) {
  if (!gShark.connected()) {
    return;
  }
  const lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
  lv_obj_t* scr = lv_event_get_current_target(e);
  if (scr == scrKeys && dir == LV_DIR_RIGHT) {
    cancelArmedSet();
    currentMain = 1;
    lastSwipeMs = millis();
    lv_scr_load_anim(scrRun, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 200, 0, false);
  } else if (scr == scrRun && dir == LV_DIR_LEFT) {
    currentMain = 0;
    lastSwipeMs = millis();
    lv_scr_load_anim(scrKeys, LV_SCR_LOAD_ANIM_MOVE_LEFT, 200, 0, false);
  }
}

void onLoopToggle(lv_event_t* e) {
  lv_obj_t* btn = lv_event_get_target(e);
  gShark.setLoop(lv_obj_has_state(btn, LV_STATE_CHECKED));
}

void onDirToggle(lv_event_t* e) {
  lv_obj_t* btn = lv_event_get_target(e);
  gShark.setDirection(lv_obj_has_state(btn, LV_STATE_CHECKED));
}

// Single run button: advance the run state machine
// stopped -> standby -> running -> (stop) stopped.
void onRunAction(lv_event_t*) {
  const uint8_t code = gShark.state().runStateCode;
  if (code == shark::kRunStart || code == 0x06 /* preview */) {
    gShark.setRunState(shark::kRunStop);
  } else if (code == shark::kRunStandby) {
    gShark.setRunState(shark::kRunStart);
  } else {
    gShark.setRunState(shark::kRunStandby);
  }
}

void onRepair(lv_event_t*) { gShark.forgetDevice(); }

void closeModal() {
  modalOpen = false;
  modalSlot = -1;
  if (modal != nullptr) {
    lv_obj_add_flag(modal, LV_OBJ_FLAG_HIDDEN);
  }
}

void openModal(int slot);

// Primary row tap. Going to a set keypoint is the fast default; an unset slot
// arms a hand-positioned "set" that commits on the next tap of the same row.
void onKeyMain(lv_event_t* e) {
  if (millis() - lastSwipeMs < kSwipeClickGuardMs) {
    return;  // release after a swipe, not a real tap
  }
  const int slot = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
  if (slot < 0 || slot >= shark::kKeypointCount) {
    return;
  }
  const shark::SharkClient::State& s = gShark.state();
  if (s.present[slot]) {
    cancelArmedSet();
    gShark.keypointGo(slot);
  } else if (armedSetSlot == slot) {
    // Commit at the current hand-tracked position, then drop tracking.
    gShark.keypointSet(slot);
    gShark.setManualTracking(false);
    armedSetSlot = -1;
  } else {
    cancelArmedSet();
    armedSetSlot = slot;
    gShark.setManualTracking(true);
  }
}

void onKeyGear(lv_event_t* e) {
  if (millis() - lastSwipeMs < kSwipeClickGuardMs) {
    return;  // release after a swipe, not a real tap
  }
  const int slot = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
  openModal(slot);
}

void onModalClose(lv_event_t*) { closeModal(); }

void onModalDelete(lv_event_t*) {
  if (modalSlot >= 0) {
    gShark.keypointDelete(modalSlot);
  }
  closeModal();
}

void onSpeedChanged(lv_event_t* e) {
  lv_obj_t* slider = lv_event_get_target(e);
  const int value = lv_slider_get_value(slider);
  if (speedValue != nullptr) {
    char buf[8];
    snprintf(buf, sizeof(buf), "%d%%", value);
    lv_label_set_text(speedValue, buf);
  }
}

void onSpeedReleased(lv_event_t* e) {
  lv_obj_t* slider = lv_event_get_target(e);
  if (modalSlot > 0) {
    gShark.setSpeed(modalSlot, lv_slider_get_value(slider));
  }
}

void updateHoldLabel() {
  if (holdValue != nullptr) {
    char buf[12];
    snprintf(buf, sizeof(buf), "%d s", modalHold);
    lv_label_set_text(holdValue, buf);
  }
}

void onHoldMinus(lv_event_t*) {
  if (modalHold > 0) {
    modalHold--;
  }
  updateHoldLabel();
  if (modalSlot > 0) {
    gShark.setHold(modalSlot, modalHold);
  }
}

void onHoldPlus(lv_event_t*) {
  if (modalHold < 60) {
    modalHold++;
  }
  updateHoldLabel();
  if (modalSlot > 0) {
    gShark.setHold(modalSlot, modalHold);
  }
}

// ---- screen construction --------------------------------------------------

void buildConnectScreen() {
  scrConnect = lv_obj_create(nullptr);
  styleScreen(scrConnect);

  connTitle = lv_label_create(scrConnect);
  lv_label_set_text(connTitle, "Shark Remote");
  lv_obj_set_style_text_font(connTitle, UI_FONT_20, 0);
  lv_obj_align(connTitle, LV_ALIGN_TOP_MID, 0, 42);

  connArc = lv_arc_create(scrConnect);
  lv_obj_set_size(connArc, 74, 74);
  lv_obj_align(connArc, LV_ALIGN_CENTER, 0, -16);
  lv_arc_set_range(connArc, 0, 100);
  lv_arc_set_bg_angles(connArc, 0, 360);
  lv_arc_set_value(connArc, 72);
  lv_obj_clear_flag(connArc, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_arc_color(connArc, lv_color_hex(kColAccentDim), LV_PART_MAIN);
  lv_obj_set_style_arc_width(connArc, 7, LV_PART_MAIN);
  lv_obj_set_style_arc_color(connArc, lv_color_hex(kColAccent), LV_PART_INDICATOR);
  lv_obj_set_style_arc_width(connArc, 7, LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(connArc, LV_OPA_TRANSP, LV_PART_KNOB);

  connMark = lv_label_create(scrConnect);
  lv_label_set_text(connMark, "BLE");
  lv_obj_set_style_text_font(connMark, UI_FONT_16, 0);
  lv_obj_set_style_text_color(connMark, lv_color_hex(kColText), 0);
  lv_obj_align_to(connMark, connArc, LV_ALIGN_CENTER, 0, 0);

  connStatus = lv_label_create(scrConnect);
  lv_label_set_text(connStatus, "Starting Bluetooth...");
  lv_obj_set_style_text_font(connStatus, UI_FONT_16, 0);
  lv_obj_set_style_text_color(connStatus, lv_color_hex(kColAccent), 0);
  lv_obj_align(connStatus, LV_ALIGN_CENTER, 0, 40);

  connDevice = lv_label_create(scrConnect);
  lv_label_set_long_mode(connDevice, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(connDevice, 180);
  lv_obj_set_style_text_align(connDevice, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(connDevice, lv_color_hex(kColMuted), 0);
  lv_label_set_text(connDevice, "iFootage Shark Nano II");
  lv_obj_align(connDevice, LV_ALIGN_CENTER, 0, 66);

  lv_obj_t* repair = makeButton(scrConnect, "Re-pair", onRepair, nullptr, kColPanel);
  lv_obj_set_size(repair, 120, 36);
  lv_obj_align(repair, LV_ALIGN_BOTTOM_MID, 0, -18);
}

void buildKeysScreen() {
  scrKeys = lv_obj_create(nullptr);
  styleScreen(scrKeys);
  lv_obj_add_event_cb(scrKeys, onScreenGesture, LV_EVENT_GESTURE, nullptr);

  keysHeader = lv_label_create(scrKeys);
  lv_obj_set_style_text_font(keysHeader, UI_FONT_14, 0);
  lv_label_set_text(keysHeader, "Keypoints");
  lv_obj_align(keysHeader, LV_ALIGN_TOP_MID, 0, 18);

  // Scrollable keypoint list. With the manual-track switch gone it can use the
  // full mid-band height; horizontal swipes bubble up to the screen gesture
  // handler so the Run view is reachable.
  keyList = lv_obj_create(scrKeys);
  lv_obj_set_size(keyList, 200, 168);
  lv_obj_align(keyList, LV_ALIGN_TOP_MID, 0, 40);
  lv_obj_set_style_bg_color(keyList, lv_color_hex(kColBg), 0);
  lv_obj_set_style_border_width(keyList, 0, 0);
  lv_obj_set_style_pad_all(keyList, 2, 0);
  lv_obj_set_style_pad_row(keyList, 4, 0);
  lv_obj_set_flex_flow(keyList, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scroll_dir(keyList, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(keyList, LV_SCROLLBAR_MODE_OFF);
  lv_obj_add_flag(keyList, LV_OBJ_FLAG_EVENT_BUBBLE);

  for (int i = 0; i < shark::kKeypointCount; ++i) {
    void* slotData = reinterpret_cast<void*>(static_cast<intptr_t>(i));

    lv_obj_t* row = lv_obj_create(keyList);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, 32);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_pad_column(row, 4, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(row, LV_OBJ_FLAG_EVENT_BUBBLE);

    lv_obj_t* btn = lv_btn_create(row);
    lv_obj_set_height(btn, lv_pct(100));
    lv_obj_set_flex_grow(btn, 1);
    lv_obj_set_style_bg_color(btn, lv_color_hex(kColPanel), 0);
    lv_obj_set_style_radius(btn, 6, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_pad_left(btn, 10, 0);
    lv_obj_add_event_cb(btn, onKeyMain, LV_EVENT_CLICKED, slotData);
    lv_obj_t* label = lv_label_create(btn);
    lv_obj_set_style_text_font(label, UI_FONT_14, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(kColText), 0);
    lv_obj_align(label, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t* gear = lv_btn_create(row);
    lv_obj_set_size(gear, 34, 30);
    lv_obj_set_style_bg_color(gear, lv_color_hex(kColPanel), 0);
    lv_obj_set_style_radius(gear, 6, 0);
    lv_obj_set_style_shadow_width(gear, 0, 0);
    lv_obj_set_style_pad_all(gear, 0, 0);
    lv_obj_add_event_cb(gear, onKeyGear, LV_EVENT_CLICKED, slotData);
    lv_obj_t* gearLabel = lv_label_create(gear);
    lv_label_set_text(gearLabel, LV_SYMBOL_SETTINGS);
    lv_obj_set_style_text_color(gearLabel, lv_color_hex(kColMuted), 0);
    lv_obj_center(gearLabel);

    keyButtons[i] = btn;
    keyLabels[i] = label;
    gearButtons[i] = gear;
  }
}

void buildRunScreen() {
  scrRun = lv_obj_create(nullptr);
  styleScreen(scrRun);
  lv_obj_add_event_cb(scrRun, onScreenGesture, LV_EVENT_GESTURE, nullptr);

  runHeader = lv_label_create(scrRun);
  lv_obj_set_style_text_font(runHeader, UI_FONT_14, 0);
  lv_label_set_text(runHeader, "Run");
  lv_obj_align(runHeader, LV_ALIGN_TOP_MID, 0, 18);

  runBar = lv_bar_create(scrRun);
  lv_obj_set_size(runBar, 150, 10);
  lv_obj_align(runBar, LV_ALIGN_TOP_MID, 0, 44);
  lv_bar_set_range(runBar, 0, 100);
  lv_bar_set_value(runBar, 0, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(runBar, lv_color_hex(kColPanel), LV_PART_MAIN);
  lv_obj_set_style_bg_color(runBar, lv_color_hex(kColAccent), LV_PART_INDICATOR);

  runPercent = lv_label_create(scrRun);
  lv_obj_set_style_text_color(runPercent, lv_color_hex(kColMuted), 0);
  lv_label_set_text(runPercent, "0%");
  lv_obj_align(runPercent, LV_ALIGN_TOP_MID, 0, 58);

  // Single large run-state button. Its label/color and action follow the run
  // state machine (stopped -> Standby -> Start -> Stop); only one shows at a
  // time so it stays big and unambiguous.
  runActionBtn = makeButton(scrRun, "Standby", onRunAction, nullptr, kColPanel, UI_FONT_20);
  lv_obj_set_size(runActionBtn, 132, 60);
  lv_obj_align(runActionBtn, LV_ALIGN_CENTER, 0, -6);
  lv_obj_set_style_radius(runActionBtn, 12, 0);
  runActionLabel = lv_obj_get_child(runActionBtn, 0);

  // Loop / reverse toggles: icon-only checkable buttons in one bottom row.
  loopBtn = lv_btn_create(scrRun);
  lv_obj_add_flag(loopBtn, LV_OBJ_FLAG_CHECKABLE);
  lv_obj_set_size(loopBtn, 46, 34);
  lv_obj_align(loopBtn, LV_ALIGN_BOTTOM_MID, -28, -14);
  lv_obj_set_style_bg_color(loopBtn, lv_color_hex(kColPanel), 0);
  lv_obj_set_style_bg_color(loopBtn, lv_color_hex(kColAccentDim), LV_STATE_CHECKED);
  lv_obj_set_style_radius(loopBtn, 8, 0);
  lv_obj_set_style_shadow_width(loopBtn, 0, 0);
  lv_obj_add_event_cb(loopBtn, onLoopToggle, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* loopIcon = lv_label_create(loopBtn);
  lv_label_set_text(loopIcon, LV_SYMBOL_LOOP);
  lv_obj_set_style_text_color(loopIcon, lv_color_hex(kColText), 0);
  lv_obj_center(loopIcon);

  dirBtn = lv_btn_create(scrRun);
  lv_obj_add_flag(dirBtn, LV_OBJ_FLAG_CHECKABLE);
  lv_obj_set_size(dirBtn, 46, 34);
  lv_obj_align(dirBtn, LV_ALIGN_BOTTOM_MID, 28, -14);
  lv_obj_set_style_bg_color(dirBtn, lv_color_hex(kColPanel), 0);
  lv_obj_set_style_bg_color(dirBtn, lv_color_hex(kColAccentDim), LV_STATE_CHECKED);
  lv_obj_set_style_radius(dirBtn, 8, 0);
  lv_obj_set_style_shadow_width(dirBtn, 0, 0);
  lv_obj_add_event_cb(dirBtn, onDirToggle, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* dirIcon = lv_label_create(dirBtn);
  lv_label_set_text(dirIcon, LV_SYMBOL_LEFT);
  lv_obj_set_style_text_color(dirIcon, lv_color_hex(kColText), 0);
  lv_obj_center(dirIcon);
}

void buildModal() {
  // Full-screen settings panel. It fills the round display (inset a few pixels
  // so the accent border reads as a ring just inside the bezel) and overlays
  // whatever screen is active.
  modal = lv_obj_create(lv_layer_top());
  lv_obj_set_size(modal, 236, 236);
  lv_obj_center(modal);
  lv_obj_set_style_radius(modal, 118, 0);
  lv_obj_set_style_bg_color(modal, lv_color_hex(kColPanel), 0);
  lv_obj_set_style_border_color(modal, lv_color_hex(kColAccent), 0);
  lv_obj_set_style_border_width(modal, 3, 0);
  lv_obj_set_style_pad_all(modal, 0, 0);
  lv_obj_clear_flag(modal, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(modal, LV_OBJ_FLAG_HIDDEN);

  // Fixed footer: Delete / Close stay pinned at the bottom of the panel.
  lv_obj_t* btnRow = lv_obj_create(modal);
  lv_obj_set_size(btnRow, 176, 40);
  lv_obj_align(btnRow, LV_ALIGN_BOTTOM_MID, 0, -16);
  lv_obj_set_style_bg_opa(btnRow, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(btnRow, 0, 0);
  lv_obj_set_style_pad_all(btnRow, 0, 0);
  lv_obj_clear_flag(btnRow, LV_OBJ_FLAG_SCROLLABLE);

  // Close is the primary action (accent). Delete is intentionally subdued so it
  // doesn't read as the default CTA.
  lv_obj_t* closeBtn = makeButton(btnRow, "Close", onModalClose, nullptr, kColAccent);
  lv_obj_set_size(closeBtn, 104, 36);
  lv_obj_align(closeBtn, LV_ALIGN_RIGHT_MID, -2, 0);

  lv_obj_t* delBtn = makeButton(btnRow, "Delete", onModalDelete, nullptr, kColAbsent, UI_FONT_14);
  lv_obj_set_size(delBtn, 58, 32);
  lv_obj_align(delBtn, LV_ALIGN_LEFT_MID, 2, 0);
  lv_obj_set_style_text_color(lv_obj_get_child(delBtn, 0), lv_color_hex(kColMuted), 0);

  // Scrollable content above the fixed footer. Title, presence, speed, and hold
  // flow vertically and can be scrolled into the wide center of the round panel.
  lv_obj_t* content = lv_obj_create(modal);
  lv_obj_set_size(content, 232, 178);
  lv_obj_align(content, LV_ALIGN_TOP_MID, 0, 2);
  lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(content, 0, 0);
  lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_top(content, 52, 0);
  lv_obj_set_style_pad_bottom(content, 20, 0);
  lv_obj_set_style_pad_left(content, 0, 0);
  lv_obj_set_style_pad_right(content, 0, 0);
  lv_obj_set_style_pad_row(content, 12, 0);
  lv_obj_set_scroll_dir(content, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(content, LV_SCROLLBAR_MODE_AUTO);

  modalTitle = lv_label_create(content);
  lv_obj_set_style_text_font(modalTitle, UI_FONT_20, 0);
  lv_label_set_text(modalTitle, "A Settings");

  modalPresence = lv_label_create(content);
  lv_obj_set_style_text_color(modalPresence, lv_color_hex(kColMuted), 0);
  lv_label_set_text(modalPresence, "not set");

  // Speed row (B..H only).
  speedRow = lv_obj_create(content);
  lv_obj_set_size(speedRow, 176, 44);
  lv_obj_set_style_bg_opa(speedRow, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(speedRow, 0, 0);
  lv_obj_set_style_pad_all(speedRow, 0, 0);
  lv_obj_clear_flag(speedRow, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* speedCaption = lv_label_create(speedRow);
  lv_label_set_text(speedCaption, "Speed");
  lv_obj_set_style_text_color(speedCaption, lv_color_hex(kColMuted), 0);
  lv_obj_align(speedCaption, LV_ALIGN_TOP_LEFT, 0, 0);

  speedValue = lv_label_create(speedRow);
  lv_label_set_text(speedValue, "--");
  lv_obj_align(speedValue, LV_ALIGN_TOP_RIGHT, 0, 0);

  speedSlider = lv_slider_create(speedRow);
  lv_obj_set_size(speedSlider, 176, 12);
  lv_obj_align(speedSlider, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_slider_set_range(speedSlider, 0, 100);
  lv_obj_set_style_bg_color(speedSlider, lv_color_hex(kColAbsent), LV_PART_MAIN);
  lv_obj_set_style_bg_color(speedSlider, lv_color_hex(kColAccent), LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(speedSlider, lv_color_hex(kColAccent), LV_PART_KNOB);
  lv_obj_add_event_cb(speedSlider, onSpeedChanged, LV_EVENT_VALUE_CHANGED, nullptr);
  lv_obj_add_event_cb(speedSlider, onSpeedReleased, LV_EVENT_RELEASED, nullptr);

  // Hold row (B..H only).
  holdRow = lv_obj_create(content);
  lv_obj_set_size(holdRow, 176, 34);
  lv_obj_set_style_bg_opa(holdRow, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(holdRow, 0, 0);
  lv_obj_set_style_pad_all(holdRow, 0, 0);
  lv_obj_clear_flag(holdRow, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* holdCaption = lv_label_create(holdRow);
  lv_label_set_text(holdCaption, "Hold");
  lv_obj_set_style_text_color(holdCaption, lv_color_hex(kColMuted), 0);
  lv_obj_align(holdCaption, LV_ALIGN_LEFT_MID, 0, 0);

  lv_obj_t* plusBtn = makeButton(holdRow, "+", onHoldPlus, nullptr, kColAccentDim);
  lv_obj_set_size(plusBtn, 30, 28);
  lv_obj_align(plusBtn, LV_ALIGN_RIGHT_MID, 0, 0);

  holdValue = lv_label_create(holdRow);
  lv_obj_set_width(holdValue, 44);
  lv_obj_set_style_text_align(holdValue, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_text(holdValue, "0 s");
  lv_obj_align(holdValue, LV_ALIGN_RIGHT_MID, -46, 0);

  lv_obj_t* minusBtn = makeButton(holdRow, "-", onHoldMinus, nullptr, kColAccentDim);
  lv_obj_set_size(minusBtn, 30, 28);
  lv_obj_align(minusBtn, LV_ALIGN_RIGHT_MID, -92, 0);
}

void openModal(int slot) {
  if (slot < 0 || slot >= shark::kKeypointCount) {
    return;
  }
  modalSlot = slot;
  modalOpen = true;

  char title[16];
  snprintf(title, sizeof(title), "%s Settings", kSlotLetters[slot]);
  lv_label_set_text(modalTitle, title);

  const shark::SharkClient::State& s = gShark.state();
  lv_label_set_text(modalPresence, s.present[slot] ? "configured" : "not set");

  const bool hasTiming = slot > 0;  // A has no travel segment
  if (hasTiming) {
    lv_obj_clear_flag(speedRow, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(holdRow, LV_OBJ_FLAG_HIDDEN);
    const int speed = (s.speed[slot] >= 0) ? s.speed[slot] : 0;
    lv_slider_set_value(speedSlider, speed, LV_ANIM_OFF);
    char buf[8];
    snprintf(buf, sizeof(buf), "%d%%", speed);
    lv_label_set_text(speedValue, buf);
    modalHold = (s.hold[slot] >= 0) ? s.hold[slot] : 0;
    updateHoldLabel();
    // Make sure we have a current timing table for read-modify-write edits.
    gShark.requestTiming();
  } else {
    lv_obj_add_flag(speedRow, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(holdRow, LV_OBJ_FLAG_HIDDEN);
  }

  lv_obj_clear_flag(modal, LV_OBJ_FLAG_HIDDEN);
}

// ---- refresh --------------------------------------------------------------

void formatHeader(char* out, size_t cap, const shark::SharkClient::State& s, const char* title) {
  char battery[12];
  if (s.battery >= 0) {
    snprintf(battery, sizeof(battery), "%d%%", s.battery);
  } else {
    snprintf(battery, sizeof(battery), "--");
  }
  snprintf(out, cap, "%s  %s  %s", title, battery, s.runText);
}

void refreshConnect(const shark::SharkClient::State& s) {
  const char* status = "Scanning";
  switch (s.link) {
    case Link::Scanning:
      status = s.hasSavedDevice ? "Looking for slider" : "Scanning";
      break;
    case Link::Connecting:
      status = s.hasSavedDevice ? "Reconnecting" : "Connecting";
      break;
    case Link::Disconnected:
      status = s.hasSavedDevice ? "Reconnecting" : "Scanning";
      break;
    case Link::Connected:
      status = "Connected";
      break;
  }

  char statusText[32];
  if (s.link == Link::Connected) {
    snprintf(statusText, sizeof(statusText), "%s", status);
  } else {
    const int dots = static_cast<int>((millis() / 450) % 4);
    snprintf(statusText, sizeof(statusText), "%s%.*s", status, dots, "...");
  }
  lv_label_set_text(connStatus, statusText);
  lv_label_set_text(connDevice,
                    (s.hasSavedDevice && s.deviceName[0] != '\0') ? s.deviceName
                                                                  : "iFootage Shark Nano II");
}

void refreshKeys(const shark::SharkClient::State& s) {
  char header[40];
  formatHeader(header, sizeof(header), s, "Keys");
  lv_label_set_text(keysHeader, header);

  for (int i = 0; i < shark::kKeypointCount; ++i) {
    const bool present = s.present[i];
    const bool armed = (armedSetSlot == i);
    char text[40];
    if (armed) {
      snprintf(text, sizeof(text), "%s  Setting... tap to set", kSlotLetters[i]);
    } else if (i == 0) {
      snprintf(text, sizeof(text), "%s %s  start", kSlotLetters[i], present ? "#" : "-");
    } else if (present && s.timingKnown && s.speed[i] >= 0) {
      snprintf(text, sizeof(text), "%s #  %d%%  %ds", kSlotLetters[i], s.speed[i], s.hold[i]);
    } else {
      snprintf(text, sizeof(text), "%s %s", kSlotLetters[i], present ? "#" : "-");
    }
    lv_label_set_text(keyLabels[i], text);
    uint32_t bg = kColPanel;
    if (armed) {
      bg = kColAccent;
    } else if (present) {
      bg = kColAccentDim;
    }
    lv_obj_set_style_bg_color(keyButtons[i], lv_color_hex(bg), 0);
    lv_obj_set_style_bg_color(gearButtons[i], lv_color_hex(bg), 0);
  }
}

void refreshRun(const shark::SharkClient::State& s) {
  char header[40];
  formatHeader(header, sizeof(header), s, "Run");
  lv_label_set_text(runHeader, header);

  const int pct = s.runProgressKnown ? static_cast<int>(s.runPercent + 0.5f) : 0;
  lv_bar_set_value(runBar, pct, LV_ANIM_OFF);
  char buf[16];
  snprintf(buf, sizeof(buf), "%s  %d%%", s.runText, pct);
  lv_label_set_text(runPercent, buf);

  // Single button: show the action that the next tap will take.
  const char* action = "Standby";
  uint32_t actionColor = kColPanel;
  if (s.runStateCode == shark::kRunStart || s.runStateCode == 0x06) {
    action = "Stop";
    actionColor = kColDanger;
  } else if (s.runStateCode == shark::kRunStandby) {
    action = "Start";
    actionColor = kColAccent;
  }
  lv_label_set_text(runActionLabel, action);
  lv_obj_set_style_bg_color(runActionBtn, lv_color_hex(actionColor), 0);

  if (s.loopOn) {
    lv_obj_add_state(loopBtn, LV_STATE_CHECKED);
  } else {
    lv_obj_clear_state(loopBtn, LV_STATE_CHECKED);
  }
  if (s.reverse) {
    lv_obj_add_state(dirBtn, LV_STATE_CHECKED);
  } else {
    lv_obj_clear_state(dirBtn, LV_STATE_CHECKED);
  }
}

void refreshModal(const shark::SharkClient::State& s) {
  if (!modalOpen || modalSlot < 0) {
    return;
  }
  lv_label_set_text(modalPresence, s.present[modalSlot] ? "configured" : "not set");
}

void showScreenForState(const shark::SharkClient::State& s) {
  if (s.link == Link::Connected) {
    lv_obj_t* target = (currentMain == 1) ? scrRun : scrKeys;
    if (lv_scr_act() != target) {
      lv_scr_load(target);
    }
  } else {
    if (modalOpen) {
      closeModal();
    }
    armedSetSlot = -1;  // link dropped; nothing to commit
    if (lv_scr_act() != scrConnect) {
      lv_scr_load(scrConnect);
    }
  }
}

}  // namespace

void init() {
  buildConnectScreen();
  buildKeysScreen();
  buildRunScreen();
  buildModal();
  lv_scr_load(scrConnect);
}

void tick() {
  const uint32_t now = millis();
  if (now - lastRefreshMs < kRefreshIntervalMs) {
    return;
  }
  lastRefreshMs = now;

  const shark::SharkClient::State& s = gShark.state();
  showScreenForState(s);

  if (s.link == Link::Connected) {
    if (currentMain == 1) {
      refreshRun(s);
    } else {
      refreshKeys(s);
    }
    refreshModal(s);
  } else {
    refreshConnect(s);
  }
  lastLinkShown = s.link;
}

void toggleMainScreen() {
  if (!gShark.connected()) {
    return;
  }
  if (modalOpen) {
    closeModal();
    return;
  }
  cancelArmedSet();
  currentMain = (currentMain == 0) ? 1 : 0;
  if (currentMain == 1) {
    lv_scr_load_anim(scrRun, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 200, 0, false);
  } else {
    lv_scr_load_anim(scrKeys, LV_SCR_LOAD_ANIM_MOVE_LEFT, 200, 0, false);
  }
}

}  // namespace ui
