#include "ui.h"

#include <Arduino.h>
#include <lvgl.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "assets/ui_icons.h"
#include "assets/about_logo.h"
#include "build_info.h"
#include "core/device_manager.h"
#include "core/factory_reset.h"
#include "core/panel_settings.h"
#include "core/driver_catalog.h"
#include "core/scene_service.h"
#include "core/system_info.h"
#include "driver_config.h"
#include "scene_ui.h"
#include "ui/picker_shell.h"
#include "ui/round_page.h"
#include "ui/title_marquee.h"
#include "portal_service.h"
#if CONFIG_DRIVER_CANON_BLE
#include "devices/canon_ble/ui.h"
#endif
#if CONFIG_DRIVER_CANON_TRIGGER
#include "devices/canon_trigger/ui.h"
#endif
#include "fonts/ui_fonts.h"
#include "haptic_feedback.h"
#if CONFIG_DRIVER_SHARK_NANO_II
#include "devices/shark_nano_ii/ui.h"
#endif
#if CONFIG_DRIVER_TASCAM_X8
#include "devices/tascam_x8/ui.h"
#endif
#if CONFIG_DRIVER_HOME_ASSISTANT
#include "devices/home_assistant/ui.h"
#endif
#if CONFIG_DRIVER_APUTURE_LIGHT
#include "devices/aputure_light/ui.h"
#endif
#if CONFIG_DRIVER_ZHIYUN_LIGHT
#include "devices/zhiyun_light/ui.h"
#endif
#if CONFIG_DRIVER_GOPRO
#include "devices/gopro/ui.h"
#endif
#if CONFIG_DRIVER_INSTA360
#include "devices/insta360/ui.h"
#endif
#if CONFIG_DRIVER_DJI_OSMO
#include "devices/dji_osmo/ui.h"
#endif
#if CONFIG_DRIVER_SONY_CAMERA
#include "devices/action_camera_research/ui.h"
#endif
#if CONFIG_DRIVER_PHONE_CAMERA
#include "devices/phone_camera/ui.h"
#endif

namespace ui {

void showPortal();

namespace {

constexpr uint32_t kColBg = 0x05070A;
constexpr uint32_t kColPanel = 0x12161D;
constexpr uint32_t kColAccent = 0x35C7F2;
constexpr uint32_t kColText = 0xF3F4F6;
constexpr uint32_t kColMuted = 0x8A94A6;
constexpr uint32_t kColDanger = 0xF26D6D;

struct DeviceUiHooks {
  constexpr DeviceUiHooks(
      studio::DriverId nextDriverId = studio::DriverId::Unknown,
      void (*nextShow)(studio::InstanceId) = nullptr,
      void (*nextHide)() = nullptr, void (*nextRelease)() = nullptr,
      bool (*nextActive)() = nullptr, void (*nextTick)() = nullptr,
      void (*nextShortPress)() = nullptr,
      void (*nextLongPress)() = nullptr)
      : driverId(nextDriverId),
        show(nextShow),
        hide(nextHide),
        release(nextRelease),
        active(nextActive),
        tick(nextTick),
        shortPress(nextShortPress),
        longPress(nextLongPress) {}

