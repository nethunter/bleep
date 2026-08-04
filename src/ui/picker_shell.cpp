#include "ui/picker_shell.h"

#include <lvgl.h>

#include <cstdio>

#include "assets/ui_icons.h"
#include "core/device_manager.h"
#include "core/driver_catalog.h"
#include "fonts/ui_fonts.h"

namespace picker_shell {
namespace {

constexpr uint32_t kColBg = 0x05070A;
constexpr uint32_t kColPanel = 0x12161D;
constexpr uint32_t kColAccent = 0x35C7F2;
constexpr uint32_t kColText = 0xF3F4F6;
constexpr uint32_t kColMuted = 0x8A94A6;
constexpr uint32_t kColDanger = 0xF26D6D;
constexpr uint32_t kColOk = 0x3DDC97;

enum class Level : uint8_t { Category, DeviceList, Action };

Mode mode = Mode::AddDriver;
Level level = Level::Category;
studio::DeviceType category = studio::DeviceType::Camera;
studio::InstanceId selectedDevice = studio::kInvalidInstanceId;
Callbacks callbacks{};

lv_obj_t* overlay = nullptr;
lv_obj_t* titleLabel = nullptr;
lv_obj_t* closeButton = nullptr;
lv_obj_t* backButton = nullptr;
lv_obj_t* body = nullptr;

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

const char* categoryName(studio::DeviceType type) {
  switch (type) {
    case studio::DeviceType::Motion:
      return "Motion";
    case studio::DeviceType::Light:
      return "Lights";
    case studio::DeviceType::Camera:
      return "Cameras";
    case studio::DeviceType::Recorder:
      return "Recorders";
    case studio::DeviceType::Unknown:
      return "Other";
  }
  return "Other";
}

// Shorter labels for the 74px tiles so text clears the round bezel.
const char* categoryTileLabel(studio::DeviceType type) {
  switch (type) {
    case studio::DeviceType::Motion:
      return "Motion";
    case studio::DeviceType::Light:
      return "Lights";
    case studio::DeviceType::Camera:
      return "Camera";
    case studio::DeviceType::Recorder:
      return "Record";
    case studio::DeviceType::Unknown:
      return "Other";
  }
  return "Other";
}

const lv_img_dsc_t* categoryIcon(studio::DeviceType type) {
  switch (type) {
    case studio::DeviceType::Motion:
      return &ui_icon_cat_motion;
    case studio::DeviceType::Light:
      return &ui_icon_cat_lights;
    case studio::DeviceType::Camera:
      return &ui_icon_cat_cameras;
    case studio::DeviceType::Recorder:
      return &ui_icon_cat_recorders;
    case studio::DeviceType::Unknown:
      return &ui_icon_devices;
  }
  return &ui_icon_devices;
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
  return descriptor != nullptr &&
         instanceCount(descriptor->id) < descriptor->maxInstances;
}

bool categoryHasDrivers(studio::DeviceType type) {
  for (size_t i = 0; i < studio::DriverCatalog::count(); ++i) {
    const studio::DriverDescriptor* descriptor = studio::DriverCatalog::at(i);
    if (descriptor != nullptr && descriptor->type == type) {
      return true;
    }
  }
  return false;
}

bool deviceSupportsSceneActions(const studio::DeviceRecord& record,
                                const studio::DriverDescriptor& descriptor) {
  if (!record.enabled) {
    return false;
  }
  const uint32_t caps = descriptor.capabilities;
  return (caps & studio::capabilityBit(studio::Capability::RecordStart)) != 0 ||
         (caps & studio::capabilityBit(studio::Capability::RecordStop)) != 0;
}

bool categoryHasSceneDevices(studio::DeviceType type) {
  for (size_t i = 0; i < studio::devices().count(); ++i) {
    const studio::DeviceRecord* record = studio::devices().at(i);
    if (record == nullptr) {
      continue;
    }
    const studio::DriverDescriptor* descriptor =
        studio::DriverCatalog::find(record->driverId);
    if (descriptor != nullptr && descriptor->type == type &&
        deviceSupportsSceneActions(*record, *descriptor)) {
      return true;
    }
  }
  return false;
}

bool categoryVisible(studio::DeviceType type) {
  return mode == Mode::AddDriver ? categoryHasDrivers(type)
                                 : categoryHasSceneDevices(type);
}

void refresh();
void onClose(lv_event_t*);
void onBack(lv_event_t*);

void setChrome() {
  const bool atRoot = level == Level::Category;
  if (closeButton != nullptr) {
    if (atRoot) {
      lv_obj_clear_flag(closeButton, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(closeButton, LV_OBJ_FLAG_HIDDEN);
    }
  }
  if (backButton != nullptr) {
    if (atRoot) {
      lv_obj_add_flag(backButton, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_clear_flag(backButton, LV_OBJ_FLAG_HIDDEN);
    }
  }
}

void onChooseCategory(lv_event_t* event) {
  category = static_cast<studio::DeviceType>(
      reinterpret_cast<uintptr_t>(lv_event_get_user_data(event)));
  level = Level::DeviceList;
  selectedDevice = studio::kInvalidInstanceId;
  refresh();
}

void onChooseDriver(lv_event_t* event) {
  const auto driverId = static_cast<studio::DriverId>(
      reinterpret_cast<uintptr_t>(lv_event_get_user_data(event)));
  if (callbacks.onDriverChosen != nullptr) {
    callbacks.onDriverChosen(driverId);
  }
  hide();
}

void onChooseDevice(lv_event_t* event) {
  selectedDevice = static_cast<studio::InstanceId>(
      reinterpret_cast<uintptr_t>(lv_event_get_user_data(event)));
  level = Level::Action;
  refresh();
}

void onChooseAction(lv_event_t* event) {
  const auto command = static_cast<studio::CommandType>(
      reinterpret_cast<uintptr_t>(lv_event_get_user_data(event)));
  if (callbacks.onSceneAction != nullptr) {
    callbacks.onSceneAction(selectedDevice, command);
  }
  hide();
}

void onChooseWait(lv_event_t*) {
  if (callbacks.onWaitChosen != nullptr) {
    callbacks.onWaitChosen();
  }
  hide();
}

void onClose(lv_event_t*) { hide(); }

void onBack(lv_event_t*) {
  if (level == Level::Action) {
    level = Level::DeviceList;
    selectedDevice = studio::kInvalidInstanceId;
    refresh();
    return;
  }
  if (level == Level::DeviceList) {
    level = Level::Category;
    refresh();
  }
}

lv_obj_t* makeCategoryTile(lv_obj_t* parent, studio::DeviceType type,
                           lv_coord_t size) {
  lv_obj_t* tile = lv_obj_create(parent);
  lv_obj_set_size(tile, size, size);
  lv_obj_set_style_bg_color(tile, lv_color_hex(kColPanel), 0);
  lv_obj_set_style_radius(tile, 14, 0);
  lv_obj_set_style_border_width(tile, 0, 0);
  lv_obj_set_style_pad_all(tile, 0, 0);
  lv_obj_clear_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(tile, LV_OBJ_FLAG_CLICKABLE);
  void* userData = reinterpret_cast<void*>(static_cast<uintptr_t>(type));
  lv_obj_add_event_cb(tile, onChooseCategory, LV_EVENT_CLICKED, userData);

  lv_obj_t* image = lv_img_create(tile);
  lv_img_set_src(image, categoryIcon(type));
  lv_obj_align(image, LV_ALIGN_TOP_MID, 0, size >= 70 ? 4 : 2);

  lv_obj_t* label = lv_label_create(tile);
  lv_label_set_text(label, categoryTileLabel(type));
  lv_obj_set_style_text_font(label, UI_FONT_14, 0);
  lv_obj_set_style_text_color(label, lv_color_hex(kColText), 0);
  lv_obj_align(label, LV_ALIGN_BOTTOM_MID, 0, size >= 70 ? -5 : -3);
  return tile;
}

void refreshCategory() {
  const bool sceneStep = mode == Mode::SceneStep;
  lv_label_set_text(titleLabel, sceneStep ? "Add step" : "Add device");
  lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(body, sceneStep ? 4 : 6, 0);
  lv_obj_set_flex_align(body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);

  if (sceneStep) {
    lv_obj_t* wait = makeButton(body, "Wait 500ms", onChooseWait, kColAccent);
    lv_obj_set_size(wait, 140, 26);
  }

  constexpr studio::DeviceType kCategories[] = {
      studio::DeviceType::Motion,
      studio::DeviceType::Light,
      studio::DeviceType::Camera,
      studio::DeviceType::Recorder,
  };
  studio::DeviceType visible[4];
  uint8_t shown = 0;
  for (studio::DeviceType type : kCategories) {
    if (!categoryVisible(type) || shown >= 4) {
      continue;
    }
    visible[shown++] = type;
  }

  if (shown == 0) {
    lv_obj_t* empty = lv_label_create(body);
    lv_label_set_text(empty, sceneStep ? "No devices" : "No drivers");
    lv_obj_set_style_text_font(empty, UI_FONT_14, 0);
    lv_obj_set_style_text_color(empty, lv_color_hex(kColMuted), 0);
    return;
  }

  // Keep tiles inside the round bezel: fewer categories use slightly smaller
  // tiles so a single row still clears the curved edges.
  const lv_coord_t tileSize =
      sceneStep ? (shown <= 2 ? 70 : 64) : (shown <= 2 ? 78 : 74);
  const lv_coord_t gap = 6;
  const uint8_t columns = shown <= 2 ? shown : 2;
  const lv_coord_t gridW = columns * tileSize + (columns > 1 ? gap : 0);
  const uint8_t rows = (shown + columns - 1) / columns;
  const lv_coord_t gridH = rows * tileSize + (rows > 1 ? gap : 0);

  lv_obj_t* grid = lv_obj_create(body);
  lv_obj_set_size(grid, gridW, gridH);
  lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(grid, 0, 0);
  lv_obj_set_style_pad_all(grid, 0, 0);
  lv_obj_set_style_pad_row(grid, gap, 0);
  lv_obj_set_style_pad_column(grid, gap, 0);
  lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);

  for (uint8_t i = 0; i < shown; ++i) {
    makeCategoryTile(grid, visible[i], tileSize);
  }
}

void refreshDriverList() {
  lv_label_set_text(titleLabel, categoryName(category));
  lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(body, 4, 0);
  lv_obj_set_flex_align(body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_START);

  bool any = false;
  for (size_t i = 0; i < studio::DriverCatalog::count(); ++i) {
    const studio::DriverDescriptor* descriptor = studio::DriverCatalog::at(i);
    if (descriptor == nullptr || descriptor->type != category) {
      continue;
    }
    any = true;
    const bool available = driverCanAdd(descriptor);
    lv_obj_t* choice = lv_btn_create(body);
    lv_obj_set_size(choice, lv_pct(100), 40);
    lv_obj_set_style_bg_color(choice, lv_color_hex(kColPanel), 0);
    lv_obj_set_style_bg_opa(choice, available ? LV_OPA_COVER : LV_OPA_60, 0);
    lv_obj_set_style_radius(choice, 8, 0);
    lv_obj_set_style_shadow_width(choice, 0, 0);
    lv_obj_set_style_pad_all(choice, 0, 0);
    if (available) {
      void* userData =
          reinterpret_cast<void*>(static_cast<uintptr_t>(descriptor->id));
      lv_obj_add_event_cb(choice, onChooseDriver, LV_EVENT_CLICKED, userData);
    } else {
      lv_obj_clear_flag(choice, LV_OBJ_FLAG_CLICKABLE);
    }

    lv_obj_t* model = lv_label_create(choice);
    lv_label_set_text(model, descriptor->model);
    lv_label_set_long_mode(model, LV_LABEL_LONG_DOT);
    lv_obj_set_width(model, 140);
    lv_obj_set_style_text_font(model, UI_FONT_14, 0);
    lv_obj_set_style_text_color(
        model, lv_color_hex(available ? kColText : kColMuted), 0);
    lv_obj_align(model, LV_ALIGN_TOP_LEFT, 8, 4);

    lv_obj_t* detail = lv_label_create(choice);
    lv_label_set_text(detail, available ? descriptor->brand : "Limit reached");
    lv_obj_set_style_text_font(detail, UI_FONT_14, 0);
    lv_obj_set_style_text_color(detail, lv_color_hex(kColMuted), 0);
    lv_obj_align(detail, LV_ALIGN_TOP_LEFT, 8, 22);
  }
  if (!any) {
    lv_obj_t* empty = lv_label_create(body);
    lv_label_set_text(empty, "No drivers");
    lv_obj_set_style_text_color(empty, lv_color_hex(kColMuted), 0);
  }
}

void refreshDeviceList() {
  lv_label_set_text(titleLabel, categoryName(category));
  lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(body, 4, 0);
  lv_obj_set_flex_align(body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_START);

  bool any = false;
  for (size_t i = 0; i < studio::devices().count(); ++i) {
    const studio::DeviceRecord* record = studio::devices().at(i);
    if (record == nullptr) {
      continue;
    }
    const studio::DriverDescriptor* descriptor =
        studio::DriverCatalog::find(record->driverId);
    if (descriptor == nullptr || descriptor->type != category ||
        !deviceSupportsSceneActions(*record, *descriptor)) {
      continue;
    }
    any = true;
    void* userData =
        reinterpret_cast<void*>(static_cast<uintptr_t>(record->instanceId));
    lv_obj_t* button = makeButton(body, record->displayName, onChooseDevice);
    lv_obj_set_size(button, lv_pct(100), 34);
    lv_obj_remove_event_cb(button, onChooseDevice);
    lv_obj_add_event_cb(button, onChooseDevice, LV_EVENT_CLICKED, userData);
  }
  if (!any) {
    lv_obj_t* empty = lv_label_create(body);
    lv_label_set_text(empty, "No devices");
    lv_obj_set_style_text_color(empty, lv_color_hex(kColMuted), 0);
  }
}

void refreshActions() {
  const studio::DeviceRecord* device = studio::devices().find(selectedDevice);
  const studio::DriverDescriptor* descriptor =
      device != nullptr ? studio::DriverCatalog::find(device->driverId) : nullptr;
  lv_label_set_text(titleLabel, device != nullptr ? device->displayName : "Action");
  lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(body, 6, 0);
  lv_obj_set_flex_align(body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);

  if (device == nullptr || descriptor == nullptr) {
    lv_obj_t* empty = lv_label_create(body);
    lv_label_set_text(empty, "Missing device");
    lv_obj_set_style_text_color(empty, lv_color_hex(kColMuted), 0);
    return;
  }
  if ((descriptor->capabilities &
       studio::capabilityBit(studio::Capability::RecordStart)) != 0) {
    void* userData = reinterpret_cast<void*>(
        static_cast<uintptr_t>(studio::CommandType::RecordStart));
    lv_obj_t* button = makeButton(body, "Record Start", onChooseAction, kColOk);
    lv_obj_set_size(button, lv_pct(100), 36);
    lv_obj_remove_event_cb(button, onChooseAction);
    lv_obj_add_event_cb(button, onChooseAction, LV_EVENT_CLICKED, userData);
  }
  if ((descriptor->capabilities &
       studio::capabilityBit(studio::Capability::RecordStop)) != 0) {
    void* userData = reinterpret_cast<void*>(
        static_cast<uintptr_t>(studio::CommandType::RecordStop));
    lv_obj_t* button =
        makeButton(body, "Record Stop", onChooseAction, kColDanger);
    lv_obj_set_size(button, lv_pct(100), 36);
    lv_obj_remove_event_cb(button, onChooseAction);
    lv_obj_add_event_cb(button, onChooseAction, LV_EVENT_CLICKED, userData);
  }
}

void refresh() {
  if (overlay == nullptr || body == nullptr) {
    return;
  }
  setChrome();
  lv_obj_clean(body);
  if (level == Level::Category) {
    refreshCategory();
  } else if (level == Level::DeviceList) {
    if (mode == Mode::AddDriver) {
      refreshDriverList();
    } else {
      refreshDeviceList();
    }
  } else {
    refreshActions();
  }
}

void ensureOverlay() {
  if (overlay != nullptr) {
    return;
  }
  overlay = lv_obj_create(lv_layer_top());
  lv_obj_set_size(overlay, 240, 240);
  lv_obj_center(overlay);
  lv_obj_set_style_radius(overlay, 120, 0);
  lv_obj_set_style_bg_color(overlay, lv_color_hex(kColBg), 0);
  lv_obj_set_style_border_width(overlay, 0, LV_PART_MAIN | LV_STATE_ANY);
  lv_obj_set_style_outline_width(overlay, 0, LV_PART_MAIN | LV_STATE_ANY);
  lv_obj_set_style_shadow_width(overlay, 0, LV_PART_MAIN | LV_STATE_ANY);
  lv_obj_set_style_text_color(overlay, lv_color_hex(kColText), 0);
  lv_obj_set_style_pad_all(overlay, 0, 0);
  lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(overlay, LV_OBJ_FLAG_HIDDEN);

  closeButton = makeButton(overlay, LV_SYMBOL_CLOSE, onClose);
  lv_obj_set_size(closeButton, 30, 30);
  lv_obj_align(closeButton, LV_ALIGN_TOP_LEFT, 34, 24);

  backButton = makeButton(overlay, LV_SYMBOL_LEFT, onBack);
  lv_obj_set_size(backButton, 30, 30);
  lv_obj_align(backButton, LV_ALIGN_TOP_LEFT, 34, 24);
  lv_obj_add_flag(backButton, LV_OBJ_FLAG_HIDDEN);

  titleLabel = lv_label_create(overlay);
  lv_obj_set_width(titleLabel, 140);
  lv_label_set_long_mode(titleLabel, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_align(titleLabel, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(titleLabel, UI_FONT_16, 0);
  lv_obj_align(titleLabel, LV_ALIGN_TOP_MID, 10, 30);

  body = lv_obj_create(overlay);
  lv_obj_set_size(body, 176, 158);
  lv_obj_align(body, LV_ALIGN_TOP_MID, 0, 56);
  lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(body, 0, 0);
  lv_obj_set_style_pad_all(body, 2, 0);
  lv_obj_set_scroll_dir(body, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(body, LV_SCROLLBAR_MODE_OFF);
  lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLL_ELASTIC);
}

}  // namespace

void show(Mode showMode, const Callbacks& showCallbacks) {
  mode = showMode;
  callbacks = showCallbacks;
  level = Level::Category;
  selectedDevice = studio::kInvalidInstanceId;
  ensureOverlay();
  refresh();
  lv_obj_clear_flag(overlay, LV_OBJ_FLAG_HIDDEN);
}

void hide() {
  if (overlay == nullptr) {
    return;
  }
  lv_obj_del(overlay);
  overlay = nullptr;
  titleLabel = nullptr;
  closeButton = nullptr;
  backButton = nullptr;
  body = nullptr;
  level = Level::Category;
  selectedDevice = studio::kInvalidInstanceId;
  const Callbacks closed = callbacks;
  callbacks = Callbacks{};
  if (closed.onClosed != nullptr) {
    closed.onClosed();
  }
}

bool active() {
  return overlay != nullptr && !lv_obj_has_flag(overlay, LV_OBJ_FLAG_HIDDEN);
}

bool handleBack() {
  if (!active()) {
    return false;
  }
  if (level != Level::Category) {
    onBack(nullptr);
    return true;
  }
  hide();
  return true;
}

#ifdef UI_SIMULATOR
void simShowCategory(Mode showMode) {
  Callbacks empty{};
  show(showMode, empty);
}

void simShowDeviceList(Mode showMode, studio::DeviceType showCategory) {
  Callbacks empty{};
  show(showMode, empty);
  category = showCategory;
  level = Level::DeviceList;
  selectedDevice = studio::kInvalidInstanceId;
  refresh();
}

void simShowActions(Mode showMode, studio::InstanceId instanceId) {
  Callbacks empty{};
  show(showMode, empty);
  const studio::DeviceRecord* record = studio::devices().find(instanceId);
  const studio::DriverDescriptor* descriptor =
      record != nullptr ? studio::DriverCatalog::find(record->driverId) : nullptr;
  category = descriptor != nullptr ? descriptor->type : studio::DeviceType::Camera;
  selectedDevice = instanceId;
  level = Level::Action;
  refresh();
}
#endif

}  // namespace picker_shell
