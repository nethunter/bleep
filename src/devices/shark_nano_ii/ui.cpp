#include "devices/shark_nano_ii/ui.h"

#include <Arduino.h>
#include <lvgl.h>

#include <cmath>
#include <cstdio>

#include "core/device_manager.h"
#include "devices/shark_nano_ii/client.h"
#include "devices/shark_nano_ii/protocol.h"
#include "fonts/ui_fonts.h"
#include "haptic_feedback.h"
#include "ui/ble_pairing_screen.h"
#include "../../ui.h"

namespace shark_ui {

namespace {

using Link = shark::SharkClient::Link;

studio::InstanceId activeInstance = studio::kInvalidInstanceId;

class SharkUiClient {
 public:
  const shark::SharkClient::State& state() const {
    const void* state = studio::devices().specializedState(activeInstance);
    return state != nullptr ? *static_cast<const shark::SharkClient::State*>(state)
                            : disconnectedState_;
  }

  bool connected() const { return state().link == Link::Connected; }
  void startScan() { send(studio::CommandType::Connect); }
  void forgetDevice() { send(studio::CommandType::ForgetPairing); }
  void refreshAll() { send(studio::CommandType::Refresh); }
  void requestTiming() { refreshAll(); }
  void keypointSet(int slot) { send(studio::CommandType::KeypointSet, slot); }
  void keypointGo(int slot) { send(studio::CommandType::KeypointGo, slot); }
  void keypointDelete(int slot) { send(studio::CommandType::KeypointDelete, slot); }
  void setSpeed(int slot, int value) { send(studio::CommandType::SetSpeed, slot, value); }
  void setHold(int slot, int value) { send(studio::CommandType::SetHold, slot, value); }
  void setRunState(uint8_t state) { send(studio::CommandType::SetRunState, state); }
  void setLoop(bool on) { send(studio::CommandType::SetLoop, on ? 1 : 0); }
  void setDirection(bool reverse) {
    send(studio::CommandType::SetDirection, reverse ? 1 : 0);
  }
  void setManualTracking(bool enabled) {
    send(studio::CommandType::SetManualTracking, enabled ? 1 : 0);
  }
  void setMotionVector(int slide, int pan) {
    send(studio::CommandType::SetMotionVector, slide, pan);
  }
  void stopMotion() { send(studio::CommandType::StopMotion); }

 private:
  void send(studio::CommandType type, int value0 = 0, int value1 = 0) {
    studio::DeviceCommand command;
    command.instanceId = activeInstance;
    command.type = type;
    command.value0 = value0;
    command.value1 = value1;
    studio::devices().enqueue(command);
  }

