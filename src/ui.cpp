#include "ui.h"

#include <Arduino.h>
#include <lvgl.h>

#include <cstdio>

#include "fonts/roboto_fonts.h"
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

// Connect screen widgets.
lv_obj_t* connTitle = nullptr;
lv_obj_t* connStatus = nullptr;
lv_obj_t* connDevice = nullptr;

// Keypoints screen widgets.
lv_obj_t* keysHeader = nullptr;
lv_obj_t* trackSwitch = nullptr;
lv_obj_t* keyButtons[shark::kKeypointCount] = {nullptr};
lv_obj_t* keyLabels[shark::kKeypointCount] = {nullptr};

// Run screen widgets.
lv_obj_t* runHeader = nullptr;
lv_obj_t* runBar = nullptr;
lv_obj_t* runPercent = nullptr;
lv_obj_t* loopSwitch = nullptr;
lv_obj_t* dirSwitch = nullptr;

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

uint32_t lastRefreshMs = 0;
Link lastLinkShown = Link::Disconnected;

void styleScreen(lv_obj_t* scr) {
  lv_obj_set_style_bg_color(scr, lv_color_hex(kColBg), 0);
  lv_obj_set_style_text_color(scr, lv_color_hex(kColText), 0);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
}

lv_obj_t* makeButton(lv_obj_t* parent, const char* text, lv_event_cb_t cb, void* userData,
                     uint32_t color, const lv_font_t* font = &lv_font_roboto_16) {
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

void onTrackSwitch(lv_event_t* e) {
  lv_obj_t* sw = lv_event_get_target(e);
  gShark.setManualTracking(lv_obj_has_state(sw, LV_STATE_CHECKED));
}

void onLoopSwitch(lv_event_t* e) {
  lv_obj_t* sw = lv_event_get_target(e);
  gShark.setLoop(lv_obj_has_state(sw, LV_STATE_CHECKED));
}

void onDirSwitch(lv_event_t* e) {
  lv_obj_t* sw = lv_event_get_target(e);
  gShark.setDirection(lv_obj_has_state(sw, LV_STATE_CHECKED));
}

void onRunStandby(lv_event_t*) { gShark.setRunState(shark::kRunStandby); }
void onRunStart(lv_event_t*) { gShark.setRunState(shark::kRunStart); }
void onRunStop(lv_event_t*) { gShark.setRunState(shark::kRunStop); }

void onRepair(lv_event_t*) { gShark.forgetDevice(); }

void closeModal() {
  modalOpen = false;
  modalSlot = -1;
  if (modal != nullptr) {
    lv_obj_add_flag(modal, LV_OBJ_FLAG_HIDDEN);
  }
}

void openModal(int slot);

void onKeyButton(lv_event_t* e) {
  const int slot = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
  openModal(slot);
}

void onModalClose(lv_event_t*) { closeModal(); }

void onModalSet(lv_event_t*) {
  if (modalSlot >= 0) {
    gShark.keypointSet(modalSlot);
  }
}

void onModalGo(lv_event_t*) {
  if (modalSlot >= 0) {
    gShark.keypointGo(modalSlot);
  }
  closeModal();
}

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
  lv_obj_set_style_text_font(connTitle, &lv_font_roboto_20, 0);
  lv_obj_align(connTitle, LV_ALIGN_TOP_MID, 0, 48);

  connStatus = lv_label_create(scrConnect);
  lv_label_set_text(connStatus, "Scanning...");
  lv_obj_set_style_text_font(connStatus, &lv_font_roboto_16, 0);
  lv_obj_set_style_text_color(connStatus, lv_color_hex(kColAccent), 0);
  lv_obj_align(connStatus, LV_ALIGN_CENTER, 0, -6);

  connDevice = lv_label_create(scrConnect);
  lv_label_set_long_mode(connDevice, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(connDevice, 180);
  lv_obj_set_style_text_align(connDevice, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(connDevice, lv_color_hex(kColMuted), 0);
  lv_label_set_text(connDevice, "iFootage Shark Nano II");
  lv_obj_align(connDevice, LV_ALIGN_CENTER, 0, 24);

  lv_obj_t* repair = makeButton(scrConnect, "Re-pair", onRepair, nullptr, kColPanel);
  lv_obj_set_size(repair, 120, 36);
  lv_obj_align(repair, LV_ALIGN_BOTTOM_MID, 0, -34);
}

void buildKeysScreen() {
  scrKeys = lv_obj_create(nullptr);
  styleScreen(scrKeys);

  keysHeader = lv_label_create(scrKeys);
  lv_obj_set_style_text_font(keysHeader, &lv_font_roboto_14, 0);
  lv_label_set_text(keysHeader, "Keypoints");
  lv_obj_align(keysHeader, LV_ALIGN_TOP_MID, 0, 18);

  // Manual tracking row.
  lv_obj_t* trackLabel = lv_label_create(scrKeys);
  lv_label_set_text(trackLabel, "Manual track");
  lv_obj_set_style_text_color(trackLabel, lv_color_hex(kColMuted), 0);
  lv_obj_align(trackLabel, LV_ALIGN_TOP_MID, -28, 40);

  trackSwitch = lv_switch_create(scrKeys);
  lv_obj_set_size(trackSwitch, 44, 22);
  lv_obj_set_style_bg_color(trackSwitch, lv_color_hex(kColAccent), LV_PART_INDICATOR | LV_STATE_CHECKED);
  lv_obj_align(trackSwitch, LV_ALIGN_TOP_MID, 56, 38);
  lv_obj_add_event_cb(trackSwitch, onTrackSwitch, LV_EVENT_VALUE_CHANGED, nullptr);

  // Scrollable keypoint list.
  lv_obj_t* list = lv_obj_create(scrKeys);
  lv_obj_set_size(list, 188, 130);
  lv_obj_align(list, LV_ALIGN_TOP_MID, 0, 68);
  lv_obj_set_style_bg_color(list, lv_color_hex(kColBg), 0);
  lv_obj_set_style_border_width(list, 0, 0);
  lv_obj_set_style_pad_all(list, 2, 0);
  lv_obj_set_style_pad_row(list, 4, 0);
  lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scroll_dir(list, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);

  for (int i = 0; i < shark::kKeypointCount; ++i) {
    lv_obj_t* btn = lv_btn_create(list);
    lv_obj_set_width(btn, lv_pct(100));
    lv_obj_set_height(btn, 30);
    lv_obj_set_style_bg_color(btn, lv_color_hex(kColPanel), 0);
    lv_obj_set_style_radius(btn, 6, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_pad_left(btn, 10, 0);
    lv_obj_add_event_cb(btn, onKeyButton, LV_EVENT_CLICKED,
                        reinterpret_cast<void*>(static_cast<intptr_t>(i)));
    lv_obj_t* label = lv_label_create(btn);
    lv_obj_set_style_text_font(label, &lv_font_roboto_14, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(kColText), 0);
    lv_obj_align(label, LV_ALIGN_LEFT_MID, 0, 0);
    keyButtons[i] = btn;
    keyLabels[i] = label;
  }
}

void buildRunScreen() {
  scrRun = lv_obj_create(nullptr);
  styleScreen(scrRun);

  runHeader = lv_label_create(scrRun);
  lv_obj_set_style_text_font(runHeader, &lv_font_roboto_14, 0);
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

  // Run-state buttons. These are narrow, so use the 14px face and widen the
  // longest label ("Standby") enough that nothing clips.
  lv_obj_t* standby = makeButton(scrRun, "Standby", onRunStandby, nullptr, kColPanel,
                                 &lv_font_roboto_14);
  lv_obj_set_size(standby, 72, 36);
  lv_obj_align(standby, LV_ALIGN_CENTER, -60, -2);

  lv_obj_t* start = makeButton(scrRun, "Start", onRunStart, nullptr, kColAccentDim,
                               &lv_font_roboto_14);
  lv_obj_set_size(start, 52, 36);
  lv_obj_align(start, LV_ALIGN_CENTER, 4, -2);

  lv_obj_t* stop = makeButton(scrRun, "Stop", onRunStop, nullptr, kColDanger,
                              &lv_font_roboto_14);
  lv_obj_set_size(stop, 52, 36);
  lv_obj_align(stop, LV_ALIGN_CENTER, 62, -2);

  // Loop row.
  lv_obj_t* loopLabel = lv_label_create(scrRun);
  lv_label_set_text(loopLabel, "Loop");
  lv_obj_set_style_text_color(loopLabel, lv_color_hex(kColMuted), 0);
  lv_obj_align(loopLabel, LV_ALIGN_CENTER, -40, 42);
  loopSwitch = lv_switch_create(scrRun);
  lv_obj_set_size(loopSwitch, 44, 22);
  lv_obj_set_style_bg_color(loopSwitch, lv_color_hex(kColAccent), LV_PART_INDICATOR | LV_STATE_CHECKED);
  lv_obj_align(loopSwitch, LV_ALIGN_CENTER, 26, 42);
  lv_obj_add_event_cb(loopSwitch, onLoopSwitch, LV_EVENT_VALUE_CHANGED, nullptr);

  // Direction row.
  lv_obj_t* dirLabel = lv_label_create(scrRun);
  lv_label_set_text(dirLabel, "Reverse");
  lv_obj_set_style_text_color(dirLabel, lv_color_hex(kColMuted), 0);
  lv_obj_align(dirLabel, LV_ALIGN_CENTER, -40, 72);
  dirSwitch = lv_switch_create(scrRun);
  lv_obj_set_size(dirSwitch, 44, 22);
  lv_obj_set_style_bg_color(dirSwitch, lv_color_hex(kColAccent), LV_PART_INDICATOR | LV_STATE_CHECKED);
  lv_obj_align(dirSwitch, LV_ALIGN_CENTER, 26, 72);
  lv_obj_add_event_cb(dirSwitch, onDirSwitch, LV_EVENT_VALUE_CHANGED, nullptr);
}

void buildModal() {
  modal = lv_obj_create(lv_layer_top());
  lv_obj_set_size(modal, 200, 200);
  lv_obj_center(modal);
  lv_obj_set_style_radius(modal, 100, 0);
  lv_obj_set_style_bg_color(modal, lv_color_hex(kColPanel), 0);
  lv_obj_set_style_border_color(modal, lv_color_hex(kColAccentDim), 0);
  lv_obj_set_style_border_width(modal, 2, 0);
  lv_obj_clear_flag(modal, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(modal, LV_OBJ_FLAG_HIDDEN);

  modalTitle = lv_label_create(modal);
  lv_obj_set_style_text_font(modalTitle, &lv_font_roboto_20, 0);
  lv_label_set_text(modalTitle, "A");
  lv_obj_align(modalTitle, LV_ALIGN_TOP_MID, 0, 4);

  modalPresence = lv_label_create(modal);
  lv_obj_set_style_text_color(modalPresence, lv_color_hex(kColMuted), 0);
  lv_label_set_text(modalPresence, "not set");
  lv_obj_align(modalPresence, LV_ALIGN_TOP_MID, 0, 28);

  // Action buttons (Set / Go / Delete).
  lv_obj_t* setBtn = makeButton(modal, "Set", onModalSet, nullptr, kColAccentDim);
  lv_obj_set_size(setBtn, 48, 30);
  lv_obj_align(setBtn, LV_ALIGN_TOP_MID, -56, 48);

  lv_obj_t* goBtn = makeButton(modal, "Go", onModalGo, nullptr, kColAccentDim);
  lv_obj_set_size(goBtn, 48, 30);
  lv_obj_align(goBtn, LV_ALIGN_TOP_MID, 0, 48);

  lv_obj_t* delBtn = makeButton(modal, "Del", onModalDelete, nullptr, kColDanger);
  lv_obj_set_size(delBtn, 48, 30);
  lv_obj_align(delBtn, LV_ALIGN_TOP_MID, 56, 48);

  // Speed row (B..H only).
  speedRow = lv_obj_create(modal);
  lv_obj_set_size(speedRow, 168, 38);
  lv_obj_align(speedRow, LV_ALIGN_TOP_MID, 0, 86);
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
  lv_obj_set_size(speedSlider, 168, 10);
  lv_obj_align(speedSlider, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_slider_set_range(speedSlider, 0, 100);
  lv_obj_set_style_bg_color(speedSlider, lv_color_hex(kColAbsent), LV_PART_MAIN);
  lv_obj_set_style_bg_color(speedSlider, lv_color_hex(kColAccent), LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(speedSlider, lv_color_hex(kColAccent), LV_PART_KNOB);
  lv_obj_add_event_cb(speedSlider, onSpeedChanged, LV_EVENT_VALUE_CHANGED, nullptr);
  lv_obj_add_event_cb(speedSlider, onSpeedReleased, LV_EVENT_RELEASED, nullptr);

  // Hold row (B..H only).
  holdRow = lv_obj_create(modal);
  lv_obj_set_size(holdRow, 168, 32);
  lv_obj_align(holdRow, LV_ALIGN_TOP_MID, 0, 130);
  lv_obj_set_style_bg_opa(holdRow, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(holdRow, 0, 0);
  lv_obj_set_style_pad_all(holdRow, 0, 0);
  lv_obj_clear_flag(holdRow, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* holdCaption = lv_label_create(holdRow);
  lv_label_set_text(holdCaption, "Hold");
  lv_obj_set_style_text_color(holdCaption, lv_color_hex(kColMuted), 0);
  lv_obj_align(holdCaption, LV_ALIGN_LEFT_MID, 0, 0);

  lv_obj_t* minusBtn = makeButton(holdRow, "-", onHoldMinus, nullptr, kColAccentDim);
  lv_obj_set_size(minusBtn, 30, 28);
  lv_obj_align(minusBtn, LV_ALIGN_CENTER, 18, 0);

  holdValue = lv_label_create(holdRow);
  lv_label_set_text(holdValue, "0 s");
  lv_obj_align(holdValue, LV_ALIGN_CENTER, 70, 0);

  lv_obj_t* plusBtn = makeButton(holdRow, "+", onHoldPlus, nullptr, kColAccentDim);
  lv_obj_set_size(plusBtn, 30, 28);
  lv_obj_align(plusBtn, LV_ALIGN_RIGHT_MID, 0, 0);

  lv_obj_t* closeBtn = makeButton(modal, "Close", onModalClose, nullptr, kColPanel);
  lv_obj_set_size(closeBtn, 80, 30);
  lv_obj_align(closeBtn, LV_ALIGN_BOTTOM_MID, 0, -6);
}

void openModal(int slot) {
  if (slot < 0 || slot >= shark::kKeypointCount) {
    return;
  }
  modalSlot = slot;
  modalOpen = true;

  lv_label_set_text(modalTitle, kSlotLetters[slot]);

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
  const char* status = "Scanning...";
  switch (s.link) {
    case Link::Scanning:
      status = s.hasSavedDevice ? "Looking for slider..." : "Scanning...";
      break;
    case Link::Connecting:
      status = s.hasSavedDevice ? "Reconnecting..." : "Connecting...";
      break;
    case Link::Disconnected:
      status = s.hasSavedDevice ? "Reconnecting..." : "Scanning...";
      break;
    case Link::Connected:
      status = "Connected";
      break;
  }
  lv_label_set_text(connStatus, status);
  lv_label_set_text(connDevice,
                    (s.hasSavedDevice && s.deviceName[0] != '\0') ? s.deviceName
                                                                  : "iFootage Shark Nano II");
}

void refreshKeys(const shark::SharkClient::State& s) {
  char header[40];
  formatHeader(header, sizeof(header), s, "Keys");
  lv_label_set_text(keysHeader, header);

  if (s.trackingKnown && s.tracking) {
    lv_obj_add_state(trackSwitch, LV_STATE_CHECKED);
  } else {
    lv_obj_clear_state(trackSwitch, LV_STATE_CHECKED);
  }

  for (int i = 0; i < shark::kKeypointCount; ++i) {
    const bool present = s.present[i];
    char text[40];
    if (i == 0) {
      snprintf(text, sizeof(text), "%s %s  start", kSlotLetters[i], present ? "#" : "-");
    } else if (present && s.timingKnown && s.speed[i] >= 0) {
      snprintf(text, sizeof(text), "%s #  %d%%  %ds", kSlotLetters[i], s.speed[i], s.hold[i]);
    } else {
      snprintf(text, sizeof(text), "%s %s", kSlotLetters[i], present ? "#" : "-");
    }
    lv_label_set_text(keyLabels[i], text);
    lv_obj_set_style_bg_color(keyButtons[i],
                              lv_color_hex(present ? kColAccentDim : kColPanel), 0);
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

  if (s.loopOn) {
    lv_obj_add_state(loopSwitch, LV_STATE_CHECKED);
  } else {
    lv_obj_clear_state(loopSwitch, LV_STATE_CHECKED);
  }
  if (s.reverse) {
    lv_obj_add_state(dirSwitch, LV_STATE_CHECKED);
  } else {
    lv_obj_clear_state(dirSwitch, LV_STATE_CHECKED);
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
  currentMain = (currentMain == 0) ? 1 : 0;
  lv_scr_load(currentMain == 1 ? scrRun : scrKeys);
}

}  // namespace ui
