#include "ui.h"

#include <Arduino.h>
#include <lvgl.h>

#include <cstdint>
#include <cstdio>

#include "core/device_manager.h"
#include "fonts/ui_fonts.h"
#include "shark_ui.h"

namespace ui {

namespace {

constexpr uint32_t kColBg = 0x05070A;
constexpr uint32_t kColPanel = 0x12161D;
constexpr uint32_t kColAccent = 0x35C7F2;
constexpr uint32_t kColText = 0xF3F4F6;
constexpr uint32_t kColMuted = 0x8A94A6;
constexpr uint32_t kColDanger = 0xF26D6D;

enum class Screen : uint8_t { Home, Devices };

Screen screen = Screen::Home;
lv_obj_t* scrHome = nullptr;
lv_obj_t* scrDevices = nullptr;
lv_obj_t* homeStatus = nullptr;
lv_obj_t* deviceList = nullptr;
lv_obj_t* addButton = nullptr;

lv_obj_t* deviceModal = nullptr;
lv_obj_t* deviceModalTitle = nullptr;
lv_obj_t* enabledSwitch = nullptr;
lv_obj_t* removeButton = nullptr;
lv_obj_t* renameOverlay = nullptr;
lv_obj_t* renameText = nullptr;
lv_obj_t* renameKeyboard = nullptr;

studio::InstanceId managedInstance = studio::kInvalidInstanceId;
uint32_t lastRefreshMs = 0;
bool removeArmed = false;

void styleScreen(lv_obj_t* object) {
  lv_obj_set_style_bg_color(object, lv_color_hex(kColBg), 0);
  lv_obj_set_style_text_color(object, lv_color_hex(kColText), 0);
  lv_obj_clear_flag(object, LV_OBJ_FLAG_SCROLLABLE);
}

lv_obj_t* makeButton(lv_obj_t* parent, const char* text, lv_event_cb_t callback,
                     uint32_t color = kColPanel) {
  lv_obj_t* button = lv_btn_create(parent);
  lv_obj_set_style_bg_color(button, lv_color_hex(color), 0);
  lv_obj_set_style_radius(button, 8, 0);
  lv_obj_set_style_shadow_width(button, 0, 0);
  lv_obj_t* label = lv_label_create(button);
  lv_label_set_text(label, text);
  lv_obj_set_style_text_font(label, UI_FONT_14, 0);
  lv_obj_set_style_text_color(label, lv_color_hex(kColText), 0);
  lv_obj_center(label);
  if (callback != nullptr) {
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, nullptr);
  }
  return button;
}

studio::InstanceId eventInstance(lv_event_t* event) {
  return static_cast<studio::InstanceId>(
      reinterpret_cast<uintptr_t>(lv_event_get_user_data(event)));
}

void closeDeviceModal() {
  lv_obj_add_flag(deviceModal, LV_OBJ_FLAG_HIDDEN);
  removeArmed = false;
  if (removeButton != nullptr) {
    lv_label_set_text(lv_obj_get_child(removeButton, 0), "Remove");
  }
  managedInstance = studio::kInvalidInstanceId;
}

void closeRename() {
  lv_obj_add_flag(renameOverlay, LV_OBJ_FLAG_HIDDEN);
  lv_keyboard_set_textarea(renameKeyboard, nullptr);
}

void refreshHome() {
  char status[32];
  snprintf(status, sizeof(status), "%u configured",
           static_cast<unsigned>(studio::devices().count()));
  lv_label_set_text(homeStatus, status);
}

const char* linkText(studio::LinkState link) {
  switch (link) {
    case studio::LinkState::Scanning:
      return "scanning";
    case studio::LinkState::Connecting:
      return "connecting";
    case studio::LinkState::Connected:
      return "connected";
    case studio::LinkState::Disconnected:
      return "ready";
  }
  return "ready";
}

void onOpenDevice(lv_event_t* event) {
  const studio::InstanceId instanceId = eventInstance(event);
  const studio::DeviceRecord* record = studio::devices().find(instanceId);
  if (record == nullptr || !record->enabled ||
      record->driverId != studio::DriverId::SharkNanoII) {
    return;
  }
  shark_ui::show(instanceId);
}

void onOpenManage(lv_event_t* event) {
  managedInstance = eventInstance(event);
  const studio::DeviceRecord* record = studio::devices().find(managedInstance);
  if (record == nullptr) {
    managedInstance = studio::kInvalidInstanceId;
    return;
  }
  lv_label_set_text(deviceModalTitle, record->displayName);
  removeArmed = false;
  lv_label_set_text(lv_obj_get_child(removeButton, 0), "Remove");
  if (record->enabled) {
    lv_obj_add_state(enabledSwitch, LV_STATE_CHECKED);
  } else {
    lv_obj_clear_state(enabledSwitch, LV_STATE_CHECKED);
  }
  lv_obj_clear_flag(deviceModal, LV_OBJ_FLAG_HIDDEN);
}

void refreshDevices() {
  lv_obj_clean(deviceList);
  bool hasShark = false;
  for (size_t i = 0; i < studio::devices().count(); ++i) {
    const studio::DeviceRecord* record = studio::devices().at(i);
    if (record == nullptr) {
      continue;
    }
    hasShark |= record->driverId == studio::DriverId::SharkNanoII;
    void* userData = reinterpret_cast<void*>(static_cast<uintptr_t>(record->instanceId));

    lv_obj_t* row = lv_obj_create(deviceList);
    lv_obj_set_size(row, lv_pct(100), 48);
    lv_obj_set_style_bg_color(row, lv_color_hex(kColPanel), 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_radius(row, 7, 0);
    lv_obj_set_style_pad_all(row, 4, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* open = lv_btn_create(row);
    lv_obj_set_size(open, 142, 40);
    lv_obj_align(open, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_opa(open, LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_width(open, 0, 0);
    lv_obj_add_event_cb(open, onOpenDevice, LV_EVENT_CLICKED, userData);

    lv_obj_t* name = lv_label_create(open);
    lv_label_set_text(name, record->displayName);
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    lv_obj_set_width(name, 128);
    lv_obj_set_style_text_font(name, UI_FONT_14, 0);
    lv_obj_set_style_text_color(
        name, lv_color_hex(record->enabled ? kColText : kColMuted), 0);
    lv_obj_align(name, LV_ALIGN_TOP_LEFT, 0, 1);

    lv_obj_t* detail = lv_label_create(open);
    const studio::DeviceRuntimeState runtime =
        studio::devices().runtimeState(record->instanceId);
    lv_label_set_text(detail, record->enabled ? linkText(runtime.link) : "disabled");
    lv_obj_set_style_text_font(detail, UI_FONT_14, 0);
    lv_obj_set_style_text_color(detail, lv_color_hex(kColMuted), 0);
    lv_obj_align(detail, LV_ALIGN_BOTTOM_LEFT, 0, -1);

    lv_obj_t* manage = makeButton(row, LV_SYMBOL_SETTINGS, nullptr);
    lv_obj_set_size(manage, 34, 34);
    lv_obj_align(manage, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_event_cb(manage, onOpenManage, LV_EVENT_CLICKED, userData);
  }
  if (hasShark) {
    lv_obj_add_flag(addButton, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_clear_flag(addButton, LV_OBJ_FLAG_HIDDEN);
  }
}

void onShowDevices(lv_event_t*) { showDevices(); }
void onShowHome(lv_event_t*) { showHome(); }
void onCloseModal(lv_event_t*) { closeDeviceModal(); }

void onAddDevice(lv_event_t*) {
  studio::InstanceId instanceId = studio::kInvalidInstanceId;
  studio::devices().add(studio::DriverId::SharkNanoII, "Shark Nano II", instanceId);
  refreshDevices();
  refreshHome();
}

void onEnabledChanged(lv_event_t* event) {
  if (managedInstance == studio::kInvalidInstanceId) {
    return;
  }
  studio::devices().setEnabled(
      managedInstance,
      lv_obj_has_state(lv_event_get_target(event), LV_STATE_CHECKED));
  refreshDevices();
}

void onRepair(lv_event_t*) {
  if (managedInstance != studio::kInvalidInstanceId) {
    studio::devices().clearPairing(managedInstance);
  }
  closeDeviceModal();
  refreshDevices();
}

void onRemove(lv_event_t*) {
  if (!removeArmed) {
    removeArmed = true;
    lv_label_set_text(deviceModalTitle, "Remove device?");
    lv_label_set_text(lv_obj_get_child(removeButton, 0), "Confirm");
    return;
  }
  if (managedInstance != studio::kInvalidInstanceId) {
    studio::devices().remove(managedInstance);
  }
  closeDeviceModal();
  refreshDevices();
  refreshHome();
}

void onOpenRename(lv_event_t*) {
  const studio::DeviceRecord* record = studio::devices().find(managedInstance);
  if (record == nullptr) {
    return;
  }
  lv_textarea_set_text(renameText, record->displayName);
  lv_keyboard_set_textarea(renameKeyboard, renameText);
  lv_obj_clear_flag(renameOverlay, LV_OBJ_FLAG_HIDDEN);
}

void onSaveRename(lv_event_t*) {
  if (managedInstance != studio::kInvalidInstanceId) {
    studio::devices().rename(managedInstance, lv_textarea_get_text(renameText));
  }
  closeRename();
  closeDeviceModal();
  refreshDevices();
}

void onCancelRename(lv_event_t*) { closeRename(); }

void buildHome() {
  scrHome = lv_obj_create(nullptr);
  styleScreen(scrHome);

  lv_obj_t* title = lv_label_create(scrHome);
  lv_label_set_text(title, "Studio Remote");
  lv_obj_set_style_text_font(title, UI_FONT_20, 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 30);

  lv_obj_t* devicesButton = makeButton(scrHome, "Devices", onShowDevices, kColAccent);
  lv_obj_set_size(devicesButton, 170, 42);
  lv_obj_align(devicesButton, LV_ALIGN_TOP_MID, 0, 66);

  const char* pending[] = {"Groups  ·  Soon", "Scenes  ·  Soon", "Portal  ·  Soon"};
  for (int i = 0; i < 3; ++i) {
    lv_obj_t* label = lv_label_create(scrHome);
    lv_label_set_text(label, pending[i]);
    lv_obj_set_style_text_font(label, UI_FONT_14, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(kColMuted), 0);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 120 + i * 24);
  }

  homeStatus = lv_label_create(scrHome);
  lv_obj_set_style_text_font(homeStatus, UI_FONT_14, 0);
  lv_obj_set_style_text_color(homeStatus, lv_color_hex(kColMuted), 0);
  lv_obj_align(homeStatus, LV_ALIGN_BOTTOM_MID, 0, -18);
}

void buildDevices() {
  scrDevices = lv_obj_create(nullptr);
  styleScreen(scrDevices);

  lv_obj_t* back = makeButton(scrDevices, LV_SYMBOL_LEFT, onShowHome);
  lv_obj_set_size(back, 34, 30);
  lv_obj_align(back, LV_ALIGN_TOP_LEFT, 18, 10);

  lv_obj_t* title = lv_label_create(scrDevices);
  lv_label_set_text(title, "Devices");
  lv_obj_set_style_text_font(title, UI_FONT_16, 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 17);

  deviceList = lv_obj_create(scrDevices);
  lv_obj_set_size(deviceList, 200, 142);
  lv_obj_align(deviceList, LV_ALIGN_TOP_MID, 0, 48);
  lv_obj_set_style_bg_opa(deviceList, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(deviceList, 0, 0);
  lv_obj_set_style_pad_all(deviceList, 2, 0);
  lv_obj_set_style_pad_row(deviceList, 5, 0);
  lv_obj_set_flex_flow(deviceList, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scroll_dir(deviceList, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(deviceList, LV_SCROLLBAR_MODE_OFF);

  addButton = makeButton(scrDevices, "+ Add device", onAddDevice, kColAccent);
  lv_obj_set_size(addButton, 140, 34);
  lv_obj_align(addButton, LV_ALIGN_BOTTOM_MID, 0, -14);
}

void buildDeviceModal() {
  deviceModal = lv_obj_create(lv_layer_top());
  lv_obj_set_size(deviceModal, 236, 236);
  lv_obj_center(deviceModal);
  lv_obj_set_style_radius(deviceModal, 118, 0);
  lv_obj_set_style_bg_color(deviceModal, lv_color_hex(kColBg), 0);
  lv_obj_set_style_border_color(deviceModal, lv_color_hex(kColAccent), 0);
  lv_obj_set_style_text_color(deviceModal, lv_color_hex(kColText), 0);
  lv_obj_clear_flag(deviceModal, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(deviceModal, LV_OBJ_FLAG_HIDDEN);

  deviceModalTitle = lv_label_create(deviceModal);
  lv_obj_set_width(deviceModalTitle, 160);
  lv_label_set_long_mode(deviceModalTitle, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_align(deviceModalTitle, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(deviceModalTitle, UI_FONT_16, 0);
  lv_obj_align(deviceModalTitle, LV_ALIGN_TOP_MID, 0, 20);

  lv_obj_t* rename = makeButton(deviceModal, "Rename", onOpenRename);
  lv_obj_set_size(rename, 130, 34);
  lv_obj_align(rename, LV_ALIGN_TOP_MID, 0, 54);

  lv_obj_t* enabledLabel = lv_label_create(deviceModal);
  lv_label_set_text(enabledLabel, "Enabled");
  lv_obj_set_style_text_font(enabledLabel, UI_FONT_14, 0);
  lv_obj_align(enabledLabel, LV_ALIGN_LEFT_MID, 34, -2);
  enabledSwitch = lv_switch_create(deviceModal);
  lv_obj_set_size(enabledSwitch, 44, 24);
  lv_obj_align(enabledSwitch, LV_ALIGN_RIGHT_MID, -34, -2);
  lv_obj_add_event_cb(enabledSwitch, onEnabledChanged, LV_EVENT_VALUE_CHANGED, nullptr);

  lv_obj_t* repair = makeButton(deviceModal, "Forget pairing", onRepair);
  lv_obj_set_size(repair, 130, 32);
  lv_obj_align(repair, LV_ALIGN_CENTER, 0, 42);

  removeButton = makeButton(deviceModal, "Remove", onRemove, kColDanger);
  lv_obj_set_size(removeButton, 82, 32);
  lv_obj_align(removeButton, LV_ALIGN_BOTTOM_LEFT, 24, -16);
  lv_obj_t* close = makeButton(deviceModal, "Close", onCloseModal, kColAccent);
  lv_obj_set_size(close, 82, 32);
  lv_obj_align(close, LV_ALIGN_BOTTOM_RIGHT, -24, -16);
}

void buildRenameOverlay() {
  renameOverlay = lv_obj_create(lv_layer_top());
  lv_obj_set_size(renameOverlay, 236, 236);
  lv_obj_center(renameOverlay);
  lv_obj_set_style_radius(renameOverlay, 118, 0);
  lv_obj_set_style_bg_color(renameOverlay, lv_color_hex(kColBg), 0);
  lv_obj_set_style_border_color(renameOverlay, lv_color_hex(kColAccent), 0);
  lv_obj_clear_flag(renameOverlay, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(renameOverlay, LV_OBJ_FLAG_HIDDEN);

  renameText = lv_textarea_create(renameOverlay);
  lv_obj_set_size(renameText, 176, 38);
  lv_obj_align(renameText, LV_ALIGN_TOP_MID, 0, 14);
  lv_textarea_set_max_length(renameText, studio::kDeviceNameCapacity - 1);
  lv_textarea_set_one_line(renameText, true);

  lv_obj_t* save = makeButton(renameOverlay, "Save", onSaveRename, kColAccent);
  lv_obj_set_size(save, 72, 30);
  lv_obj_align(save, LV_ALIGN_TOP_LEFT, 36, 57);
  lv_obj_t* cancel = makeButton(renameOverlay, "Cancel", onCancelRename);
  lv_obj_set_size(cancel, 72, 30);
  lv_obj_align(cancel, LV_ALIGN_TOP_RIGHT, -36, 57);

  renameKeyboard = lv_keyboard_create(renameOverlay);
  lv_obj_set_size(renameKeyboard, 220, 130);
  lv_obj_align(renameKeyboard, LV_ALIGN_BOTTOM_MID, 0, -2);
}

}  // namespace

void init() {
  buildHome();
  buildDevices();
  buildDeviceModal();
  buildRenameOverlay();
  shark_ui::init();
  refreshHome();
  refreshDevices();
  lv_scr_load(scrHome);
}

void tick() {
  if (shark_ui::active()) {
    shark_ui::tick();
    return;
  }
  const uint32_t now = millis();
  if (now - lastRefreshMs < 500) {
    return;
  }
  lastRefreshMs = now;
  if (screen == Screen::Home) {
    refreshHome();
  }
}

void handleShortPress() {
  if (shark_ui::active()) {
    shark_ui::handleShortPress();
  } else if (!lv_obj_has_flag(renameOverlay, LV_OBJ_FLAG_HIDDEN)) {
    closeRename();
  } else if (!lv_obj_has_flag(deviceModal, LV_OBJ_FLAG_HIDDEN)) {
    closeDeviceModal();
  } else if (screen == Screen::Devices) {
    showHome();
  }
}

void showHome() {
  if (shark_ui::active()) {
    shark_ui::hide();
  }
  closeDeviceModal();
  closeRename();
  screen = Screen::Home;
  refreshHome();
  lv_scr_load(scrHome);
}

void showDevices() {
  if (shark_ui::active()) {
    shark_ui::hide();
  }
  closeDeviceModal();
  closeRename();
  screen = Screen::Devices;
  refreshDevices();
  lv_scr_load(scrDevices);
}

}  // namespace ui