  shark::SharkClient::State disconnectedState_;
};

SharkUiClient gShark;

constexpr uint32_t kRefreshIntervalMs = 150;
const char* const kSlotLetters[shark::kKeypointCount] = {"A", "B", "C", "D", "E", "F", "G", "H"};

// Palette.
constexpr uint32_t kColBg = 0x05070A;
constexpr uint32_t kColPanel = 0x12161D;
constexpr uint32_t kColPanelRaised = 0x1A2029;
constexpr uint32_t kColAccent = 0x35C7F2;
constexpr uint32_t kColAccentDim = 0x1E5A6B;
constexpr uint32_t kColText = 0xF3F4F6;
constexpr uint32_t kColMuted = 0x8A94A6;
constexpr uint32_t kColPresent = 0x4ADE80;
constexpr uint32_t kColAbsent = 0x3A3F4A;

// 240 round panel: keep chrome inside the inscribed circle so corner controls
// stay visible and touchable (square TOP_LEFT at ~18,8 is clipped by the bezel).
constexpr lv_coord_t kRoundBackX = 40;
constexpr lv_coord_t kRoundBackY = 36;
constexpr uint32_t kColDanger = 0xF26D6D;

// Screens.
lv_obj_t* scrConnect = nullptr;
lv_obj_t* scrKeys = nullptr;
lv_obj_t* scrRun = nullptr;
studio_ui::BlePairingScreen pairingScreen;

// Keypoints screen widgets.
lv_obj_t* keysHeader = nullptr;
lv_obj_t* keyRows[shark::kKeypointCount] = {nullptr};
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
lv_obj_t* loopLabel = nullptr;
lv_obj_t* dirLabel = nullptr;

// Per-keypoint modal (lives on the top layer so it overlays any screen).
lv_obj_t* overlayScrim = nullptr;
lv_obj_t* modal = nullptr;
lv_obj_t* modalTitle = nullptr;
lv_obj_t* modalPresence = nullptr;
lv_obj_t* speedRow = nullptr;
lv_obj_t* speedSlider = nullptr;
lv_obj_t* speedValue = nullptr;
lv_obj_t* holdRow = nullptr;
lv_obj_t* holdValue = nullptr;

// Keypoint positioning overlay.
lv_obj_t* setOverlay = nullptr;
lv_obj_t* setTitle = nullptr;
lv_obj_t* setHint = nullptr;
lv_obj_t* setModeMark = nullptr;
lv_obj_t* joystickBase = nullptr;
lv_obj_t* joystickKnob = nullptr;
lv_obj_t* unlockBtn = nullptr;
lv_obj_t* joystickBtn = nullptr;
lv_obj_t* setBtn = nullptr;
lv_obj_t* cancelBtn = nullptr;

int currentMain = 0;  // 0 = keypoints, 1 = run
int modalSlot = -1;
int modalHold = 0;
bool modalOpen = false;

int setSlot = -1;
bool setOverlayOpen = false;
bool manualUnlockActive = false;
bool joystickHeld = false;
int joystickSlide = 0;
int joystickPan = 0;
int lastSentSlide = 0;
int lastSentPan = 0;
uint32_t lastMotionMs = 0;

// Timestamp of the last handled swipe. A horizontal swipe over a full-width
// row can still raise a button CLICKED on release, so taps that land within
// this window are ignored.
uint32_t lastSwipeMs = 0;
constexpr uint32_t kSwipeClickGuardMs = 350;
constexpr uint32_t kMotionIntervalMs = 170;
constexpr int kJoystickRadius = 34;
constexpr int kJoystickDeadZone = 6;

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

int nextSettableSlot(const shark::SharkClient::State& s) {
  if (!s.presenceKnown) {
    return 0;
  }
  for (int i = 0; i < shark::kKeypointCount; ++i) {
    if (!s.present[i]) {
      return i;
    }
  }
  return -1;
}

int clampVelocity(int value) {
  if (value < -shark::kManualMotionMaxVelocity) {
    return -shark::kManualMotionMaxVelocity;
  }
  if (value > shark::kManualMotionMaxVelocity) {
    return shark::kManualMotionMaxVelocity;
  }
  return value;
}

void centerJoystickKnob() {
  if (joystickKnob != nullptr) {
    lv_obj_center(joystickKnob);
  }
}

void sendMotionNow(int slide, int pan, uint32_t now) {
  joystickSlide = clampVelocity(slide);
  joystickPan = clampVelocity(pan);
  lastSentSlide = joystickSlide;
  lastSentPan = joystickPan;
  lastMotionMs = now;
  gShark.setMotionVector(joystickSlide, joystickPan);
}

void stopActiveMotion() {
  const bool moving = joystickHeld || joystickSlide != 0 || joystickPan != 0 ||
                      lastSentSlide != 0 || lastSentPan != 0;
  joystickHeld = false;
  joystickSlide = 0;
  joystickPan = 0;
  lastSentSlide = 0;
  lastSentPan = 0;
  centerJoystickKnob();
  if (moving) {
    gShark.stopMotion();
  }
}

void pumpMotion(uint32_t now) {
  if (!setOverlayOpen || !joystickHeld) {
    return;
  }
  if (now - lastMotionMs >= kMotionIntervalMs) {
    sendMotionNow(joystickSlide, joystickPan, now);
  }
}

void updateJoystickFromPoint(const lv_point_t& point, uint32_t now) {
  if (joystickBase == nullptr || joystickKnob == nullptr) {
    return;
  }

  lv_area_t base;
  lv_obj_get_coords(joystickBase, &base);
  const int centerX = (base.x1 + base.x2) / 2;
  const int centerY = (base.y1 + base.y2) / 2;
  float dx = static_cast<float>(point.x - centerX);
  float dy = static_cast<float>(point.y - centerY);
  const float dist = sqrtf(dx * dx + dy * dy);
  if (dist > kJoystickRadius && dist > 0.0f) {
    const float scale = static_cast<float>(kJoystickRadius) / dist;
    dx *= scale;
    dy *= scale;
  }

  const int baseCenterX = lv_obj_get_width(joystickBase) / 2;
  const int baseCenterY = lv_obj_get_height(joystickBase) / 2;
  lv_obj_set_pos(joystickKnob,
                 static_cast<int>(baseCenterX + dx - lv_obj_get_width(joystickKnob) / 2),
                 static_cast<int>(baseCenterY + dy - lv_obj_get_height(joystickKnob) / 2));

  if (fabsf(dx) < kJoystickDeadZone) {
    dx = 0.0f;
  }
  if (fabsf(dy) < kJoystickDeadZone) {
    dy = 0.0f;
  }

  const int slide = static_cast<int>(dx * shark::kManualMotionMaxVelocity / kJoystickRadius);
  const int pan = static_cast<int>(-dy * shark::kManualMotionMaxVelocity / kJoystickRadius);
  const bool wasHeld = joystickHeld;
  joystickHeld = true;
  joystickSlide = clampVelocity(slide);
  joystickPan = clampVelocity(pan);

  const bool centered = joystickSlide == 0 && joystickPan == 0;
  const bool sentMoving = lastSentSlide != 0 || lastSentPan != 0;
  if (!wasHeld || (centered && sentMoving) || now - lastMotionMs >= kMotionIntervalMs) {
    sendMotionNow(joystickSlide, joystickPan, now);
  }
}

void showPositionChoice() {
  if (unlockBtn != nullptr) {
    lv_obj_clear_flag(unlockBtn, LV_OBJ_FLAG_HIDDEN);
  }
  if (joystickBtn != nullptr) {
    lv_obj_clear_flag(joystickBtn, LV_OBJ_FLAG_HIDDEN);
  }
  if (joystickBase != nullptr) {
    lv_obj_add_flag(joystickBase, LV_OBJ_FLAG_HIDDEN);
  }
  if (setBtn != nullptr) {
    lv_obj_add_flag(setBtn, LV_OBJ_FLAG_HIDDEN);
  }
  if (setModeMark != nullptr) {
    lv_obj_add_flag(setModeMark, LV_OBJ_FLAG_HIDDEN);
  }
}

void showSetActions() {
  if (unlockBtn != nullptr) {
    lv_obj_add_flag(unlockBtn, LV_OBJ_FLAG_HIDDEN);
  }
  if (joystickBtn != nullptr) {
    lv_obj_add_flag(joystickBtn, LV_OBJ_FLAG_HIDDEN);
  }
  if (setBtn != nullptr) {
    lv_obj_clear_flag(setBtn, LV_OBJ_FLAG_HIDDEN);
  }
}

// ---- event handlers -------------------------------------------------------

void closeModal();

void destroySetOverlayObjects();
void destroyModalObjects();
void maybeDestroyScrim();

void closeSetOverlay(bool commit) {
  const int slot = setSlot;
  const bool wasUnlocked = manualUnlockActive;
  stopActiveMotion();
  setOverlayOpen = false;
  setSlot = -1;
  manualUnlockActive = false;
  destroySetOverlayObjects();
  maybeDestroyScrim();
  if (commit && slot >= 0) {
    gShark.keypointSet(slot);
  }
  if (wasUnlocked) {
    gShark.setManualTracking(false);
  }
}

void cancelPositioning() { closeSetOverlay(false); }

void ensureScrim();
void buildSetOverlay();
void showConnectedMain(int nextMain);

void openSetOverlay(int slot) {
  if (slot < 0 || slot >= shark::kKeypointCount) {
    return;
  }
  if (modalOpen) {
    closeModal();
  }
  stopActiveMotion();
  manualUnlockActive = false;
  setSlot = slot;
  setOverlayOpen = true;
  ensureScrim();
  buildSetOverlay();

  char title[24];
  snprintf(title, sizeof(title), "Set %s Position", kSlotLetters[slot]);
  lv_label_set_text(setTitle, title);
  lv_label_set_text(setHint, "Choose a method");
  centerJoystickKnob();
  showPositionChoice();
  lv_obj_clear_flag(overlayScrim, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(setOverlay, LV_OBJ_FLAG_HIDDEN);
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
    cancelPositioning();
    lastSwipeMs = millis();
    showConnectedMain(1);
  } else if (scr == scrRun && dir == LV_DIR_LEFT) {
    lastSwipeMs = millis();
    showConnectedMain(0);
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

// Advance the primary run action: stopped -> standby -> running -> stopped.
void advanceRunState() {
  const uint8_t code = gShark.state().runStateCode;
  if (code == shark::kRunStart || code == 0x06 /* preview */) {
    gShark.setRunState(shark::kRunStop);
  } else if (code == shark::kRunStandby) {
    gShark.setRunState(shark::kRunStart);
  } else {
    gShark.setRunState(shark::kRunStandby);
  }
}

void onRunAction(lv_event_t*) { advanceRunState(); }

void onRepair(lv_event_t*) {
  if (studio::devices().pendingAddCommitFailed(activeInstance)) {
    studio::devices().retryPendingAdd(activeInstance);
    return;
  }
  gShark.forgetDevice();
}
void onBack(lv_event_t*) {
  haptic_feedback::request(haptic_feedback::Pattern::Back);
  hide();
  ui::showDeviceParent();
}

void closeModal() {
  modalOpen = false;
  modalSlot = -1;
  destroyModalObjects();
  maybeDestroyScrim();
}

void openModal(int slot);

// Primary row tap. Going to a set keypoint is the fast default; an unset slot
// opens the joystick positioning overlay for the one next settable slot.
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
    cancelPositioning();
    gShark.keypointGo(slot);
  } else if (slot == nextSettableSlot(s)) {
    openSetOverlay(slot);
  }
}

void onKeyGear(lv_event_t* e) {
  if (millis() - lastSwipeMs < kSwipeClickGuardMs) {
    return;  // release after a swipe, not a real tap
  }
  const int slot = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
  if (slot >= 0 && slot < shark::kKeypointCount && gShark.state().present[slot]) {
    cancelPositioning();
    openModal(slot);
  }
}

void onModalClose(lv_event_t*) { closeModal(); }

void onSetCommit(lv_event_t*) { closeSetOverlay(true); }

void onSetCancel(lv_event_t*) { closeSetOverlay(false); }

void onSetManual(lv_event_t*) {
  if (setSlot < 0) {
    return;
  }
  manualUnlockActive = true;
  stopActiveMotion();
  gShark.setManualTracking(true);
  char title[24];
  snprintf(title, sizeof(title), "Set %s Manually", kSlotLetters[setSlot]);
  lv_label_set_text(setTitle, title);
  lv_label_set_text(setHint, "Move by hand, then save");
  lv_label_set_text(setModeMark, "MANUAL POSITIONING ACTIVE");
  lv_obj_clear_flag(setModeMark, LV_OBJ_FLAG_HIDDEN);
  if (joystickBase != nullptr) {
    lv_obj_add_flag(joystickBase, LV_OBJ_FLAG_HIDDEN);
  }
  showSetActions();
}

void onSetJoystick(lv_event_t*) {
  if (setSlot < 0) {
    return;
  }
  if (manualUnlockActive) {
    gShark.setManualTracking(false);
    manualUnlockActive = false;
  }
  stopActiveMotion();
  char title[24];
  snprintf(title, sizeof(title), "Set %s Joystick", kSlotLetters[setSlot]);
  lv_label_set_text(setTitle, title);
  lv_label_set_text(setHint, "Drag, then save");
  lv_obj_add_flag(setModeMark, LV_OBJ_FLAG_HIDDEN);
  centerJoystickKnob();
  if (joystickBase != nullptr) {
    lv_obj_clear_flag(joystickBase, LV_OBJ_FLAG_HIDDEN);
  }
  showSetActions();
}

void onJoystick(lv_event_t* e) {
  const lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_PRESSED || code == LV_EVENT_PRESSING) {
    lv_point_t point;
    lv_indev_get_point(lv_indev_get_act(), &point);
    updateJoystickFromPoint(point, millis());
  } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
    stopActiveMotion();
  }
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
  if (scrConnect != nullptr) {
    return;
  }
  pairingScreen.create(onBack, onRepair);
  pairingScreen.setTitle("Shark Remote");
  pairingScreen.setStatus("Starting Bluetooth", "iFootage Shark Nano II",
                          true, true, "Retry");
  scrConnect = pairingScreen.screen();
}