  studio::DriverId driverId;
  void (*show)(studio::InstanceId);
  void (*hide)();
  void (*release)();
  bool (*active)();
  void (*tick)();
  void (*shortPress)();
  void (*longPress)();
};

const DeviceUiHooks kDeviceUis[] = {
#if CONFIG_DRIVER_PHONE_CAMERA
    {studio::DriverId::PhoneCamera, phone_camera_ui::show,
     phone_camera_ui::hide, phone_camera_ui::release, phone_camera_ui::active,
     phone_camera_ui::tick, phone_camera_ui::handleShortPress,
     phone_camera_ui::handleLongPress},
#endif
#if CONFIG_DRIVER_INSTA360
    {studio::DriverId::Insta360, insta360_ui::show, insta360_ui::hide,
     insta360_ui::release, insta360_ui::active, insta360_ui::tick,
     insta360_ui::handleShortPress, insta360_ui::handleLongPress},
#endif
#if CONFIG_DRIVER_DJI_OSMO
    {studio::DriverId::DjiOsmo, dji_osmo_ui::show, dji_osmo_ui::hide,
     dji_osmo_ui::release, dji_osmo_ui::active, dji_osmo_ui::tick,
     dji_osmo_ui::handleShortPress, dji_osmo_ui::handleLongPress},
#endif
#if CONFIG_DRIVER_SONY_CAMERA
    {studio::DriverId::SonyCamera, action_camera_research_ui::show,
     action_camera_research_ui::hide, action_camera_research_ui::release,
     action_camera_research_ui::active, nullptr, nullptr,
     action_camera_research_ui::handleLongPress},
#endif
#if CONFIG_DRIVER_GOPRO
    {studio::DriverId::GoPro, gopro_ui::show, gopro_ui::hide,
     gopro_ui::release, gopro_ui::active, gopro_ui::tick,
     gopro_ui::handleShortPress, gopro_ui::handleLongPress},
#endif
#if CONFIG_DRIVER_ZHIYUN_LIGHT
    {studio::DriverId::ZhiyunLight, zhiyun_light_ui::show,
     zhiyun_light_ui::hide, zhiyun_light_ui::release,
     zhiyun_light_ui::active, zhiyun_light_ui::tick,
     zhiyun_light_ui::handleShortPress, zhiyun_light_ui::handleLongPress},
#endif
#if CONFIG_DRIVER_APUTURE_LIGHT
    {studio::DriverId::AputureLight, aputure_light_ui::show,
     aputure_light_ui::hide, aputure_light_ui::release,
     aputure_light_ui::active, aputure_light_ui::tick,
     aputure_light_ui::handleShortPress, aputure_light_ui::handleLongPress},
#endif
#if CONFIG_DRIVER_SHARK_NANO_II
    {studio::DriverId::SharkNanoII, shark_ui::show, shark_ui::hide,
     shark_ui::release, shark_ui::active, shark_ui::tick,
     shark_ui::handleShortPress, shark_ui::handleLongPress},
#endif
#if CONFIG_DRIVER_CANON_TRIGGER
    {studio::DriverId::CanonTrigger, canon_trigger_ui::show,
     canon_trigger_ui::hide, canon_trigger_ui::release,
     canon_trigger_ui::active, canon_trigger_ui::tick,
     canon_trigger_ui::handleShortPress, canon_trigger_ui::handleLongPress},
#endif
#if CONFIG_DRIVER_CANON_BLE
    {studio::DriverId::CanonBle, canon_ble_ui::show, canon_ble_ui::hide,
     canon_ble_ui::release, canon_ble_ui::active, canon_ble_ui::tick,
     canon_ble_ui::handleShortPress, canon_ble_ui::handleLongPress},
#endif
#if CONFIG_DRIVER_TASCAM_X8
    {studio::DriverId::TascamX8, tascam_x8_ui::show, tascam_x8_ui::hide,
     tascam_x8_ui::release, tascam_x8_ui::active, tascam_x8_ui::tick,
     tascam_x8_ui::handleShortPress, tascam_x8_ui::handleLongPress},
#endif
#if CONFIG_DRIVER_HOME_ASSISTANT
    {studio::DriverId::HomeAssistant, home_assistant_ui::show,
     home_assistant_ui::hide, home_assistant_ui::release,
     home_assistant_ui::active, home_assistant_ui::tick,
     home_assistant_ui::handleShortPress, home_assistant_ui::handleLongPress},
#endif
    {},
};

const DeviceUiHooks* activeDeviceUi() {
  for (const DeviceUiHooks& hooks : kDeviceUis) {
    if (hooks.active != nullptr && hooks.active()) return &hooks;
  }
  return nullptr;
}

const DeviceUiHooks* deviceUi(studio::DriverId driverId) {
  for (const DeviceUiHooks& hooks : kDeviceUis) {
    if (hooks.driverId == driverId) return &hooks;
  }
  return nullptr;
}

void hideDeviceUis() {
  for (const DeviceUiHooks& hooks : kDeviceUis) {
    if (hooks.hide != nullptr && hooks.active != nullptr && hooks.active()) {
      hooks.hide();
    }
  }
}

enum class Screen : uint8_t { Home, Devices, Portal, Settings };
enum class SettingsView : uint8_t { Menu, Wifi, About, SystemInfo, FactoryReset };

Screen screen = Screen::Home;
lv_obj_t* scrHome = nullptr;
lv_obj_t* scrDevices = nullptr;
lv_obj_t* scrPortal = nullptr;
lv_obj_t* scrSettings = nullptr;
lv_obj_t* settingsScreens[5] = {};
lv_obj_t* settingsHeaders[5] = {};
lv_obj_t* settingsHeader = nullptr;
lv_obj_t* settingsMenuList = nullptr;
lv_obj_t* aboutContent = nullptr;
lv_obj_t* systemInfoLabel = nullptr;
lv_obj_t* factoryResetButton = nullptr;
lv_obj_t* factoryResetProgress = nullptr;
lv_obj_t* factoryResetStatus = nullptr;
SettingsView settingsView = SettingsView::Menu;
bool factoryResetHolding = false;
bool factoryResetTriggered = false;
bool settingsHeaderNeedsRefresh = false;
uint32_t factoryResetStartedMs = 0;
lv_obj_t* deviceList = nullptr;
lv_obj_t* addButton = nullptr;
lv_obj_t* devicePagePrevious = nullptr;
lv_obj_t* devicePageNext = nullptr;
lv_obj_t* devicePageLabel = nullptr;
lv_obj_t* portalStatus = nullptr;
lv_obj_t* portalSsid = nullptr;
lv_obj_t* portalPassword = nullptr;
lv_obj_t* portalAddress = nullptr;
lv_obj_t* portalQr = nullptr;
lv_obj_t* portalExit = nullptr;
char portalQrData[96] = "";

lv_obj_t* deviceModal = nullptr;
lv_obj_t* deviceModalTitle = nullptr;
lv_obj_t* deviceModalBody = nullptr;
lv_obj_t* enabledSwitch = nullptr;
lv_obj_t* disconnectButton = nullptr;
lv_obj_t* removeButton = nullptr;
lv_obj_t* renameOverlay = nullptr;
lv_obj_t* renameText = nullptr;
lv_obj_t* renameKeypad = nullptr;
lv_obj_t* renamePageLabel = nullptr;
lv_obj_t* renameCaseLabel = nullptr;

studio::InstanceId managedInstance = studio::kInvalidInstanceId;
uint32_t lastRefreshMs = 0;
bool removeArmed = false;
bool disconnectArmed = false;
uint8_t renamePage = 0;
bool renameUpperCase = true;
size_t devicePage = 0;
RenameDoneFn renameDoneCallback = nullptr;
RenameCancelFn renameCancelCallback = nullptr;
uint8_t hapticErrorMask = 0;
studio::InstanceId hapticForegroundInstance = studio::kInvalidInstanceId;
bool hapticForegroundReady = false;
studio::SceneId hapticSceneId = studio::kInvalidSceneId;
studio::ScenePhase hapticScenePhase = studio::ScenePhase::Idle;

static const char* kRenameUpperPage0[] = {"A", "B", "C", "\n", "D", "E", "F", "\n",
                                         "G", "H", "I", ""};
static const char* kRenameUpperPage1[] = {"J", "K", "L", "\n", "M", "N", "O", "\n",
                                         "P", "Q", "R", ""};
static const char* kRenameUpperPage2[] = {"S", "T", "U", "\n", "V", "W", "X", "\n",
                                         "Y", "Z", "", ""};
static const char* kRenameLowerPage0[] = {"a", "b", "c", "\n", "d", "e", "f", "\n",
                                         "g", "h", "i", ""};
static const char* kRenameLowerPage1[] = {"j", "k", "l", "\n", "m", "n", "o", "\n",
                                         "p", "q", "r", ""};
static const char* kRenameLowerPage2[] = {"s", "t", "u", "\n", "v", "w", "x", "\n",
                                         "y", "z", "", ""};
static const char* kRenameSymbolPage[] = {"0", "1", "2", "3", "\n", "4", "5", "6", "7",
                                         "\n", "8", "9", "-", "_", ""};
constexpr uint8_t kRenamePageCount = 4;

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

lv_obj_t* makeModeTile(lv_obj_t* parent, const lv_img_dsc_t* icon, const char* caption,
                       bool enabled, lv_event_cb_t callback) {
  lv_obj_t* tile = lv_obj_create(parent);
  lv_obj_set_size(tile, 74, 74);
  lv_obj_set_style_bg_color(tile, lv_color_hex(kColPanel), 0);
  lv_obj_set_style_radius(tile, 16, 0);
  lv_obj_set_style_border_width(tile, 0, 0);
  lv_obj_set_style_pad_all(tile, 0, 0);
  lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
  if (enabled) {
    lv_obj_add_flag(tile, LV_OBJ_FLAG_CLICKABLE);
  } else {
    lv_obj_clear_flag(tile, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(tile, LV_OPA_70, 0);
  }
  if (callback != nullptr) {
    lv_obj_add_event_cb(tile, callback, LV_EVENT_CLICKED, nullptr);
  }

  lv_obj_t* image = lv_img_create(tile);
  lv_img_set_src(image, icon);
  // Keep Nano Banana Pro colors; dim unavailable modes instead of recoloring.
  lv_obj_set_style_img_opa(image, enabled ? LV_OPA_COVER : LV_OPA_50, 0);
  lv_obj_align(image, LV_ALIGN_TOP_MID, 0, 4);

  lv_obj_t* label = lv_label_create(tile);
  lv_label_set_text(label, caption);
  lv_obj_set_style_text_font(label, UI_FONT_14, 0);
  lv_obj_set_style_text_color(label, lv_color_hex(enabled ? kColText : kColMuted), 0);
  lv_obj_align(label, LV_ALIGN_BOTTOM_MID, 0, -5);
  return tile;
}

studio::InstanceId eventInstance(lv_event_t* event) {
  return static_cast<studio::InstanceId>(
      reinterpret_cast<uintptr_t>(lv_event_get_user_data(event)));
}

void destroyOverlay(lv_obj_t*& overlay) {
  if (overlay == nullptr) {
    return;
  }
  lv_obj_del(overlay);
  overlay = nullptr;
}

void closeDeviceModal() {
  removeArmed = false;
  disconnectArmed = false;
  managedInstance = studio::kInvalidInstanceId;
  destroyOverlay(deviceModal);
  deviceModalTitle = nullptr;
  deviceModalBody = nullptr;
  enabledSwitch = nullptr;
  disconnectButton = nullptr;
  removeButton = nullptr;
}

void closeRename() {
  destroyOverlay(renameOverlay);
  renameText = nullptr;
  renameKeypad = nullptr;
  renamePageLabel = nullptr;
  renameCaseLabel = nullptr;
  renameDoneCallback = nullptr;
  renameCancelCallback = nullptr;
}

void buildRenameOverlay();

void onAddPickerClosed() {
  if (addButton != nullptr) {
    lv_obj_clear_flag(addButton, LV_OBJ_FLAG_HIDDEN);
  }
}

void closeAddPicker() {
  if (picker_shell::active()) {
    picker_shell::hide();
  } else {
    onAddPickerClosed();
  }
}

size_t instanceCount(studio::DriverId driverId) {
  size_t instances = 0;
  for (size_t i = 0; i < studio::devices().count(); ++i) {
    const studio::DeviceRecord* record = studio::devices().at(i);
    instances += record != nullptr && record->driverId == driverId ? 1 : 0;
  }
  return instances;
}

bool driverCanAdd(const studio::DriverDescriptor* descriptor) {
  return descriptor != nullptr && descriptor->discoverable &&
         descriptor->id != studio::DriverId::HomeAssistant &&
         instanceCount(descriptor->id) < descriptor->maxInstances;
}

bool canAddDevice() {
  for (size_t i = 0; i < studio::DriverCatalog::count(); ++i) {
    if (driverCanAdd(studio::DriverCatalog::at(i))) return true;
  }
  return false;
}

constexpr size_t kDeviceRowsPerPage = 6;

size_t devicePageCount() {
  const size_t count = studio::devices().count();
  return count <= kDeviceRowsPerPage
             ? 1
             : (count + kDeviceRowsPerPage - 1) / kDeviceRowsPerPage;
}

size_t buildDeviceDisplayOrder(studio::InstanceId* order, size_t capacity) {
  const size_t count = studio::devices().count();
  if (order == nullptr || capacity < count) return 0;

  size_t next = 0;
  for (uint8_t pass = 0; pass < 2; ++pass) {
    const bool wantConnected = pass == 0;
    for (size_t i = 0; i < count; ++i) {
      const studio::DeviceRecord* record = studio::devices().at(i);
      if (record == nullptr) continue;
      const bool connected =
          record->enabled &&
          studio::devices().runtimeState(record->instanceId).link ==
              studio::LinkState::Connected;
      if (connected == wantConnected) {
        order[next++] = record->instanceId;
      }
    }
  }
  return next;
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
  if (record == nullptr || !record->enabled) {
    return;
  }
  if (studio::scenes().holdsLinks()) {
    return;
  }
  showDevice(instanceId);
}

void buildDeviceModal();
void buildRenameOverlay();
void onAddDevice(lv_event_t*);
void refreshDevices();
void refreshDeviceModal();

void onDevicePagePrevious(lv_event_t*) {
  if (devicePage > 0) {
    --devicePage;
    refreshDevices();
  }
}

void onDevicePageNext(lv_event_t*) {
  const size_t pageCount = devicePageCount();
  if (devicePage + 1 < pageCount) {
    ++devicePage;
    refreshDevices();
  }
}

void onOpenManage(lv_event_t* event) {
  managedInstance = eventInstance(event);
  const studio::DeviceRecord* record = studio::devices().find(managedInstance);
  if (record == nullptr) {
    managedInstance = studio::kInvalidInstanceId;
    return;
  }
  if (deviceModal == nullptr) {
    buildDeviceModal();
  }
  removeArmed = false;
  disconnectArmed = false;
  refreshDeviceModal();
  lv_obj_clear_flag(deviceModal, LV_OBJ_FLAG_HIDDEN);
}

void refreshDevices() {
  lv_obj_clean(deviceList);
  addButton = nullptr;
  studio::InstanceId displayOrder[CONFIG_MAX_DEVICE_INSTANCES] = {};
  const size_t displayCount =
      buildDeviceDisplayOrder(displayOrder, CONFIG_MAX_DEVICE_INSTANCES);
  const bool canAdd = canAddDevice();
  const size_t pageCount = devicePageCount();
  const bool paged = pageCount > 1;
  if (devicePage >= pageCount) devicePage = pageCount - 1;
  lv_obj_set_height(deviceList, paged ? 126 : 158);
  const size_t first = devicePage * kDeviceRowsPerPage;
  const size_t last = first + kDeviceRowsPerPage < displayCount
                          ? first + kDeviceRowsPerPage
                          : displayCount;
  for (size_t i = first; i < last; ++i) {
    const studio::DeviceRecord* record =
        studio::devices().find(displayOrder[i]);
    if (record == nullptr) {
      continue;
    }
    void* userData = reinterpret_cast<void*>(static_cast<uintptr_t>(record->instanceId));

    lv_obj_t* row = lv_obj_create(deviceList);
    lv_obj_set_size(row, lv_pct(100), 52);
    lv_obj_set_style_bg_color(row, lv_color_hex(kColPanel), 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_radius(row, 7, 0);
    lv_obj_set_style_pad_all(row, 4, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(row, onOpenDevice, LV_EVENT_CLICKED, userData);

    const studio::DeviceRuntimeState runtime =
        studio::devices().runtimeState(record->instanceId);
    lv_obj_t* name = lv_label_create(row);
    lv_label_set_text(name, record->displayName);
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    lv_obj_set_size(name, 132, 20);
    lv_obj_set_style_text_font(name, UI_FONT_14, 0);
    lv_obj_set_style_text_color(
        name, lv_color_hex(record->enabled ? kColText : kColMuted), 0);
    lv_obj_align(name, LV_ALIGN_TOP_LEFT, 4, 3);

    lv_obj_t* status = lv_label_create(row);
    lv_label_set_text(status,
                      record->enabled ? linkText(runtime.link) : "disabled");
    lv_label_set_long_mode(status, LV_LABEL_LONG_DOT);
    lv_obj_set_size(status, 132, 16);
    lv_obj_set_style_text_font(status, UI_FONT_12, 0);
    lv_obj_set_style_text_color(status, lv_color_hex(kColMuted), 0);
    lv_obj_align(status, LV_ALIGN_BOTTOM_LEFT, 4, -3);

    lv_obj_t* manage = lv_label_create(row);
    lv_label_set_text(manage, LV_SYMBOL_SETTINGS);
    lv_obj_set_size(manage, 36, 44);
    lv_obj_set_style_text_align(manage, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_top(manage, 13, 0);
    lv_obj_add_flag(manage, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(manage, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_event_cb(manage, onOpenManage, LV_EVENT_CLICKED, userData);
  }
  if (canAdd && devicePage + 1 == pageCount) {
    lv_obj_t* addRow = lv_obj_create(deviceList);
    lv_obj_set_size(addRow, lv_pct(100), studio::devices().count() > 0 ? 42 : 34);
    lv_obj_set_style_bg_opa(addRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(addRow, 0, 0);
    lv_obj_set_style_pad_all(addRow, 0, 0);
    lv_obj_clear_flag(addRow, LV_OBJ_FLAG_SCROLLABLE);
    addButton = makeButton(addRow, "+ Add device", onAddDevice, kColAccent);
    lv_obj_set_size(addButton, 140, 34);
    lv_obj_align(addButton, LV_ALIGN_BOTTOM_MID, 0, 0);
  }

  lv_obj_update_layout(deviceList);
  lv_obj_scroll_to_y(deviceList, 0, LV_ANIM_OFF);
  if (!paged) {
    lv_obj_add_flag(devicePagePrevious, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(devicePageLabel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(devicePageNext, LV_OBJ_FLAG_HIDDEN);
    return;
  }

  lv_obj_clear_flag(devicePagePrevious, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(devicePageLabel, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(devicePageNext, LV_OBJ_FLAG_HIDDEN);
  char pageText[16];
  std::snprintf(pageText, sizeof(pageText), "%u/%u",
                static_cast<unsigned>(devicePage + 1),
                static_cast<unsigned>(pageCount));
  lv_label_set_text(devicePageLabel, pageText);
  if (devicePage == 0) {
    lv_obj_add_state(devicePagePrevious, LV_STATE_DISABLED);
  } else {
    lv_obj_clear_state(devicePagePrevious, LV_STATE_DISABLED);
  }
  if (devicePage + 1 >= pageCount) {
    lv_obj_add_state(devicePageNext, LV_STATE_DISABLED);
  } else {
    lv_obj_clear_state(devicePageNext, LV_STATE_DISABLED);
  }
}

void releaseDeviceRows() {
  if (deviceList != nullptr) {
    lv_obj_clean(deviceList);
    addButton = nullptr;
  }
}

void onShowDevices(lv_event_t*) { showDevices(); }
void onShowHome(lv_event_t*) {
  haptic_feedback::request(haptic_feedback::Pattern::Back);
  showHome();
}
void onShowScenes(lv_event_t*) { scene_ui::show(); }
void onShowPortal(lv_event_t*) { showPortal(); }
void onShowSettings(lv_event_t*) { showSettings(); }
void onExitPortal(lv_event_t*) {
  haptic_feedback::request(haptic_feedback::Pattern::Back);
  portal::stop();
  showHome();
}
void onCloseModal(lv_event_t*) {
  haptic_feedback::request(haptic_feedback::Pattern::Back);
  closeDeviceModal();
}

void onDriverChosen(studio::DriverId driverId) {
  const studio::DriverDescriptor* descriptor =
      studio::DriverCatalog::find(driverId);
  if (!driverCanAdd(descriptor)) {
    return;
  }
  studio::InstanceId instanceId = studio::kInvalidInstanceId;
  if (studio::devices().beginAdd(descriptor->id, descriptor->model,
                                 instanceId) != studio::RegistryStatus::Ok) {
    refreshDevices();
    return;
  }
  showDevice(instanceId);
  if (!studio::devices().ownedBy(instanceId,
                                 studio::ConnectionOwner::Foreground)) {
    studio::devices().cancelPendingAdd(instanceId);
    showDevices();
  }
}

void onAddDevice(lv_event_t*) {
  picker_shell::Callbacks callbacks;
  callbacks.onDriverChosen = onDriverChosen;
  callbacks.onClosed = onAddPickerClosed;
  if (addButton != nullptr) {
    lv_obj_add_flag(addButton, LV_OBJ_FLAG_HIDDEN);
  }
  picker_shell::show(picker_shell::Mode::AddDriver, callbacks);
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

void onDisconnect(lv_event_t*) {
  if (managedInstance == studio::kInvalidInstanceId) {
    return;
  }
  const studio::CommandStatus status =
      studio::devices().disconnect(managedInstance, disconnectArmed);
  if (status == studio::CommandStatus::ConfirmationRequired) {
    disconnectArmed = true;
    lv_label_set_text(deviceModalTitle, "Recording active?");
    lv_label_set_text(lv_obj_get_child(disconnectButton, 0), "Confirm");
    return;
  }
  if (status == studio::CommandStatus::Busy) {
    lv_label_set_text(deviceModalTitle, "Device busy");
    return;
  }
  closeDeviceModal();
  refreshDevices();
}

void onRemove(lv_event_t*) {
  if (!removeArmed) {
    removeArmed = true;
    refreshDeviceModal();
    return;
  }
  if (managedInstance != studio::kInvalidInstanceId) {
    studio::devices().remove(managedInstance);
  }
  closeDeviceModal();
  refreshDevices();
}

void onCancelRemove(lv_event_t*) {
  removeArmed = false;
  refreshDeviceModal();
}

const char** renamePageMap() {
  if (renamePage == 0) {
    return renameUpperCase ? kRenameUpperPage0 : kRenameLowerPage0;
  }
  if (renamePage == 1) {
    return renameUpperCase ? kRenameUpperPage1 : kRenameLowerPage1;
  }
  if (renamePage == 2) {
    return renameUpperCase ? kRenameUpperPage2 : kRenameLowerPage2;
  }
  return kRenameSymbolPage;
}

void refreshRenameKeypad() {
  static const char* kPageNames[] = {"A-I", "J-R", "S-Z", "0-9"};
  lv_btnmatrix_set_map(renameKeypad, renamePageMap());
  lv_label_set_text(renamePageLabel, kPageNames[renamePage]);
  lv_label_set_text(renameCaseLabel, renameUpperCase ? "Aa" : "aA");
}

void stepRenamePage(int delta) {
  int page = static_cast<int>(renamePage) + delta;
  if (page < 0) {
    page += kRenamePageCount;
  } else if (page >= kRenamePageCount) {
    page -= kRenamePageCount;
  }
  renamePage = static_cast<uint8_t>(page);
  refreshRenameKeypad();
}

void onRenamePrevious(lv_event_t*) { stepRenamePage(-1); }

void onRenameNext(lv_event_t*) { stepRenamePage(1); }

void onRenameKeypadGesture(lv_event_t*) {
  lv_indev_t* input = lv_indev_get_act();
  if (input == nullptr) {
    return;
  }
  const lv_dir_t direction = lv_indev_get_gesture_dir(input);
  if (direction == LV_DIR_LEFT) {
    stepRenamePage(1);
  } else if (direction == LV_DIR_RIGHT) {
    stepRenamePage(-1);
  }
}

void onRenameCharacter(lv_event_t* event) {
  lv_obj_t* keypad = lv_event_get_target(event);
  const uint16_t button = lv_btnmatrix_get_selected_btn(keypad);
  if (button == LV_BTNMATRIX_BTN_NONE) {
    return;
  }
  const char* text = lv_btnmatrix_get_btn_text(keypad, button);
  if (text != nullptr && text[0] != '\0') {
    lv_textarea_add_text(renameText, text);
  }
}

void onRenameBackspace(lv_event_t*) { lv_textarea_del_char(renameText); }

void onRenameSpace(lv_event_t*) { lv_textarea_add_char(renameText, ' '); }

void onRenameCase(lv_event_t*) {
  renameUpperCase = !renameUpperCase;
  refreshRenameKeypad();
}

void onDeviceRenameDone(const char* name) {
  if (managedInstance != studio::kInvalidInstanceId && name != nullptr) {
    studio::devices().rename(managedInstance, name);
  }
  closeDeviceModal();
  refreshDevices();
}

void onOpenRename(lv_event_t*) {
  const studio::DeviceRecord* record = studio::devices().find(managedInstance);
  if (record == nullptr) {
    return;
  }
  releaseDeviceRows();
  promptRename(record->displayName, onDeviceRenameDone);
}

void onSaveRename(lv_event_t*) {
  const RenameDoneFn done = renameDoneCallback;
  char name[studio::kDeviceNameCapacity] = "";
  if (renameText != nullptr) {
    std::strncpy(name, lv_textarea_get_text(renameText), sizeof(name) - 1);
  }
  closeRename();
  if (done != nullptr) {
    done(name);
  }
}

void onCancelRename(lv_event_t*) {
  const RenameCancelFn cancel = renameCancelCallback;
  closeRename();
  if (cancel != nullptr) {
    cancel();
  } else if (screen == Screen::Devices) {
    refreshDevices();
  }
}

void destroySettingsScreen() {
  if (scrSettings != nullptr && lv_scr_act() == scrSettings && scrHome != nullptr) {
    lv_scr_load(scrHome);
  }
  for (lv_obj_t*& child : settingsScreens) {
    if (child != nullptr) {
      lv_obj_del(child);
      child = nullptr;
    }
  }
  for (lv_obj_t*& header : settingsHeaders) header = nullptr;
  scrSettings = nullptr;
  settingsHeader = nullptr;
  settingsMenuList = nullptr;
  aboutContent = nullptr;
  systemInfoLabel = nullptr;
  factoryResetButton = nullptr;
  factoryResetProgress = nullptr;
  factoryResetStatus = nullptr;
  factoryResetHolding = false;
  factoryResetTriggered = false;
  settingsHeaderNeedsRefresh = false;
}

lv_obj_t* settingsScreen(const char* title, lv_event_cb_t back) {
  scrSettings = lv_obj_create(nullptr);
  styleScreen(scrSettings);
  lv_obj_clear_flag(scrSettings, LV_OBJ_FLAG_SCROLLABLE);
  settingsHeader = lv_obj_create(scrSettings);
  lv_obj_set_size(settingsHeader, 240, 58);
  lv_obj_align(settingsHeader, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_style_bg_color(settingsHeader, lv_color_hex(kColBg), 0);
  lv_obj_set_style_bg_opa(settingsHeader, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(settingsHeader, 0, 0);
  lv_obj_set_style_pad_all(settingsHeader, 0, 0);
  lv_obj_clear_flag(settingsHeader, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(settingsHeader, LV_OBJ_FLAG_FLOATING);
  studio_ui::RoundPageHeaderOptions header;
  header.title = title;
  header.onBack = back;
  header.panelColor = kColPanel;
  header.textColor = kColText;
  studio_ui::createRoundPageHeader(settingsHeader, header);
  return scrSettings;
}

lv_obj_t* settingsRow(lv_obj_t* parent, const char* title, const char* detail,
                      lv_event_cb_t callback, uint32_t color = kColPanel) {
  lv_obj_t* row = lv_btn_create(parent);
  lv_obj_set_size(row, lv_pct(100), 32);
  lv_obj_set_style_bg_color(row, lv_color_hex(color), 0);
  lv_obj_set_style_radius(row, 8, 0);
  lv_obj_set_style_shadow_width(row, 0, 0);
  lv_obj_set_style_pad_all(row, 0, 0);
  if (callback != nullptr) lv_obj_add_event_cb(row, callback, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* label = lv_label_create(row);
  lv_label_set_text(label, title);
  lv_obj_set_style_text_font(label, UI_FONT_14, 0);
  lv_obj_align(label, LV_ALIGN_LEFT_MID, 10, detail != nullptr ? -6 : 0);
  if (detail != nullptr) {
    lv_obj_t* sub = lv_label_create(row);
    lv_label_set_text(sub, detail);
    lv_obj_set_style_text_font(sub, UI_FONT_14, 0);
    lv_obj_set_style_text_color(sub, lv_color_hex(kColMuted), 0);
    lv_obj_align(sub, LV_ALIGN_LEFT_MID, 10, 7);
  }
  if (callback != nullptr) {
    lv_obj_t* arrow = lv_label_create(row);
    lv_label_set_text(arrow, LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_font(arrow, UI_FONT_14, 0);
    lv_obj_align(arrow, LV_ALIGN_RIGHT_MID, -10, 0);
  }
  return row;
}

void showSettingsView(SettingsView view);

void onSettingsBack(lv_event_t*) {
  haptic_feedback::request(haptic_feedback::Pattern::Back);
  if (settingsView == SettingsView::Menu) showHome();
  else showSettingsView(SettingsView::Menu);
}

void onWifiSettings(lv_event_t*) { showSettingsView(SettingsView::Wifi); }
void onAbout(lv_event_t*) { showSettingsView(SettingsView::About); }
void onSystemInfo(lv_event_t*) { showSettingsView(SettingsView::SystemInfo); }
void onFactoryReset(lv_event_t*) { showSettingsView(SettingsView::FactoryReset); }

void onConfigureWifi(lv_event_t*) {
  destroySettingsScreen();
  showPortal();
}

void onHapticChanged(lv_event_t* event) {
  lv_obj_t* toggle = lv_event_get_target(event);
  const bool requested = lv_obj_has_state(toggle, LV_STATE_CHECKED);
  const bool previous = studio::panelSettings().get().hapticEnabled;
  if (!studio::panelSettings().setHapticEnabled(requested)) {
    if (previous) lv_obj_add_state(toggle, LV_STATE_CHECKED);
    else lv_obj_clear_state(toggle, LV_STATE_CHECKED);
    haptic_feedback::setEnabled(true);
    haptic_feedback::request(haptic_feedback::Pattern::Error);
    haptic_feedback::setEnabled(previous);
    return;
  }
  haptic_feedback::setEnabled(requested);
  if (requested) haptic_feedback::request(haptic_feedback::Pattern::Press);
}

void onFactoryResetPressed(lv_event_t*) {
  if (factoryResetTriggered) return;
  factoryResetHolding = true;
  factoryResetStartedMs = millis();
}

void onFactoryResetReleased(lv_event_t*) {
  if (factoryResetTriggered) return;
  factoryResetHolding = false;
  if (factoryResetProgress != nullptr) {
    lv_bar_set_value(factoryResetProgress, 0, LV_ANIM_OFF);
  }
  if (factoryResetStatus != nullptr) {
    lv_label_set_text(factoryResetStatus, "Hold for 3 seconds");
  }
}

void onSettingsContentScrolled(lv_event_t*) {
  settingsHeaderNeedsRefresh = true;
}

void buildSettingsMenu() {
  settingsScreen("Settings", onSettingsBack);
  lv_obj_t* list = lv_obj_create(scrSettings);
  settingsMenuList = list;
  lv_obj_set_size(list, 190, 162);
  lv_obj_align(list, LV_ALIGN_TOP_MID, 0, 58);
  lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(list, 0, 0);
  lv_obj_set_style_pad_all(list, 2, 0);
  lv_obj_set_style_pad_row(list, 3, 0);
  lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scroll_dir(list, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);
  lv_obj_add_event_cb(list, onSettingsContentScrolled, LV_EVENT_SCROLL, nullptr);
  settingsRow(list, "About", nullptr, onAbout);
  settingsRow(list, "Wi-Fi", nullptr, onWifiSettings);
  lv_obj_t* haptic = settingsRow(list, "Haptic feedback", nullptr, nullptr);
  lv_obj_t* toggle = lv_switch_create(haptic);
  lv_obj_set_size(toggle, 42, 22);
  lv_obj_align(toggle, LV_ALIGN_RIGHT_MID, -10, 0);
  if (studio::panelSettings().get().hapticEnabled) {
    lv_obj_add_state(toggle, LV_STATE_CHECKED);
  }
  lv_obj_add_event_cb(toggle, onHapticChanged, LV_EVENT_VALUE_CHANGED, nullptr);
  settingsRow(list, "System info", nullptr, onSystemInfo);
  settingsRow(list, "Factory reset", nullptr, onFactoryReset);
}

void buildWifiSettings() {
  settingsScreen("Wi-Fi", onSettingsBack);
  const portal::SavedWifiSummary saved = portal::savedWifiSummary();
  lv_obj_t* status = lv_label_create(scrSettings);
  lv_obj_set_width(status, 176);
  lv_obj_set_style_text_align(status, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(status, UI_FONT_16, 0);
  lv_obj_set_style_text_color(status, lv_color_hex(saved.configured ? kColAccent
                                                                    : kColMuted), 0);
  lv_label_set_text(status, saved.configured ? "SAVED NETWORK\nRADIO OFF"
                                              : "NOT CONFIGURED");
  lv_obj_align(status, LV_ALIGN_TOP_MID, 0, 72);
  lv_obj_t* ssid = lv_label_create(scrSettings);
  lv_obj_set_width(ssid, 174);
  lv_label_set_long_mode(ssid, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_align(ssid, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(ssid, UI_FONT_14, 0);
  lv_label_set_text(ssid, saved.configured ? saved.ssid : "No studio Wi-Fi saved");
  lv_obj_align(ssid, LV_ALIGN_TOP_MID, 0, 122);
  lv_obj_t* configure =
      makeButton(scrSettings, "OPEN PORTAL", onConfigureWifi, kColAccent);
  lv_obj_set_size(configure, 158, 34);
  lv_obj_align(configure, LV_ALIGN_BOTTOM_MID, 0, -38);
}

void buildAbout() {
  settingsScreen("About", onSettingsBack);
  lv_obj_t* content = lv_obj_create(scrSettings);
  aboutContent = content;
  lv_obj_set_size(content, 200, 162);
  lv_obj_align(content, LV_ALIGN_TOP_MID, 0, 58);
  lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(content, 0, 0);
  lv_obj_set_style_pad_all(content, 2, 0);
  lv_obj_set_scroll_dir(content, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(content, LV_SCROLLBAR_MODE_AUTO);
  lv_obj_add_event_cb(content, onSettingsContentScrolled, LV_EVENT_SCROLL, nullptr);

  lv_obj_t* logo = lv_img_create(content);
  lv_img_set_src(logo, &ui_about_logo);
  lv_obj_align(logo, LV_ALIGN_TOP_MID, 0, 2);
  lv_obj_t* description = lv_label_create(content);
  lv_label_set_text(description, "Bluetooth Low Energy\nEquipment Panel");
  lv_obj_set_width(description, 190);
  lv_obj_set_style_text_align(description, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(description, UI_FONT_14, 0);
  lv_obj_set_style_text_color(description, lv_color_hex(kColMuted), 0);
  lv_obj_align(description, LV_ALIGN_TOP_MID, 0, 64);
  char identity[96];
  std::snprintf(identity, sizeof(identity), "v%s\n%s\n%s  |  %s",
                build_info::kFirmwareVersion, portal::unitId(),
                build_info::kGitCommit,
                build_info::kGitDate);
  lv_obj_t* version = lv_label_create(content);
  lv_label_set_text(version, identity);
  lv_obj_set_width(version, 190);
  lv_obj_set_style_text_align(version, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(version, UI_FONT_14, 0);
  lv_obj_align(version, LV_ALIGN_TOP_MID, 0, 106);
  char detail[64];
  std::snprintf(detail, sizeof(detail), "%s\nApache-2.0",
                build_info::kHardware);
  lv_obj_t* hardware = lv_label_create(content);
  lv_label_set_text(hardware, detail);
  lv_obj_set_width(hardware, 180);
  lv_obj_set_style_text_align(hardware, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(hardware, UI_FONT_14, 0);
  lv_obj_set_style_text_color(hardware, lv_color_hex(kColMuted), 0);
  lv_obj_align(hardware, LV_ALIGN_TOP_MID, 0, 168);
}

void refreshSystemInfo() {
  if (systemInfoLabel == nullptr) return;
  const studio::SystemInfo info = studio::systemInfo();
  char text[220];
  std::snprintf(text, sizeof(text),
                "%s\n%s\n\nHeap free   %lu\nLargest     %lu\nMinimum     %lu\nBLE groups  %u\nWi-Fi       %s",
                build_info::kHardware, build_info::kGitCommit,
                static_cast<unsigned long>(info.freeHeap),
                static_cast<unsigned long>(info.largestFreeBlock),
                static_cast<unsigned long>(info.minimumFreeHeap),
                static_cast<unsigned>(info.activeBleGroups), info.wifiState);
  lv_label_set_text(systemInfoLabel, text);
}

void buildSystemInfo() {
  settingsScreen("System info", onSettingsBack);
  systemInfoLabel = lv_label_create(scrSettings);
  lv_obj_set_width(systemInfoLabel, 174);
  lv_obj_set_style_text_font(systemInfoLabel, UI_FONT_14, 0);
  lv_obj_set_style_text_color(systemInfoLabel, lv_color_hex(kColText), 0);
  lv_obj_align(systemInfoLabel, LV_ALIGN_TOP_LEFT, 38, 66);
  refreshSystemInfo();
}

void buildFactoryReset() {
  settingsScreen("Factory reset", onSettingsBack);
  lv_obj_t* warning = lv_label_create(scrSettings);
  lv_label_set_text(warning,
      "ERASE ALL SAVED DATA?\nDevices + scenes + networks\nBLE bonds + haptics + mesh keys\nReset mesh lights manually\nFirmware stays / cannot undo");
  lv_obj_set_width(warning, 220);
  lv_obj_set_style_text_align(warning, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(warning, UI_FONT_14, 0);
  lv_obj_set_style_text_line_space(warning, -2, 0);
  lv_obj_set_style_text_color(warning, lv_color_hex(kColText), 0);
  lv_obj_align(warning, LV_ALIGN_TOP_MID, 0, 60);
  factoryResetProgress = lv_bar_create(scrSettings);
  lv_obj_set_size(factoryResetProgress, 150, 5);
  lv_obj_align(factoryResetProgress, LV_ALIGN_BOTTOM_MID, 0, -58);
  lv_bar_set_range(factoryResetProgress, 0, 3000);
  lv_bar_set_value(factoryResetProgress, 0, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(factoryResetProgress, lv_color_hex(kColPanel), 0);
  lv_obj_set_style_bg_color(factoryResetProgress, lv_color_hex(kColDanger),
                            LV_PART_INDICATOR);
  factoryResetButton = makeButton(scrSettings, "HOLD TO RESET", nullptr, kColDanger);
  lv_obj_set_size(factoryResetButton, 126, 32);
  lv_obj_align(factoryResetButton, LV_ALIGN_BOTTOM_MID, 0, -22);
  lv_obj_add_event_cb(factoryResetButton, onFactoryResetPressed, LV_EVENT_PRESSED,
                      nullptr);
  lv_obj_add_event_cb(factoryResetButton, onFactoryResetReleased, LV_EVENT_RELEASED,
                      nullptr);
  lv_obj_add_event_cb(factoryResetButton, onFactoryResetReleased,
                      LV_EVENT_PRESS_LOST, nullptr);
  factoryResetStatus = lv_label_create(scrSettings);
  lv_label_set_text(factoryResetStatus, "Hold for 3 seconds");
  lv_obj_set_style_text_font(factoryResetStatus, UI_FONT_14, 0);
  lv_obj_set_style_text_color(factoryResetStatus, lv_color_hex(kColMuted), 0);
  lv_obj_align(factoryResetStatus, LV_ALIGN_TOP_MID, 0, 153);
}

void showSettingsView(SettingsView view) {
  factoryResetHolding = false;
  settingsView = view;
  screen = Screen::Settings;
  const size_t index = static_cast<size_t>(view);
  if (settingsScreens[index] == nullptr) {
    scrSettings = nullptr;
    switch (view) {
      case SettingsView::Menu: buildSettingsMenu(); break;
      case SettingsView::Wifi: buildWifiSettings(); break;
      case SettingsView::About: buildAbout(); break;
      case SettingsView::SystemInfo: buildSystemInfo(); break;
      case SettingsView::FactoryReset: buildFactoryReset(); break;
    }
    settingsScreens[index] = scrSettings;
    settingsHeaders[index] = settingsHeader;
  } else {
    scrSettings = settingsScreens[index];
    settingsHeader = settingsHeaders[index];
  }
  lv_obj_move_foreground(settingsHeader);
  lv_scr_load(scrSettings);
  lv_obj_invalidate(scrSettings);
  settingsHeaderNeedsRefresh = true;
}

void buildHome() {
  scrHome = lv_obj_create(nullptr);
  styleScreen(scrHome);

  studio_ui::RoundPageHeaderOptions header;
  header.title = "Ble(e)p";
  header.actionSymbol = LV_SYMBOL_SETTINGS;
  header.onAction = onShowSettings;
  header.titleFont = UI_FONT_20;
  header.panelColor = kColPanel;
  header.textColor = kColText;
  studio_ui::createRoundPageHeader(scrHome, header);

  lv_obj_t* grid = lv_obj_create(scrHome);
  lv_obj_set_size(grid, 164, 158);
  lv_obj_align(grid, LV_ALIGN_TOP_MID, 0, 50);
  lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(grid, 0, 0);
  lv_obj_set_style_pad_all(grid, 0, 0);
  lv_obj_set_style_pad_row(grid, 8, 0);
  lv_obj_set_style_pad_column(grid, 8, 0);
  lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);

  makeModeTile(grid, &ui_icon_devices, "Devices", true, onShowDevices);
  makeModeTile(grid, &ui_icon_groups, "Groups", false, nullptr);
  makeModeTile(grid, &ui_icon_scenes, "Scenes", true, onShowScenes);
  makeModeTile(grid, &ui_icon_portal, "Portal", true, onShowPortal);
}

void buildDevices() {
  scrDevices = lv_obj_create(nullptr);
  styleScreen(scrDevices);

  studio_ui::RoundPageHeaderOptions header;
  header.title = "Devices";
  header.onBack = onShowHome;
  header.panelColor = kColPanel;
  header.textColor = kColText;
  studio_ui::createRoundPageHeader(scrDevices, header);

  deviceList = lv_obj_create(scrDevices);
  lv_obj_set_size(deviceList, 200, 158);
  lv_obj_align(deviceList, LV_ALIGN_TOP_MID, 0,
               studio_ui::kRoundPageContentY);
  lv_obj_set_style_bg_opa(deviceList, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(deviceList, 0, 0);
  lv_obj_set_style_pad_all(deviceList, 2, 0);
  lv_obj_set_style_pad_row(deviceList, 5, 0);
  lv_obj_set_flex_flow(deviceList, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scroll_dir(deviceList, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(deviceList, LV_SCROLLBAR_MODE_OFF);

  devicePagePrevious =
      makeButton(scrDevices, LV_SYMBOL_LEFT, onDevicePagePrevious, kColPanel);
  lv_obj_set_size(devicePagePrevious, 34, 28);
  lv_obj_align(devicePagePrevious, LV_ALIGN_BOTTOM_MID, -48, -20);

  devicePageLabel = lv_label_create(scrDevices);
  lv_obj_set_style_text_font(devicePageLabel, UI_FONT_14, 0);
  lv_obj_set_style_text_color(devicePageLabel, lv_color_hex(kColMuted), 0);
  lv_obj_align(devicePageLabel, LV_ALIGN_BOTTOM_MID, 0, -26);

  devicePageNext =
      makeButton(scrDevices, LV_SYMBOL_RIGHT, onDevicePageNext, kColPanel);
  lv_obj_set_size(devicePageNext, 34, 28);
  lv_obj_align(devicePageNext, LV_ALIGN_BOTTOM_MID, 48, -20);
}

void buildPortal() {
  if (scrPortal != nullptr) return;
  scrPortal = lv_obj_create(nullptr);
  styleScreen(scrPortal);
  lv_obj_t* title = lv_label_create(scrPortal);
  lv_label_set_text(title, "PORTAL / LINK");
  lv_obj_set_style_text_font(title, UI_FONT_16, 0);
  lv_obj_set_style_text_color(title, lv_color_hex(kColAccent), 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);
  portalStatus = lv_label_create(scrPortal);
  lv_obj_set_width(portalStatus, 184);
  lv_obj_set_style_text_align(portalStatus, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(portalStatus, UI_FONT_14, 0);
  lv_obj_align(portalStatus, LV_ALIGN_TOP_MID, 0, 47);

  portalQr = lv_qrcode_create(scrPortal, 96, lv_color_hex(kColBg),
                              lv_color_hex(kColText));
  lv_obj_align(portalQr, LV_ALIGN_TOP_LEFT, 28, 72);

  portalSsid = lv_label_create(scrPortal);
  lv_obj_set_width(portalSsid, 82);
  lv_obj_set_style_text_align(portalSsid, LV_TEXT_ALIGN_LEFT, 0);
  lv_obj_set_style_text_font(portalSsid, UI_FONT_14, 0);
  lv_obj_set_style_text_color(portalSsid, lv_color_hex(kColText), 0);
  lv_obj_align(portalSsid, LV_ALIGN_TOP_LEFT, 134, 76);
  portalPassword = lv_label_create(scrPortal);
  lv_obj_set_width(portalPassword, 82);
  lv_obj_set_style_text_align(portalPassword, LV_TEXT_ALIGN_LEFT, 0);
  lv_obj_set_style_text_font(portalPassword, UI_FONT_14, 0);
  lv_obj_set_style_text_color(portalPassword, lv_color_hex(kColMuted), 0);
  lv_obj_align(portalPassword, LV_ALIGN_TOP_LEFT, 134, 124);
  portalAddress = lv_label_create(scrPortal);
  lv_label_set_text(portalAddress, "http://192.168.4.1");
  lv_obj_set_width(portalAddress, 180);
  lv_obj_set_style_text_align(portalAddress, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(portalAddress, UI_FONT_14, 0);
  lv_obj_set_style_text_color(portalAddress, lv_color_hex(kColAccent), 0);
  lv_obj_align(portalAddress, LV_ALIGN_TOP_MID, 0, 173);
  portalExit = makeButton(scrPortal, "EXIT PORTAL", onExitPortal, kColDanger);
  lv_obj_set_size(portalExit, 112, 30);
  lv_obj_align(portalExit, LV_ALIGN_BOTTOM_MID, 0, -8);
}

void refreshPortal() {
  if (scrPortal == nullptr) return;
  lv_label_set_text(portalStatus, portal::statusText());
  char text[64];
  std::snprintf(text, sizeof(text), "SSID\n%s", portal::ssid());
  lv_label_set_text(portalSsid, text);
  std::snprintf(text, sizeof(text), "PASS\n%s", portal::password());
  if (portal::password()[0] == '\0') {
    std::strncpy(text,
                 std::strncmp(portal::qrPayload(), "WIFI:", 5) == 0
                     ? "OPEN\nNETWORK" : "SAME LOCAL\nWI-FI",
                 sizeof(text) - 1);
    text[sizeof(text) - 1] = '\0';
  }
  lv_label_set_text(portalPassword, text);
  lv_label_set_text(portalAddress, portal::url());
  const char* qrPayload = portal::qrPayload();
  if (std::strncmp(portalQrData, qrPayload, sizeof(portalQrData)) != 0) {
    std::strncpy(portalQrData, qrPayload, sizeof(portalQrData) - 1);
    portalQrData[sizeof(portalQrData) - 1] = '\0';
    lv_qrcode_update(portalQr, portalQrData, std::strlen(portalQrData));
  }
  lv_obj_invalidate(portalExit);
}

void buildDeviceModal() {
  if (deviceModal != nullptr) {
    return;
  }
  deviceModal = lv_obj_create(lv_layer_top());
  lv_obj_set_size(deviceModal, 240, 240);
  lv_obj_center(deviceModal);
  lv_obj_set_style_radius(deviceModal, 120, 0);
  lv_obj_set_style_bg_color(deviceModal, lv_color_hex(kColBg), 0);
  lv_obj_set_style_border_width(deviceModal, 0, 0);
  lv_obj_set_style_text_color(deviceModal, lv_color_hex(kColText), 0);
  lv_obj_set_style_pad_all(deviceModal, 0, 0);
  lv_obj_clear_flag(deviceModal, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(deviceModal, LV_OBJ_FLAG_HIDDEN);

  studio_ui::RoundPageHeaderOptions header;
  header.backSymbol = LV_SYMBOL_CLOSE;
  header.onBack = onCloseModal;
  header.panelColor = kColPanel;
  header.textColor = kColText;
  deviceModalTitle =
      studio_ui::createRoundPageHeader(deviceModal, header).title;

  deviceModalBody = studio_ui::createRoundPageMenuBody(deviceModal, 5);
  refreshDeviceModal();
}

void refreshDeviceModal() {
  if (deviceModalBody == nullptr || deviceModalTitle == nullptr) return;
  lv_obj_clean(deviceModalBody);
  enabledSwitch = nullptr;
  disconnectButton = nullptr;
  removeButton = nullptr;

  const studio::DeviceRecord* record = studio::devices().find(managedInstance);
  if (record == nullptr) return;

  if (removeArmed) {
    lv_label_set_text(deviceModalTitle, "Remove?");
    lv_obj_t* warning = lv_label_create(deviceModalBody);
    lv_obj_set_width(warning, lv_pct(100));
    lv_label_set_long_mode(warning, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(warning, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(warning, UI_FONT_14, 0);
    lv_obj_set_style_text_color(warning, lv_color_hex(kColMuted), 0);
    char message[studio::kDeviceNameCapacity + 40];
    std::snprintf(message, sizeof(message), "Remove %s?\nThis cannot be undone.",
                  record->displayName);
    lv_label_set_text(warning, message);
    lv_obj_t* cancel = makeButton(deviceModalBody, "Cancel", onCancelRemove);
    lv_obj_set_size(cancel, lv_pct(100), 32);
    removeButton =
        makeButton(deviceModalBody, "Remove device", onRemove, kColDanger);
    lv_obj_set_size(removeButton, lv_pct(100), 32);
    return;
  }

  lv_label_set_text(deviceModalTitle, record->displayName);

  lv_obj_t* rename = makeButton(deviceModalBody, "Rename", onOpenRename);
  lv_obj_set_size(rename, lv_pct(100), 32);

  lv_obj_t* enabledRow = lv_obj_create(deviceModalBody);
  lv_obj_set_size(enabledRow, lv_pct(100), 34);
  lv_obj_set_style_bg_color(enabledRow, lv_color_hex(kColPanel), 0);
  lv_obj_set_style_radius(enabledRow, 8, 0);
  lv_obj_set_style_border_width(enabledRow, 0, 0);
  lv_obj_set_style_pad_all(enabledRow, 0, 0);
  lv_obj_clear_flag(enabledRow, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t* enabledLabel = lv_label_create(enabledRow);
  lv_label_set_text(enabledLabel, "Enabled");
  lv_obj_set_style_text_font(enabledLabel, UI_FONT_14, 0);
  lv_obj_align(enabledLabel, LV_ALIGN_LEFT_MID, 12, 0);
  enabledSwitch = lv_switch_create(enabledRow);
  lv_obj_set_size(enabledSwitch, 44, 24);
  lv_obj_align(enabledSwitch, LV_ALIGN_RIGHT_MID, -10, 0);
  lv_obj_add_event_cb(enabledSwitch, onEnabledChanged, LV_EVENT_VALUE_CHANGED, nullptr);

  lv_obj_t* connectionRow = lv_obj_create(deviceModalBody);
  lv_obj_set_size(connectionRow, lv_pct(100), 32);
  lv_obj_set_style_bg_opa(connectionRow, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(connectionRow, 0, 0);
  lv_obj_set_style_pad_all(connectionRow, 0, 0);
  lv_obj_clear_flag(connectionRow, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t* repair = makeButton(connectionRow, "Forget", onRepair);
  lv_obj_set_size(repair, 80, 32);
  lv_obj_align(repair, LV_ALIGN_LEFT_MID, 0, 0);

  disconnectButton = makeButton(connectionRow, "Disconnect", onDisconnect);
  lv_obj_set_size(disconnectButton, 80, 32);
  lv_obj_align(disconnectButton, LV_ALIGN_RIGHT_MID, 0, 0);

  removeButton = makeButton(deviceModalBody, "Remove", onRemove, kColDanger);
  lv_obj_set_size(removeButton, lv_pct(100), 32);

  if (studio::devices().isActive(managedInstance)) {
    lv_obj_clear_state(disconnectButton, LV_STATE_DISABLED);
  } else {
    lv_obj_add_state(disconnectButton, LV_STATE_DISABLED);
  }
  if (record->enabled) {
    lv_obj_add_state(enabledSwitch, LV_STATE_CHECKED);
  } else {
    lv_obj_clear_state(enabledSwitch, LV_STATE_CHECKED);
  }
}

void buildRenameOverlay() {
  if (renameOverlay != nullptr) {
    return;
  }
  renameOverlay = lv_obj_create(lv_layer_top());
  lv_obj_set_size(renameOverlay, 236, 236);
  lv_obj_center(renameOverlay);
  lv_obj_set_style_radius(renameOverlay, 118, 0);
  lv_obj_set_style_bg_color(renameOverlay, lv_color_hex(kColBg), 0);
  lv_obj_set_style_border_color(renameOverlay, lv_color_hex(kColAccent), 0);
  lv_obj_set_style_pad_all(renameOverlay, 0, 0);
  lv_obj_clear_flag(renameOverlay, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(renameOverlay, LV_OBJ_FLAG_HIDDEN);

  lv_obj_t* cancel =
      makeButton(renameOverlay, LV_SYMBOL_CLOSE, onCancelRename, kColPanel);
  lv_obj_set_size(cancel, 30, 30);
  lv_obj_align(cancel, LV_ALIGN_TOP_LEFT, 34, 24);

  renameText = lv_textarea_create(renameOverlay);
  lv_obj_set_size(renameText, 100, 30);
  lv_obj_align(renameText, LV_ALIGN_TOP_MID, 0, 18);
  lv_textarea_set_max_length(renameText, studio::kDeviceNameCapacity - 1);
  lv_textarea_set_one_line(renameText, true);
  lv_textarea_set_cursor_click_pos(renameText, true);

  lv_obj_t* save = makeButton(renameOverlay, LV_SYMBOL_OK, onSaveRename, kColAccent);
  lv_obj_set_size(save, 30, 30);
  lv_obj_align(save, LV_ALIGN_TOP_RIGHT, -34, 24);

  lv_obj_t* previous = makeButton(renameOverlay, LV_SYMBOL_LEFT, onRenamePrevious);
  lv_obj_set_size(previous, 32, 28);
  lv_obj_align(previous, LV_ALIGN_TOP_MID, -55, 53);

  renamePageLabel = lv_label_create(renameOverlay);
  lv_obj_set_style_text_font(renamePageLabel, UI_FONT_14, 0);
  lv_obj_set_style_text_color(renamePageLabel, lv_color_hex(kColMuted), 0);
  lv_obj_align(renamePageLabel, LV_ALIGN_TOP_MID, 0, 59);

  lv_obj_t* next = makeButton(renameOverlay, LV_SYMBOL_RIGHT, onRenameNext);
  lv_obj_set_size(next, 32, 28);
  lv_obj_align(next, LV_ALIGN_TOP_MID, 55, 53);

  renameKeypad = lv_btnmatrix_create(renameOverlay);
  lv_obj_set_size(renameKeypad, 150, 84);
  lv_obj_align(renameKeypad, LV_ALIGN_TOP_MID, 0, 83);
  lv_obj_set_style_bg_color(renameKeypad, lv_color_hex(kColBg), 0);
  lv_obj_set_style_border_width(renameKeypad, 0, 0);
  lv_obj_set_style_pad_all(renameKeypad, 2, 0);
  lv_obj_set_style_pad_gap(renameKeypad, 3, 0);
  lv_obj_set_style_bg_color(renameKeypad, lv_color_hex(kColPanel), LV_PART_ITEMS);
  lv_obj_set_style_radius(renameKeypad, 7, LV_PART_ITEMS);
  lv_obj_set_style_text_font(renameKeypad, UI_FONT_16, LV_PART_ITEMS);
  lv_obj_add_event_cb(renameKeypad, onRenameCharacter, LV_EVENT_VALUE_CHANGED, nullptr);
  lv_obj_add_event_cb(renameKeypad, onRenameKeypadGesture, LV_EVENT_GESTURE, nullptr);

  lv_obj_t* backspace =
      makeButton(renameOverlay, LV_SYMBOL_BACKSPACE, onRenameBackspace);
  lv_obj_set_size(backspace, 40, 26);
  lv_obj_align(backspace, LV_ALIGN_TOP_MID, -51, 172);
  lv_obj_t* space = makeButton(renameOverlay, "Space", onRenameSpace);
  lv_obj_set_size(space, 56, 26);
  lv_obj_align(space, LV_ALIGN_TOP_MID, 0, 172);
  lv_obj_t* letterCase = makeButton(renameOverlay, "Aa", onRenameCase);
  lv_obj_set_size(letterCase, 40, 26);
  lv_obj_align(letterCase, LV_ALIGN_TOP_MID, 51, 172);
  renameCaseLabel = lv_obj_get_child(letterCase, 0);

  refreshRenameKeypad();
}

}  // namespace

void releaseDeviceUis() {
  for (const DeviceUiHooks& hooks : kDeviceUis) {
    if (hooks.release != nullptr) hooks.release();
  }
}

void init() {
  studio::panelSettings().begin();
  haptic_feedback::setEnabled(studio::panelSettings().get().hapticEnabled);
  buildHome();
  buildDevices();
  buildPortal();
  lv_scr_load(scrHome);
}

void monitorHapticErrors() {
  constexpr uint8_t kRuntimeError = 1U << 0;
  constexpr uint8_t kPendingAddError = 1U << 1;
  constexpr uint8_t kSceneError = 1U << 2;
  constexpr uint8_t kPortalError = 1U << 3;

  uint8_t nextMask = 0;
  const studio::InstanceId foreground = studio::devices().foregroundInstance();
  if (foreground != studio::kInvalidInstanceId) {
    const studio::DeviceRuntimeState runtime =
        studio::devices().runtimeState(foreground);
    if (runtime.commandFailed) {
      nextMask |= kRuntimeError;
    }
    if (studio::devices().pendingAddCommitFailed(foreground)) {
      nextMask |= kPendingAddError;
    }
  }
  if (studio::scenes().progress().phase == studio::ScenePhase::Failed) {
    nextMask |= kSceneError;
  }
  if (portal::status() == portal::Status::Error) {
    nextMask |= kPortalError;
  }

  if ((nextMask & static_cast<uint8_t>(~hapticErrorMask)) != 0) {
    haptic_feedback::request(haptic_feedback::Pattern::Error);
  }
  hapticErrorMask = nextMask;
}

void monitorHapticConnections() {
  const studio::InstanceId foreground = studio::devices().foregroundInstance();
  const studio::DeviceRuntimeState runtime =
      foreground == studio::kInvalidInstanceId
          ? studio::DeviceRuntimeState{}
          : studio::devices().runtimeState(foreground);
  const bool foregroundReady =
      foreground != studio::kInvalidInstanceId &&
      runtime.link == studio::LinkState::Connected && runtime.protocolReady;

  if (foreground != hapticForegroundInstance) {
    hapticForegroundInstance = foreground;
    hapticForegroundReady = foregroundReady;
    if (foregroundReady) {
      // Opening a retained ready session confirms that commands can be sent
      // immediately, just like a connection becoming ready in place.
      haptic_feedback::request(haptic_feedback::Pattern::Connected);
    }
  } else {
    if (foregroundReady && !hapticForegroundReady) {
      haptic_feedback::request(haptic_feedback::Pattern::Connected);
    }
    hapticForegroundReady = foregroundReady;
  }

  const studio::SceneProgress& progress = studio::scenes().progress();
  if (progress.phase == studio::ScenePhase::Ready &&
      (progress.sceneId != hapticSceneId ||
       hapticScenePhase != studio::ScenePhase::Ready)) {
    haptic_feedback::request(haptic_feedback::Pattern::Connected);
  }
  hapticSceneId = progress.sceneId;
  hapticScenePhase = progress.phase;
}

void tick() {
  monitorHapticErrors();
  monitorHapticConnections();
  const DeviceUiHooks* activeUi = activeDeviceUi();
  if (activeUi != nullptr) {
    if (activeUi->tick != nullptr) activeUi->tick();
    return;
  }
  if (settingsHeaderNeedsRefresh && settingsHeader != nullptr) {
    settingsHeaderNeedsRefresh = false;
    lv_obj_move_foreground(settingsHeader);
    lv_obj_invalidate(scrSettings);
  }
  if (scene_ui::active()) {
    scene_ui::tick();
    return;
  }
  if (screen == Screen::Settings &&
      settingsView == SettingsView::FactoryReset && factoryResetHolding &&
      !factoryResetTriggered) {
    const uint32_t elapsed = millis() - factoryResetStartedMs;
    if (factoryResetProgress != nullptr) {
      lv_bar_set_value(factoryResetProgress,
                       static_cast<int32_t>(elapsed > 3000 ? 3000 : elapsed),
                       LV_ANIM_OFF);
    }
    if (elapsed >= 3000) {
      factoryResetTriggered = true;
      factoryResetHolding = false;
      lv_label_set_text(factoryResetStatus, "Erasing saved data...");
      lv_obj_add_state(factoryResetButton, LV_STATE_DISABLED);
      studio::scenes().cancel();
      if (portal::active()) portal::stop();
      studio::devices().deactivateAll();
      haptic_feedback::setEnabled(false);
      if (!studio::factory_reset::eraseAndRestart()) {
        factoryResetTriggered = false;
        lv_label_set_text(factoryResetStatus, "Reset failed");
        lv_obj_clear_state(factoryResetButton, LV_STATE_DISABLED);
        haptic_feedback::setEnabled(
            studio::panelSettings().get().hapticEnabled);
        haptic_feedback::request(haptic_feedback::Pattern::Error);
      }
    }
  }
  const uint32_t now = millis();
  if (now - lastRefreshMs < 500) {
    return;
  }
  lastRefreshMs = now;
  if (screen == Screen::Portal) {
    refreshPortal();
  } else if (screen == Screen::Settings &&
             settingsView == SettingsView::SystemInfo) {
    refreshSystemInfo();
  }
}

void handleShortPress() {
  const DeviceUiHooks* activeUi = activeDeviceUi();
  if (activeUi != nullptr) {
    if (activeUi->shortPress != nullptr) activeUi->shortPress();
    return;
  }
  if (scene_ui::active()) {
    scene_ui::handleShortPress();
    return;
  }
}

bool handleLongPress() {
  const DeviceUiHooks* activeUi = activeDeviceUi();
  if (activeUi != nullptr) {
    if (activeUi->longPress != nullptr) activeUi->longPress();
    return true;
  }
  if (scene_ui::active()) {
    scene_ui::handleLongPress();
    return true;
  }
  if (picker_shell::handleBack()) {
    return true;
  }
  if (renameOverlay != nullptr) {
    onCancelRename(nullptr);
    return true;
  } else if (deviceModal != nullptr) {
    closeDeviceModal();
    return true;
  } else if (screen == Screen::Devices) {
    showHome();
    return true;
  } else if (screen == Screen::Portal) {
    onExitPortal(nullptr);
    return true;
  } else if (screen == Screen::Settings) {
    onSettingsBack(nullptr);
    return true;
  }
  return false;
}

bool handleLongPressToHome() {
  bool handled = false;
  // The deepest current path is bounded; use each screen's ordinary Back path
  // so provisional pairing, borrowed controls, and sequence ownership clean up.
  for (uint8_t depth = 0; depth < 8 && handleLongPress(); ++depth) {
    handled = true;
  }
  return handled;
}

void showHome() {
  hideDeviceUis();
  if (portal::active()) portal::stop();
  if (scene_ui::active()) scene_ui::hide();
  closeDeviceModal();
  closeRename();
  closeAddPicker();
  releaseDeviceRows();
  screen = Screen::Home;
  lv_scr_load(scrHome);
  destroySettingsScreen();
  releaseDeviceUis();
}

void showSettings() {
  closeDeviceModal();
  closeRename();
  closeAddPicker();
  releaseDeviceRows();
  showSettingsView(SettingsView::Menu);
  releaseDeviceUis();
}

void showDevices() {
  hideDeviceUis();
  if (scene_ui::active()) scene_ui::hide();
  closeDeviceModal();
  closeRename();
  closeAddPicker();
  screen = Screen::Devices;
  lv_scr_load(scrDevices);
  releaseDeviceUis();
  refreshDevices();
}

void showPortal() {
  closeDeviceModal();
  closeRename();
  closeAddPicker();
  destroySettingsScreen();
  if (!portal::begin()) {
    return;
  }
  releaseDeviceRows();
  screen = Screen::Portal;
  refreshPortal();
  lv_scr_load(scrPortal);
  releaseDeviceUis();
}

void showDevice(studio::InstanceId instanceId) {
  const studio::DeviceRecord* record = studio::devices().find(instanceId);
  if (record == nullptr || !record->enabled) return;
  const DeviceUiHooks* hooks = deviceUi(record->driverId);
  if (hooks == nullptr || hooks->show == nullptr) return;
  releaseDeviceRows();
  hooks->show(instanceId);
}

void showDeviceParent() {
  if (scene_ui::deviceControlOpen()) {
    scene_ui::returnFromDeviceControl();
    return;
  }
  const studio::InstanceId pending = studio::devices().pendingAdd();
  if (pending != studio::kInvalidInstanceId) {
    studio::devices().cancelPendingAdd(pending);
  }
  showDevices();
}

void parkForScreenRebuild() {
  lv_obj_t* resident = screen == Screen::Devices ? scrDevices : scrHome;
  if (resident != nullptr) {
    lv_scr_load(resident);
  }
}

void releaseInactiveScreens() { releaseDeviceUis(); }

void promptRename(const char* initial, RenameDoneFn onDone,
                  RenameCancelFn onCancel) {
  if (renameOverlay == nullptr) {
    buildRenameOverlay();
  }
  renameDoneCallback = onDone;
  renameCancelCallback = onCancel;
  lv_textarea_set_text(renameText, initial != nullptr ? initial : "");
  lv_textarea_set_cursor_pos(renameText, LV_TEXTAREA_CURSOR_LAST);
  renamePage = 0;
  renameUpperCase = true;
  refreshRenameKeypad();
  lv_obj_clear_flag(renameOverlay, LV_OBJ_FLAG_HIDDEN);
}

void closeRenamePrompt() { closeRename(); }

bool renamePromptActive() {
  return renameOverlay != nullptr &&
         !lv_obj_has_flag(renameOverlay, LV_OBJ_FLAG_HIDDEN);
}

#ifdef UI_SIMULATOR
studio::InstanceId simDeviceAtDisplayIndex(size_t index) {
  studio::InstanceId displayOrder[CONFIG_MAX_DEVICE_INSTANCES] = {};
  const size_t count =
      buildDeviceDisplayOrder(displayOrder, CONFIG_MAX_DEVICE_INSTANCES);
  return index < count ? displayOrder[index] : studio::kInvalidInstanceId;
}

bool simAddDeviceAtListEnd() {
  devicePage = devicePageCount() - 1;
  refreshDevices();
  if (addButton == nullptr || lv_obj_get_parent(addButton) == nullptr) {
    return false;
  }
  lv_obj_t* addRow = lv_obj_get_parent(addButton);
  if (lv_obj_get_parent(addRow) != deviceList) {
    return false;
  }
  const uint32_t count = lv_obj_get_child_cnt(deviceList);
  const bool centered =
      lv_obj_get_x(addButton) + lv_obj_get_width(addButton) / 2 ==
      lv_obj_get_width(addRow) / 2;
  return count > 0 && lv_obj_get_child(deviceList, count - 1) == addRow &&
         centered;
}

void simShowAddDevice() {
  showDevices();
  if (addButton != nullptr) {
    lv_obj_add_flag(addButton, LV_OBJ_FLAG_HIDDEN);
  }
  picker_shell::Callbacks callbacks;
  callbacks.onDriverChosen = onDriverChosen;
  callbacks.onClosed = onAddPickerClosed;
  picker_shell::show(picker_shell::Mode::AddDriver, callbacks);
}

void simShowManage(studio::InstanceId instanceId) {
  showDevices();
  managedInstance = instanceId;
  const studio::DeviceRecord* record = studio::devices().find(managedInstance);
  if (record == nullptr) {
    managedInstance = studio::kInvalidInstanceId;
    return;
  }
  if (deviceModal == nullptr) {
    buildDeviceModal();
  }
  removeArmed = false;
  disconnectArmed = false;
  refreshDeviceModal();
  lv_obj_clear_flag(deviceModal, LV_OBJ_FLAG_HIDDEN);
}

void simShowRename(studio::InstanceId instanceId) {
  simShowManage(instanceId);
  const studio::DeviceRecord* record = studio::devices().find(managedInstance);
  if (record == nullptr) {
    return;
  }
  releaseDeviceRows();
  promptRename(record->displayName, onDeviceRenameDone);
}

void simSubmitRename(const char* name) {
  if (renameText == nullptr) return;
  lv_textarea_set_text(renameText, name != nullptr ? name : "");
  onSaveRename(nullptr);
}

void simCancelRename() { onCancelRename(nullptr); }

void simRequestManagedDisconnect() { onDisconnect(nullptr); }
void simRequestManagedRemove() { onRemove(nullptr); }

void simShowWifiSettings() { showSettingsView(SettingsView::Wifi); }
void simShowAbout() { showSettingsView(SettingsView::About); }
void simShowSystemInfo() { showSettingsView(SettingsView::SystemInfo); }
void simShowFactoryReset() { showSettingsView(SettingsView::FactoryReset); }

void simScrollSettingsToEnd() {
  if (settingsMenuList != nullptr) {
    lv_obj_scroll_to_y(settingsMenuList, lv_obj_get_scroll_bottom(settingsMenuList),
                       LV_ANIM_OFF);
  }
}

void simScrollAboutToEnd() {
  if (aboutContent != nullptr) {
    lv_obj_scroll_to_y(aboutContent, lv_obj_get_scroll_bottom(aboutContent),
                       LV_ANIM_OFF);
  }
}

void simSetHapticEnabled(bool enabled) {
  if (studio::panelSettings().setHapticEnabled(enabled)) {
    haptic_feedback::setEnabled(enabled);
  }
}

void simSetFactoryResetHolding(bool holding) {
  if (holding) onFactoryResetPressed(nullptr);
  else onFactoryResetReleased(nullptr);
}

bool simShowingHome() {
  return screen == Screen::Home && lv_scr_act() == scrHome;
}
#endif

}  // namespace ui
