#include "devices/canon_ble/ui.h"

#include <Arduino.h>
#include <lvgl.h>

#include "core/device_manager.h"
#include "devices/canon_ble/state.h"
#include "fonts/ui_fonts.h"
#include "../../ui.h"

namespace canon_ble_ui {

namespace {

constexpr uint32_t kBg = 0x05070A;
constexpr uint32_t kPanel = 0x171B22;
constexpr uint32_t kAccent = 0xE53935;
constexpr uint32_t kReady = 0x2E7D5B;
constexpr uint32_t kText = 0xF3F4F6;
constexpr uint32_t kMuted = 0x8A94A6;

lv_obj_t* screen = nullptr;
lv_obj_t* titleLabel = nullptr;
lv_obj_t* statusLabel = nullptr;
lv_obj_t* qualityLabel = nullptr;
lv_obj_t* actionRing = nullptr;
lv_obj_t* actionButton = nullptr;
lv_obj_t* actionLabel = nullptr;
lv_obj_t* unknownControls = nullptr;
lv_obj_t* unknownStart = nullptr;
lv_obj_t* unknownStop = nullptr;
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

void showUnknownControls(bool show) {
  if (show) {
    lv_obj_add_flag(actionRing, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(unknownControls, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_clear_flag(actionRing, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(unknownControls, LV_OBJ_FLAG_HIDDEN);
  }
}

void setAction(const char* label, uint32_t color, bool enabled) {
  showUnknownControls(false);
  lv_label_set_text(actionLabel, label);
  lv_obj_set_style_border_color(actionRing, lv_color_hex(color), 0);
  lv_obj_set_style_bg_color(actionButton,
                            lv_color_hex(enabled ? color : kPanel), 0);
  if (enabled) {
    lv_obj_clear_state(actionButton, LV_STATE_DISABLED);
  } else {
    lv_obj_add_state(actionButton, LV_STATE_DISABLED);
  }
}

void refresh() {
  const studio::DeviceRuntimeState runtime =
      studio::devices().runtimeState(instanceId);
  const auto* state = static_cast<const canon_ble::CanonBleState*>(
      studio::devices().specializedState(instanceId));

  if (runtime.link != studio::LinkState::Connected || state == nullptr) {
    const char* status = "DISCONNECTED";
    const char* detail = "SMARTPHONE BLE";
    if (runtime.link == studio::LinkState::Scanning) {
      status = "PAIRING...";
      detail = "SELECT OK ON CAMERA";
    } else if (runtime.link == studio::LinkState::Connecting) {
      status = "CONNECTING...";
      detail = "SECURE HANDSHAKE";
    } else if (state != nullptr && state->pairingRejected) {
      status = "PAIRING REJECTED";
      detail = "TRY AGAIN";
    }
    lv_label_set_text(statusLabel, status);
    lv_label_set_text(qualityLabel, detail);
    setAction("WAITING", kAccent, false);
    return;
  }

  if (state->phase != canon_ble::CanonBleState::Phase::Ready) {
    lv_label_set_text(statusLabel, "OPENING...");
    lv_label_set_text(qualityLabel, "REQUESTING CAMERA STATE");
    setAction("WAIT", kAccent, false);
    return;
  }

  using Recording = canon_ble::CanonBleState::Recording;
  if (state->recording == Recording::Starting) {
    lv_label_set_text(statusLabel, "STARTING...");
    lv_label_set_text(qualityLabel, "WAITING FOR CAMERA");
    setAction("WAIT", kAccent, false);
  } else if (state->recording == Recording::Stopping) {
    lv_label_set_text(statusLabel, "STOPPING...");
    lv_label_set_text(qualityLabel, "WAITING FOR CAMERA");
    setAction("WAIT", kAccent, false);
  } else if (state->recordingConfirmed &&
             state->recording == Recording::Recording) {
    lv_label_set_text(statusLabel, "RECORDING");
    lv_label_set_text(qualityLabel, "CAMERA CONFIRMED");
    setAction("STOP", kAccent, true);
  } else if (state->recordingConfirmed &&
             state->recording == Recording::Stopped) {
    lv_label_set_text(statusLabel, "READY");
    lv_label_set_text(qualityLabel, "CAMERA CONFIRMED");
    setAction("START", kReady, true);
  } else {
    lv_label_set_text(statusLabel,
                      state->lastCommandFailed ? "NO CONFIRMATION"
                                               : "CONNECTED");
    lv_label_set_text(qualityLabel, "STATE UNKNOWN");
    showUnknownControls(true);
  }
}

void performPrimaryAction() {
  if (studio::devices().runtimeState(instanceId).link !=
      studio::LinkState::Connected) {
    return;
  }
  const auto* state = static_cast<const canon_ble::CanonBleState*>(
      studio::devices().specializedState(instanceId));
  if (state == nullptr ||
      state->phase != canon_ble::CanonBleState::Phase::Ready ||
      state->commandPending) {
    return;
  }
  if (state->recordingConfirmed &&
      state->recording == canon_ble::CanonBleState::Recording::Recording) {
    enqueue(studio::CommandType::RecordStop);
  } else {
    enqueue(studio::CommandType::RecordStart);
  }
}

void onBack(lv_event_t*) {
  hide();
  ui::showDevices();
}

void onAction(lv_event_t*) { performPrimaryAction(); }

void onUnknownStart(lv_event_t*) {
  enqueue(studio::CommandType::RecordStart);
}

void onUnknownStop(lv_event_t*) {
  enqueue(studio::CommandType::RecordStop);
}

lv_obj_t* createUnknownButton(lv_obj_t* parent, const char* label,
                              uint32_t color, lv_event_cb_t callback) {
  lv_obj_t* button = lv_btn_create(parent);
  lv_obj_set_size(button, 78, 62);
  lv_obj_set_style_radius(button, 24, 0);
  lv_obj_set_style_bg_color(button, lv_color_hex(color), 0);
  lv_obj_set_style_shadow_width(button, 0, 0);
  lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* text = lv_label_create(button);
  lv_label_set_text(text, label);
  lv_obj_set_style_text_font(text, UI_FONT_16, 0);
  lv_obj_set_style_text_color(text, lv_color_hex(kText), 0);
  lv_obj_center(text);
  return button;
}

}  // namespace

void init() {
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
  lv_label_set_long_mode(titleLabel, LV_LABEL_LONG_DOT);
  lv_obj_set_width(titleLabel, 132);
  lv_obj_set_height(titleLabel, 20);
  lv_obj_set_style_text_align(titleLabel, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(titleLabel, UI_FONT_16, 0);
  lv_obj_align(titleLabel, LV_ALIGN_TOP_MID, 15, 29);

  statusLabel = lv_label_create(screen);
  lv_obj_set_style_text_font(statusLabel, UI_FONT_16, 0);
  lv_obj_set_style_text_color(statusLabel, lv_color_hex(kText), 0);
  lv_obj_align(statusLabel, LV_ALIGN_TOP_MID, 0, 62);

  qualityLabel = lv_label_create(screen);
  lv_obj_set_width(qualityLabel, 190);
  lv_obj_set_style_text_align(qualityLabel, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(qualityLabel, UI_FONT_14, 0);
  lv_obj_set_style_text_color(qualityLabel, lv_color_hex(kMuted), 0);
  lv_obj_align(qualityLabel, LV_ALIGN_TOP_MID, 0, 78);

  actionRing = lv_obj_create(screen);
  lv_obj_set_size(actionRing, 136, 136);
  lv_obj_align(actionRing, LV_ALIGN_BOTTOM_MID, 0, -4);
  lv_obj_set_style_radius(actionRing, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(actionRing, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(actionRing, 3, 0);
  lv_obj_set_style_pad_all(actionRing, 8, 0);
  lv_obj_clear_flag(actionRing, LV_OBJ_FLAG_SCROLLABLE);

  actionButton = lv_btn_create(actionRing);
  lv_obj_set_size(actionButton, 118, 118);
  lv_obj_center(actionButton);
  lv_obj_set_style_radius(actionButton, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_shadow_width(actionButton, 0, 0);
  lv_obj_add_event_cb(actionButton, onAction, LV_EVENT_CLICKED, nullptr);
  actionLabel = lv_label_create(actionButton);
  lv_obj_set_style_text_align(actionLabel, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(actionLabel, UI_FONT_16, 0);
  lv_obj_set_style_text_color(actionLabel, lv_color_hex(kText), 0);
  lv_obj_center(actionLabel);

  unknownControls = lv_obj_create(screen);
  lv_obj_set_size(unknownControls, 180, 82);
  lv_obj_align(unknownControls, LV_ALIGN_BOTTOM_MID, 0, -34);
  lv_obj_set_style_bg_opa(unknownControls, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(unknownControls, 0, 0);
  lv_obj_set_style_pad_all(unknownControls, 0, 0);
  lv_obj_clear_flag(unknownControls, LV_OBJ_FLAG_SCROLLABLE);
  unknownStart =
      createUnknownButton(unknownControls, "START", kReady, onUnknownStart);
  lv_obj_align(unknownStart, LV_ALIGN_LEFT_MID, 4, 0);
  unknownStop =
      createUnknownButton(unknownControls, "STOP", kAccent, onUnknownStop);
  lv_obj_align(unknownStop, LV_ALIGN_RIGHT_MID, -4, 0);
  lv_obj_add_flag(unknownControls, LV_OBJ_FLAG_HIDDEN);
}

void show(studio::InstanceId id) {
  instanceId = id;
  const studio::DeviceRecord* record = studio::devices().find(id);
  lv_label_set_text(titleLabel, record != nullptr ? record->displayName : "");
  visible = studio::devices().activate(id);
  lastRefreshMs = 0;
  refresh();
  lv_scr_load(screen);
}

void hide() {
  if (visible) {
    studio::devices().deactivate();
  }
  visible = false;
  instanceId = studio::kInvalidInstanceId;
}

bool active() { return visible; }

void tick() {
  const uint32_t now = millis();
  if (now - lastRefreshMs >= 200) {
    lastRefreshMs = now;
    refresh();
  }
}

void handleShortPress() { performPrimaryAction(); }

}  // namespace canon_ble_ui