void buildKeysScreen() {
  if (scrKeys != nullptr) {
    return;
  }
  scrKeys = lv_obj_create(nullptr);
  styleScreen(scrKeys);
  lv_obj_add_event_cb(scrKeys, onScreenGesture, LV_EVENT_GESTURE, nullptr);

  lv_obj_t* back = makeButton(scrKeys, LV_SYMBOL_LEFT, onBack, nullptr, kColPanel,
                              UI_FONT_14);
  lv_obj_set_size(back, 34, 30);
  lv_obj_align(back, LV_ALIGN_TOP_LEFT, kRoundBackX, kRoundBackY);

  keysHeader = lv_label_create(scrKeys);
  lv_obj_set_style_text_font(keysHeader, UI_FONT_14, 0);
  lv_label_set_text(keysHeader, "Keypoints");
  lv_obj_set_width(keysHeader, 136);
  lv_obj_set_style_text_align(keysHeader, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(keysHeader, LV_ALIGN_TOP_MID, 14, 43);

  // Scrollable keypoint list. With the manual-track switch gone it can use the
  // full mid-band height; horizontal swipes bubble up to the screen gesture
  // handler so the Run view is reachable.
  keyList = lv_obj_create(scrKeys);
  lv_obj_set_size(keyList, 200, 148);
  lv_obj_align(keyList, LV_ALIGN_TOP_MID, 0, 61);
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
    lv_obj_set_height(row, 34);
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
    lv_obj_set_style_radius(btn, 8, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_pad_left(btn, 10, 0);
    lv_obj_add_event_cb(btn, onKeyMain, LV_EVENT_CLICKED, slotData);
    lv_obj_t* label = lv_label_create(btn);
    lv_obj_set_style_text_font(label, UI_FONT_14, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(kColText), 0);
    lv_obj_align(label, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t* gear = lv_btn_create(row);
    lv_obj_set_size(gear, 34, 32);
    lv_obj_set_style_bg_color(gear, lv_color_hex(kColPanel), 0);
    lv_obj_set_style_radius(gear, 8, 0);
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
    keyRows[i] = row;
  }
}

void buildRunScreen() {
  if (scrRun != nullptr) {
    return;
  }
  scrRun = lv_obj_create(nullptr);
  styleScreen(scrRun);
  lv_obj_add_event_cb(scrRun, onScreenGesture, LV_EVENT_GESTURE, nullptr);

  lv_obj_t* back = makeButton(scrRun, LV_SYMBOL_LEFT, onBack, nullptr, kColPanel,
                              UI_FONT_14);
  lv_obj_set_size(back, 34, 30);
  lv_obj_align(back, LV_ALIGN_TOP_LEFT, kRoundBackX, kRoundBackY);

  runHeader = lv_label_create(scrRun);
  lv_obj_set_style_text_font(runHeader, UI_FONT_14, 0);
  lv_label_set_text(runHeader, "Run");
  lv_obj_set_width(runHeader, 136);
  lv_obj_set_style_text_align(runHeader, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(runHeader, LV_ALIGN_TOP_MID, 14, 43);

  runBar = lv_bar_create(scrRun);
  lv_obj_set_size(runBar, 150, 8);
  lv_obj_align(runBar, LV_ALIGN_TOP_MID, 0, 66);
  lv_obj_set_style_radius(runBar, 4, LV_PART_MAIN);
  lv_obj_set_style_radius(runBar, 4, LV_PART_INDICATOR);
  lv_bar_set_range(runBar, 0, 100);
  lv_bar_set_value(runBar, 0, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(runBar, lv_color_hex(kColPanel), LV_PART_MAIN);
  lv_obj_set_style_bg_color(runBar, lv_color_hex(kColAccent), LV_PART_INDICATOR);

  runPercent = lv_label_create(scrRun);
  lv_obj_set_style_text_font(runPercent, UI_FONT_14, 0);
  lv_obj_set_style_text_color(runPercent, lv_color_hex(kColMuted), 0);
  lv_label_set_text(runPercent, "0%");
  lv_obj_align(runPercent, LV_ALIGN_TOP_MID, 0, 80);

  // Single large run-state button. Its label/color and action follow the run
  // state machine (stopped -> Standby -> Start -> Stop); only one shows at a
  // time so it stays big and unambiguous.
  runActionBtn = makeButton(scrRun, "Standby", onRunAction, nullptr, kColPanel, UI_FONT_20);
  lv_obj_set_size(runActionBtn, 136, 58);
  lv_obj_align(runActionBtn, LV_ALIGN_CENTER, 0, 11);
  lv_obj_set_style_radius(runActionBtn, 14, 0);
  runActionLabel = lv_obj_get_child(runActionBtn, 0);

  // Text labels keep the toggles understandable without relying on icon lore.
  loopBtn = lv_btn_create(scrRun);
  lv_obj_add_flag(loopBtn, LV_OBJ_FLAG_CHECKABLE);
  lv_obj_set_size(loopBtn, 72, 34);
  lv_obj_align(loopBtn, LV_ALIGN_BOTTOM_MID, -39, -25);
  lv_obj_set_style_bg_color(loopBtn, lv_color_hex(kColPanel), 0);
  lv_obj_set_style_bg_color(loopBtn, lv_color_hex(kColAccentDim), LV_STATE_CHECKED);
  lv_obj_set_style_radius(loopBtn, 8, 0);
  lv_obj_set_style_shadow_width(loopBtn, 0, 0);
  lv_obj_add_event_cb(loopBtn, onLoopToggle, LV_EVENT_CLICKED, nullptr);
  loopLabel = lv_label_create(loopBtn);
  lv_label_set_text(loopLabel, "Loop on");
  lv_obj_set_style_text_font(loopLabel, UI_FONT_14, 0);
  lv_obj_set_style_text_color(loopLabel, lv_color_hex(kColText), 0);
  lv_obj_center(loopLabel);

  dirBtn = lv_btn_create(scrRun);
  lv_obj_add_flag(dirBtn, LV_OBJ_FLAG_CHECKABLE);
  lv_obj_set_size(dirBtn, 72, 34);
  lv_obj_align(dirBtn, LV_ALIGN_BOTTOM_MID, 39, -25);
  lv_obj_set_style_bg_color(dirBtn, lv_color_hex(kColPanel), 0);
  lv_obj_set_style_bg_color(dirBtn, lv_color_hex(kColAccentDim), LV_STATE_CHECKED);
  lv_obj_set_style_radius(dirBtn, 8, 0);
  lv_obj_set_style_shadow_width(dirBtn, 0, 0);
  lv_obj_add_event_cb(dirBtn, onDirToggle, LV_EVENT_CLICKED, nullptr);
  dirLabel = lv_label_create(dirBtn);
  lv_label_set_text(dirLabel, "Forward");
  lv_obj_set_style_text_font(dirLabel, UI_FONT_14, 0);
  lv_obj_set_style_text_color(dirLabel, lv_color_hex(kColText), 0);
  lv_obj_center(dirLabel);
}

void ensureScrim() {
  if (overlayScrim != nullptr) {
    return;
  }
  // Opaque backing prevents the active screen from showing around the rounded
  // panel near the physical display edge.
  overlayScrim = lv_obj_create(lv_layer_top());
  lv_obj_set_size(overlayScrim, 240, 240);
  lv_obj_center(overlayScrim);
  lv_obj_set_style_radius(overlayScrim, 0, 0);
  lv_obj_set_style_bg_color(overlayScrim, lv_color_hex(kColPanel), 0);
  lv_obj_set_style_bg_opa(overlayScrim, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(overlayScrim, 0, 0);
  lv_obj_set_style_pad_all(overlayScrim, 0, 0);
  lv_obj_clear_flag(overlayScrim, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(overlayScrim, LV_OBJ_FLAG_HIDDEN);
}

void maybeDestroyScrim() {
  if (modalOpen || setOverlayOpen || overlayScrim == nullptr) {
    return;
  }
  lv_obj_del(overlayScrim);
  overlayScrim = nullptr;
}

void destroyModalObjects() {
  if (modal == nullptr) {
    return;
  }
  lv_obj_del(modal);
  modal = nullptr;
  modalTitle = nullptr;
  modalPresence = nullptr;
  speedRow = nullptr;
  speedSlider = nullptr;
  speedValue = nullptr;
  holdRow = nullptr;
  holdValue = nullptr;
}

void destroySetOverlayObjects() {
  if (setOverlay == nullptr) {
    return;
  }
  lv_obj_del(setOverlay);
  setOverlay = nullptr;
  setTitle = nullptr;
  setHint = nullptr;
  setModeMark = nullptr;
  joystickBase = nullptr;
  joystickKnob = nullptr;
  unlockBtn = nullptr;
  joystickBtn = nullptr;
  setBtn = nullptr;
  cancelBtn = nullptr;
}

void buildModal() {
  if (modal != nullptr) {
    return;
  }
  // Full-screen settings panel. The accent border reads as a ring at the bezel
  // and overlays whatever screen is active.
  modal = lv_obj_create(lv_layer_top());
  lv_obj_set_size(modal, 240, 240);
  lv_obj_center(modal);
  lv_obj_set_style_radius(modal, 120, 0);
  lv_obj_set_style_bg_color(modal, lv_color_hex(kColPanel), 0);
  lv_obj_set_style_border_color(modal, lv_color_hex(kColAccent), 0);
  lv_obj_set_style_border_width(modal, 3, 0);
  lv_obj_set_style_pad_all(modal, 0, 0);
  lv_obj_clear_flag(modal, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(modal, LV_OBJ_FLAG_HIDDEN);

  // Fixed footer: Delete / Close stay pinned at the bottom of the panel.
  lv_obj_t* btnRow = lv_obj_create(modal);
  lv_obj_set_size(btnRow, 176, 40);
  lv_obj_align(btnRow, LV_ALIGN_BOTTOM_MID, 0, -14);
  lv_obj_set_style_bg_opa(btnRow, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(btnRow, 0, 0);
  lv_obj_set_style_pad_all(btnRow, 0, 0);
  lv_obj_clear_flag(btnRow, LV_OBJ_FLAG_SCROLLABLE);

  // Close is the primary action (accent). Delete is intentionally subdued so it
  // doesn't read as the default CTA.
  lv_obj_t* closeBtn = makeButton(btnRow, "Done", onModalClose, nullptr, kColAccent);
  lv_obj_set_size(closeBtn, 104, 36);
  lv_obj_align(closeBtn, LV_ALIGN_RIGHT_MID, -2, 0);

  lv_obj_t* delBtn = makeButton(btnRow, "Delete", onModalDelete, nullptr, kColAbsent, UI_FONT_14);
  lv_obj_set_size(delBtn, 58, 32);
  lv_obj_align(delBtn, LV_ALIGN_LEFT_MID, 2, 0);
  lv_obj_set_style_text_color(lv_obj_get_child(delBtn, 0), lv_color_hex(kColDanger), 0);

  // Fixed content above the footer keeps every control in the round safe area.
  lv_obj_t* content = lv_obj_create(modal);
  lv_obj_set_size(content, 190, 158);
  lv_obj_align(content, LV_ALIGN_TOP_MID, 0, 17);
  lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(content, 0, 0);
  lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_top(content, 8, 0);
  lv_obj_set_style_pad_bottom(content, 8, 0);
  lv_obj_set_style_pad_left(content, 0, 0);
  lv_obj_set_style_pad_right(content, 0, 0);
  lv_obj_set_style_pad_row(content, 9, 0);
  lv_obj_set_scroll_dir(content, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(content, LV_SCROLLBAR_MODE_OFF);

  modalTitle = lv_label_create(content);
  lv_obj_set_style_text_font(modalTitle, UI_FONT_20, 0);
  lv_label_set_text(modalTitle, "A Settings");

  modalPresence = lv_label_create(content);
  lv_obj_set_style_text_color(modalPresence, lv_color_hex(kColMuted), 0);
  lv_label_set_text(modalPresence, "not set");

  // Speed row (B..H only).
  speedRow = lv_obj_create(content);
  lv_obj_set_size(speedRow, 176, 46);
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
  lv_obj_set_size(holdRow, 176, 36);
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

void buildSetOverlay() {
  if (setOverlay != nullptr) {
    return;
  }
  setOverlay = lv_obj_create(lv_layer_top());
  lv_obj_set_size(setOverlay, 240, 240);
  lv_obj_center(setOverlay);
  lv_obj_set_style_radius(setOverlay, 120, 0);
  lv_obj_set_style_bg_color(setOverlay, lv_color_hex(kColPanel), 0);
  lv_obj_set_style_border_color(setOverlay, lv_color_hex(kColAccent), 0);
  lv_obj_set_style_border_width(setOverlay, 3, 0);
  lv_obj_set_style_pad_all(setOverlay, 0, 0);
  lv_obj_clear_flag(setOverlay, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(setOverlay, LV_OBJ_FLAG_HIDDEN);

  setTitle = lv_label_create(setOverlay);
  lv_obj_set_style_text_font(setTitle, UI_FONT_20, 0);
  lv_label_set_text(setTitle, "Set Position");
  lv_obj_align(setTitle, LV_ALIGN_TOP_MID, 0, 24);

  setHint = lv_label_create(setOverlay);
  lv_obj_set_width(setHint, 178);
  lv_obj_set_style_text_font(setHint, UI_FONT_14, 0);
  lv_obj_set_style_text_align(setHint, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(setHint, lv_color_hex(kColMuted), 0);
  lv_label_set_text(setHint, "Choose a method");
  lv_obj_align(setHint, LV_ALIGN_TOP_MID, 0, 50);

  setModeMark = lv_label_create(setOverlay);
  lv_obj_set_width(setModeMark, 174);
  lv_obj_set_style_text_font(setModeMark, UI_FONT_14, 0);
  lv_obj_set_style_text_align(setModeMark, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_color(setModeMark, lv_color_hex(kColAccent), 0);
  lv_label_set_text(setModeMark, "MANUAL POSITIONING ACTIVE");
  lv_obj_align(setModeMark, LV_ALIGN_CENTER, 0, 2);
  lv_obj_add_flag(setModeMark, LV_OBJ_FLAG_HIDDEN);

  unlockBtn = makeButton(setOverlay, "Move by hand", onSetManual, nullptr, kColAccent, UI_FONT_14);
  lv_obj_set_size(unlockBtn, 136, 38);
  lv_obj_align(unlockBtn, LV_ALIGN_CENTER, 0, -16);

  joystickBtn = makeButton(setOverlay, "Joystick", onSetJoystick, nullptr, kColAccentDim, UI_FONT_16);
  lv_obj_set_size(joystickBtn, 136, 38);
  lv_obj_align(joystickBtn, LV_ALIGN_CENTER, 0, 30);

  joystickBase = lv_obj_create(setOverlay);
  lv_obj_set_size(joystickBase, 108, 108);
  lv_obj_align(joystickBase, LV_ALIGN_CENTER, 0, 5);
  lv_obj_set_style_radius(joystickBase, 54, 0);
  lv_obj_set_style_bg_color(joystickBase, lv_color_hex(kColBg), 0);
  lv_obj_set_style_border_color(joystickBase, lv_color_hex(kColAccentDim), 0);
  lv_obj_set_style_border_width(joystickBase, 2, 0);
  lv_obj_clear_flag(joystickBase, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(joystickBase, onJoystick, LV_EVENT_PRESSED, nullptr);
  lv_obj_add_event_cb(joystickBase, onJoystick, LV_EVENT_PRESSING, nullptr);
  lv_obj_add_event_cb(joystickBase, onJoystick, LV_EVENT_RELEASED, nullptr);
  lv_obj_add_event_cb(joystickBase, onJoystick, LV_EVENT_PRESS_LOST, nullptr);
  lv_obj_add_flag(joystickBase, LV_OBJ_FLAG_HIDDEN);

  joystickKnob = lv_obj_create(joystickBase);
  lv_obj_set_size(joystickKnob, 34, 34);
  lv_obj_set_style_radius(joystickKnob, 17, 0);
  lv_obj_set_style_bg_color(joystickKnob, lv_color_hex(kColAccent), 0);
  lv_obj_set_style_border_width(joystickKnob, 0, 0);
  lv_obj_clear_flag(joystickKnob, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_clear_flag(joystickKnob, LV_OBJ_FLAG_CLICKABLE);
  centerJoystickKnob();

  lv_obj_t* slideL = lv_label_create(joystickBase);
  lv_label_set_text(slideL, LV_SYMBOL_LEFT);
  lv_obj_set_style_text_font(slideL, UI_FONT_14, 0);
  lv_obj_set_style_text_color(slideL, lv_color_hex(kColMuted), 0);
  lv_obj_align(slideL, LV_ALIGN_LEFT_MID, 7, 0);

  lv_obj_t* slideR = lv_label_create(joystickBase);
  lv_label_set_text(slideR, LV_SYMBOL_RIGHT);
  lv_obj_set_style_text_font(slideR, UI_FONT_14, 0);
  lv_obj_set_style_text_color(slideR, lv_color_hex(kColMuted), 0);
  lv_obj_align(slideR, LV_ALIGN_RIGHT_MID, -7, 0);

  lv_obj_t* panL = lv_label_create(joystickBase);
  lv_label_set_text(panL, LV_SYMBOL_UP);
  lv_obj_set_style_text_font(panL, UI_FONT_14, 0);
  lv_obj_set_style_text_color(panL, lv_color_hex(kColMuted), 0);
  lv_obj_align(panL, LV_ALIGN_TOP_MID, 0, 7);

  lv_obj_t* panR = lv_label_create(joystickBase);
  lv_label_set_text(panR, LV_SYMBOL_DOWN);
  lv_obj_set_style_text_font(panR, UI_FONT_14, 0);
  lv_obj_set_style_text_color(panR, lv_color_hex(kColMuted), 0);
  lv_obj_align(panR, LV_ALIGN_BOTTOM_MID, 0, -7);

  setBtn = makeButton(setOverlay, "Save", onSetCommit, nullptr, kColAccent);
  lv_obj_set_size(setBtn, 82, 34);
  lv_obj_align(setBtn, LV_ALIGN_BOTTOM_MID, 45, -16);
  lv_obj_add_flag(setBtn, LV_OBJ_FLAG_HIDDEN);

  cancelBtn = makeButton(setOverlay, "Cancel", onSetCancel, nullptr, kColAbsent, UI_FONT_14);
  lv_obj_set_size(cancelBtn, 78, 32);
  lv_obj_align(cancelBtn, LV_ALIGN_BOTTOM_MID, -45, -17);
  lv_obj_set_style_text_color(lv_obj_get_child(cancelBtn, 0), lv_color_hex(kColMuted), 0);
}

void openModal(int slot) {
  if (slot < 0 || slot >= shark::kKeypointCount) {
    return;
  }
  modalSlot = slot;
  modalOpen = true;
  ensureScrim();
  buildModal();

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
  lv_obj_clear_flag(overlayScrim, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(modal);
}

// ---- refresh --------------------------------------------------------------

void formatHeader(char* out, size_t cap, const shark::SharkClient::State& s, const char* title) {
  char battery[12];
  if (s.battery >= 0) {
    snprintf(battery, sizeof(battery), "%d%%", s.battery);
  } else {
    snprintf(battery, sizeof(battery), "--");
  }
  snprintf(out, cap, "%s  %s", title, battery);
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
  pairingScreen.setStatus(
      statusText,
      (s.hasSavedDevice && s.deviceName[0] != '\0') ? s.deviceName
                                                     : "iFootage Shark Nano II",
      s.link != Link::Connected, true, s.hasSavedDevice ? "Re-pair" : "Retry");
}

void refreshKeys(const shark::SharkClient::State& s) {
  char header[40];
  formatHeader(header, sizeof(header), s, "Keys");
  lv_label_set_text(keysHeader, header);

  const int nextSlot = nextSettableSlot(s);
  for (int i = 0; i < shark::kKeypointCount; ++i) {
    const bool present = s.present[i];
    const bool visible = present || i == nextSlot;
    if (visible) {
      lv_obj_clear_flag(keyRows[i], LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(keyRows[i], LV_OBJ_FLAG_HIDDEN);
    }

    char text[40];
    if (!present) {
      snprintf(text, sizeof(text), "%s   + Add position", kSlotLetters[i]);
    } else if (i == 0) {
      snprintf(text, sizeof(text), "%s   Start point", kSlotLetters[i]);
    } else if (present && s.timingKnown && s.speed[i] >= 0) {
      snprintf(text, sizeof(text), "%s   %d%%  |  %ds", kSlotLetters[i], s.speed[i], s.hold[i]);
    } else {
      snprintf(text, sizeof(text), "%s   Configured", kSlotLetters[i]);
    }
    lv_label_set_text(keyLabels[i], text);
    uint32_t bg = present ? kColAccentDim : kColPanel;
    lv_obj_set_style_bg_color(keyButtons[i], lv_color_hex(bg), 0);
    lv_obj_set_style_bg_color(gearButtons[i], lv_color_hex(bg), 0);
    if (present) {
      lv_obj_clear_flag(gearButtons[i], LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(gearButtons[i], LV_OBJ_FLAG_HIDDEN);
    }
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
    lv_label_set_text(loopLabel, "Loop on");
  } else {
    lv_obj_clear_state(loopBtn, LV_STATE_CHECKED);
    lv_label_set_text(loopLabel, "Loop off");
  }
  if (s.reverse) {
    lv_obj_add_state(dirBtn, LV_STATE_CHECKED);
    lv_label_set_text(dirLabel, "Reverse");
  } else {
    lv_obj_clear_state(dirBtn, LV_STATE_CHECKED);
    lv_label_set_text(dirLabel, "Forward");
  }
}

void refreshModal(const shark::SharkClient::State& s) {
  if (!modalOpen || modalSlot < 0) {
    return;
  }
  lv_label_set_text(modalPresence, s.present[modalSlot] ? "configured" : "not set");
}

void clearKeysPointers() {
  scrKeys = nullptr;
  keysHeader = nullptr;
  keyList = nullptr;
  for (int i = 0; i < shark::kKeypointCount; ++i) {
    keyRows[i] = nullptr;
    keyButtons[i] = nullptr;
    keyLabels[i] = nullptr;
    gearButtons[i] = nullptr;
  }
}

void clearRunPointers() {
  scrRun = nullptr;
  runHeader = nullptr;
  runBar = nullptr;
  runPercent = nullptr;
  runActionBtn = nullptr;
  runActionLabel = nullptr;
  loopBtn = nullptr;
  dirBtn = nullptr;
  loopLabel = nullptr;
  dirLabel = nullptr;
}

void destroyKeysScreen() {
  if (scrKeys != nullptr && lv_scr_act() != scrKeys) {
    lv_obj_del(scrKeys);
    clearKeysPointers();
  }
}

void destroyRunScreen() {
  if (scrRun != nullptr && lv_scr_act() != scrRun) {
    lv_obj_del(scrRun);
    clearRunPointers();
  }
}

void showConnectedMain(int nextMain) {
  currentMain = nextMain == 1 ? 1 : 0;
  if (currentMain == 1) {
    buildRunScreen();
    lv_scr_load(scrRun);
    destroyKeysScreen();
  } else {
    buildKeysScreen();
    lv_scr_load(scrKeys);
    destroyRunScreen();
  }
  if (scrConnect != nullptr && lv_scr_act() != scrConnect) {
    pairingScreen.destroy();
    scrConnect = nullptr;
  }
}

void clearScreenPointers() {
  scrConnect = nullptr;
  clearKeysPointers();
  clearRunPointers();
}

void destroyIdleScreens() {
  const lv_obj_t* active = lv_scr_act();
  if (active == scrConnect || active == scrKeys || active == scrRun) {
    return;
  }
  if (scrConnect != nullptr) {
    pairingScreen.destroy();
  }
  if (scrKeys != nullptr) {
    lv_obj_del(scrKeys);
  }
  if (scrRun != nullptr) {
    lv_obj_del(scrRun);
  }
  clearScreenPointers();
  destroySetOverlayObjects();
  destroyModalObjects();
  maybeDestroyScrim();
  if (overlayScrim != nullptr) {
    lv_obj_del(overlayScrim);
    overlayScrim = nullptr;
  }
}

void showScreenForState(const shark::SharkClient::State& s) {
  if (studio::devices().pendingAddCommitFailed(activeInstance)) {
    buildConnectScreen();
    pairingScreen.setStatus("Couldn't save", "Retry to add this device",
                            false, true, "Retry");
    if (lv_scr_act() != scrConnect) {
      lv_scr_load(scrConnect);
      destroyKeysScreen();
      destroyRunScreen();
    }
  } else if (s.link == Link::Connected) {
    lv_obj_t* target = (currentMain == 1) ? scrRun : scrKeys;
    if (lv_scr_act() != target) {
      showConnectedMain(currentMain);
    }
  } else {
    if (modalOpen) {
      closeModal();
    }
    if (setOverlayOpen) {
      cancelPositioning();
    }
    buildConnectScreen();
    if (lv_scr_act() != scrConnect) {
      lv_scr_load(scrConnect);
      destroyKeysScreen();
      destroyRunScreen();
    }
  }
}

}  // namespace

void init() {}

void show(studio::InstanceId instanceId) {
  if (!studio::devices().acquire(instanceId,
                                 studio::ConnectionOwner::Foreground)) {
    return;
  }
  activeInstance = instanceId;
  currentMain = 0;
  buildConnectScreen();
  const studio::DeviceRecord* record = studio::devices().find(instanceId);
  if (record != nullptr) {
    pairingScreen.setTitle(record->displayName);
  }
  lv_scr_load(scrConnect);
  ui::releaseInactiveScreens();
}

void hide() {
  if (setOverlayOpen) {
    cancelPositioning();
  }
  if (modalOpen) {
    closeModal();
  }
  stopActiveMotion();
  studio::devices().release(activeInstance,
                            studio::ConnectionOwner::Foreground);
  activeInstance = studio::kInvalidInstanceId;
}

void release() {
  if (active()) {
    return;
  }
  destroyIdleScreens();
}

bool active() { return activeInstance != studio::kInvalidInstanceId; }

void tick() {
  if (!active()) {
    return;
  }
  const uint32_t now = millis();
  pumpMotion(now);
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

void handleShortPress() {
  if (!gShark.connected()) {
    return;
  }
  if (modalOpen || setOverlayOpen) {
    return;
  }
  if (currentMain == 1) {
    advanceRunState();
    return;
  }
  showConnectedMain(1);
}

void handleLongPress() {
  if (modalOpen) {
    closeModal();
    return;
  }
  if (setOverlayOpen) {
    cancelPositioning();
    return;
  }
  hide();
  ui::showDeviceParent();
}

#ifdef UI_SIMULATOR
void simShowKeypoints() {
  showConnectedMain(0);
}

void simShowKeypointSettings(int slot) { openModal(slot); }

void simShowPositionChoice(int slot) { openSetOverlay(slot); }

void simShowManualPositioning() { onSetManual(nullptr); }

void simShowJoystickPositioning() { onSetJoystick(nullptr); }
#endif

}  // namespace shark_ui
