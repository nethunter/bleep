#include "scene_ui.h"

#include <Arduino.h>
#include <lvgl.h>

#include <cstdio>
#include <cstring>

#include "core/device_manager.h"
#include "core/driver_catalog.h"
#include "core/scene_service.h"
#include "fonts/ui_fonts.h"
#include "ui.h"

namespace scene_ui {
namespace {

constexpr uint32_t kColBg = 0x05070A;
constexpr uint32_t kColPanel = 0x12161D;
constexpr uint32_t kColAccent = 0x35C7F2;
constexpr uint32_t kColText = 0xF3F4F6;
constexpr uint32_t kColMuted = 0x8A94A6;
constexpr uint32_t kColDanger = 0xF26D6D;
constexpr uint32_t kColOk = 0x3DDC97;

constexpr lv_coord_t kRoundBackX = 40;
constexpr lv_coord_t kRoundBackY = 36;

enum class View : uint8_t { List, Run, Edit };
enum class AddLevel : uint8_t { Category, Device, Action };

View view = View::List;
bool visible = false;
studio::SceneId currentScene = studio::kInvalidSceneId;
bool editingStart = true;
uint32_t lastRefreshMs = 0;
AddLevel addLevel = AddLevel::Category;
studio::DeviceType addCategory = studio::DeviceType::Camera;
studio::InstanceId addDeviceId = studio::kInvalidInstanceId;

lv_obj_t* scrList = nullptr;
lv_obj_t* scrRun = nullptr;
lv_obj_t* scrEdit = nullptr;
lv_obj_t* listBody = nullptr;
lv_obj_t* runTitle = nullptr;
lv_obj_t* runPhase = nullptr;
lv_obj_t* runDetail = nullptr;
lv_obj_t* startButton = nullptr;
lv_obj_t* stopButton = nullptr;
lv_obj_t* editBody = nullptr;
lv_obj_t* editTitle = nullptr;
lv_obj_t* addOverlay = nullptr;
lv_obj_t* addTitle = nullptr;
lv_obj_t* addBackButton = nullptr;
lv_obj_t* addCloseButton = nullptr;
lv_obj_t* addList = nullptr;

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

studio::SceneId eventScene(lv_event_t* event) {
  return static_cast<studio::SceneId>(
      reinterpret_cast<uintptr_t>(lv_event_get_user_data(event)));
}

const char* phaseText(studio::ScenePhase phase) {
  switch (phase) {
    case studio::ScenePhase::Idle:
      return "Ready";
    case studio::ScenePhase::Connecting:
      return "Connecting";
    case studio::ScenePhase::RunningStart:
      return "Starting";
    case studio::ScenePhase::IdleArmed:
      return "Recording";
    case studio::ScenePhase::RunningStop:
      return "Stopping";
    case studio::ScenePhase::Failed:
      return "Failed";
    case studio::ScenePhase::Completed:
      return "Done";
  }
  return "Ready";
}

void formatStep(char* buffer, size_t capacity, const studio::SceneStep& step) {
  if (step.type == studio::SceneStepType::Wait) {
    std::snprintf(buffer, capacity, "Wait %ums",
                  static_cast<unsigned>(step.waitMs));
    return;
  }
  const studio::DeviceRecord* device = studio::devices().find(step.targetId);
  const char* name = device != nullptr ? device->displayName : "?";
  const char* command =
      step.command == studio::CommandType::RecordStart
          ? "Rec"
          : (step.command == studio::CommandType::RecordStop ? "Stop" : "Cmd");
  std::snprintf(buffer, capacity, "%s %s", command, name);
}

void ensureScreens();
void refreshList();
void refreshRun();
void refreshEdit();
void showListView();
void showRunView(studio::SceneId sceneId);
void showEditView(studio::SceneId sceneId, bool startList);

void onBackToHome(lv_event_t*) {
  hide();
  ui::showHome();
}
void onBackToList(lv_event_t*) { showListView(); }
void onBackToRun(lv_event_t*) { showRunView(currentScene); }

void onOpenScene(lv_event_t* event) { showRunView(eventScene(event)); }

void onAddBlank(lv_event_t*) {
  if (studio::scenes().busy()) {
    return;
  }
  studio::SceneId id = studio::kInvalidSceneId;
  if (studio::scenes().add("Sequence", id) == studio::SceneRegistryStatus::Ok) {
    showEditView(id, true);
  }
}

void onSeedPressRecord(lv_event_t*) {
  if (studio::scenes().busy()) {
    return;
  }
  studio::SceneId id = studio::kInvalidSceneId;
  if (studio::scenes().seedPressRecord(id)) {
    showRunView(id);
  }
}

void onStart(lv_event_t*) {
  if (currentScene == studio::kInvalidSceneId) {
    return;
  }
  studio::scenes().start(currentScene);
  refreshRun();
}

void onStop(lv_event_t*) {
  studio::scenes().stop();
  refreshRun();
}

void onCancel(lv_event_t*) {
  studio::scenes().cancel();
  refreshRun();
}

void onEditStart(lv_event_t*) {
  if (studio::scenes().busy() || studio::scenes().holdsLinks()) {
    return;
  }
  showEditView(currentScene, true);
}

void onEditStop(lv_event_t*) {
  if (studio::scenes().busy() || studio::scenes().holdsLinks()) {
    return;
  }
  showEditView(currentScene, false);
}

void onDeleteScene(lv_event_t*) {
  if (studio::scenes().busy() || studio::scenes().holdsLinks()) {
    return;
  }
  studio::scenes().remove(currentScene);
  currentScene = studio::kInvalidSceneId;
  showListView();
}

bool loadEditable(studio::SceneRecord& record) {
  const studio::SceneRecord* source = studio::scenes().find(currentScene);
  if (source == nullptr) {
    return false;
  }
  record = *source;
  return true;
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

bool deviceSupportsSceneActions(const studio::DeviceRecord& record,
                                const studio::DriverDescriptor& descriptor) {
  if (!record.enabled) {
    return false;
  }
  const uint32_t caps = descriptor.capabilities;
  return (caps & studio::capabilityBit(studio::Capability::RecordStart)) != 0 ||
         (caps & studio::capabilityBit(studio::Capability::RecordStop)) != 0;
}

bool categoryHasSceneDevices(studio::DeviceType category) {
  for (size_t i = 0; i < studio::devices().count(); ++i) {
    const studio::DeviceRecord* record = studio::devices().at(i);
    if (record == nullptr) {
      continue;
    }
    const studio::DriverDescriptor* descriptor =
        studio::DriverCatalog::find(record->driverId);
    if (descriptor == nullptr || descriptor->type != category) {
      continue;
    }
    if (deviceSupportsSceneActions(*record, *descriptor)) {
      return true;
    }
  }
  return false;
}

void refreshAddPicker();

void closeAddOverlay() {
  if (addOverlay != nullptr) {
    lv_obj_add_flag(addOverlay, LV_OBJ_FLAG_HIDDEN);
  }
  addLevel = AddLevel::Category;
  addCategory = studio::DeviceType::Camera;
  addDeviceId = studio::kInvalidInstanceId;
}

void onAddWait(lv_event_t*) {
  studio::SceneRecord record;
  if (!loadEditable(record)) {
    return;
  }
  studio::SceneStep* steps = editingStart ? record.startSteps : record.stopSteps;
  uint8_t& count = editingStart ? record.startCount : record.stopCount;
  if (count >= CONFIG_MAX_SCENE_STEPS) {
    return;
  }
  steps[count++] = studio::makeWaitStep(500);
  studio::scenes().replace(record);
  closeAddOverlay();
  refreshEdit();
}

void onAddRecordAction(lv_event_t* event) {
  const auto command = static_cast<studio::CommandType>(
      reinterpret_cast<uintptr_t>(lv_event_get_user_data(event)));
  if (addDeviceId == studio::kInvalidInstanceId) {
    return;
  }
  studio::SceneRecord record;
  if (!loadEditable(record)) {
    return;
  }
  studio::SceneStep* steps = editingStart ? record.startSteps : record.stopSteps;
  uint8_t& count = editingStart ? record.startCount : record.stopCount;
  if (count >= CONFIG_MAX_SCENE_STEPS) {
    return;
  }
  steps[count++] = studio::makeActionStep(addDeviceId, command);
  studio::scenes().replace(record);
  closeAddOverlay();
  refreshEdit();
}

void onChooseCategory(lv_event_t* event) {
  addCategory = static_cast<studio::DeviceType>(
      reinterpret_cast<uintptr_t>(lv_event_get_user_data(event)));
  addLevel = AddLevel::Device;
  addDeviceId = studio::kInvalidInstanceId;
  refreshAddPicker();
}

void onChooseDevice(lv_event_t* event) {
  addDeviceId = static_cast<studio::InstanceId>(
      reinterpret_cast<uintptr_t>(lv_event_get_user_data(event)));
  addLevel = AddLevel::Action;
  refreshAddPicker();
}

void onAddPickerBack(lv_event_t*) {
  if (addLevel == AddLevel::Action) {
    addLevel = AddLevel::Device;
    addDeviceId = studio::kInvalidInstanceId;
    refreshAddPicker();
    return;
  }
  if (addLevel == AddLevel::Device) {
    addLevel = AddLevel::Category;
    refreshAddPicker();
  }
}

void onMoveUp(lv_event_t* event) {
  const uint8_t index = static_cast<uint8_t>(
      reinterpret_cast<uintptr_t>(lv_event_get_user_data(event)));
  studio::SceneRecord record;
  if (!loadEditable(record) || index == 0) {
    return;
  }
  studio::SceneStep* steps = editingStart ? record.startSteps : record.stopSteps;
  const studio::SceneStep tmp = steps[index - 1];
  steps[index - 1] = steps[index];
  steps[index] = tmp;
  studio::scenes().replace(record);
  refreshEdit();
}

void onMoveDown(lv_event_t* event) {
  const uint8_t index = static_cast<uint8_t>(
      reinterpret_cast<uintptr_t>(lv_event_get_user_data(event)));
  studio::SceneRecord record;
  if (!loadEditable(record)) {
    return;
  }
  uint8_t& count = editingStart ? record.startCount : record.stopCount;
  if (index + 1 >= count) {
    return;
  }
  studio::SceneStep* steps = editingStart ? record.startSteps : record.stopSteps;
  const studio::SceneStep tmp = steps[index + 1];
  steps[index + 1] = steps[index];
  steps[index] = tmp;
  studio::scenes().replace(record);
  refreshEdit();
}

void onDeleteStep(lv_event_t* event) {
  const uint8_t index = static_cast<uint8_t>(
      reinterpret_cast<uintptr_t>(lv_event_get_user_data(event)));
  studio::SceneRecord record;
  if (!loadEditable(record)) {
    return;
  }
  studio::SceneStep* steps = editingStart ? record.startSteps : record.stopSteps;
  uint8_t& count = editingStart ? record.startCount : record.stopCount;
  if (index >= count) {
    return;
  }
  for (uint8_t i = index + 1; i < count; ++i) {
    steps[i - 1] = steps[i];
  }
  steps[count - 1] = studio::SceneStep{};
  --count;
  studio::scenes().replace(record);
  refreshEdit();
}

void refreshAddPicker() {
  lv_obj_clean(addList);
  const bool atRoot = addLevel == AddLevel::Category;
  if (addBackButton != nullptr) {
    if (atRoot) {
      lv_obj_add_flag(addBackButton, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_clear_flag(addBackButton, LV_OBJ_FLAG_HIDDEN);
    }
  }
  if (addCloseButton != nullptr) {
    if (atRoot) {
      lv_obj_clear_flag(addCloseButton, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(addCloseButton, LV_OBJ_FLAG_HIDDEN);
    }
  }

  if (addLevel == AddLevel::Category) {
    lv_label_set_text(addTitle, "Add step");
    lv_obj_t* wait = makeButton(addList, "Wait 500ms", onAddWait, kColAccent);
    lv_obj_set_size(wait, lv_pct(100), 30);

    constexpr studio::DeviceType kCategories[] = {
        studio::DeviceType::Camera,
        studio::DeviceType::Recorder,
        studio::DeviceType::Motion,
        studio::DeviceType::Light,
        studio::DeviceType::Unknown,
    };
    for (studio::DeviceType category : kCategories) {
      if (!categoryHasSceneDevices(category)) {
        continue;
      }
      void* userData =
          reinterpret_cast<void*>(static_cast<uintptr_t>(category));
      lv_obj_t* button =
          makeButton(addList, categoryName(category), onChooseCategory);
      lv_obj_set_size(button, lv_pct(100), 30);
      lv_obj_remove_event_cb(button, onChooseCategory);
      lv_obj_add_event_cb(button, onChooseCategory, LV_EVENT_CLICKED, userData);
    }
    return;
  }

  if (addLevel == AddLevel::Device) {
    lv_label_set_text(addTitle, categoryName(addCategory));
    bool any = false;
    for (size_t i = 0; i < studio::devices().count(); ++i) {
      const studio::DeviceRecord* record = studio::devices().at(i);
      if (record == nullptr) {
        continue;
      }
      const studio::DriverDescriptor* descriptor =
          studio::DriverCatalog::find(record->driverId);
      if (descriptor == nullptr || descriptor->type != addCategory ||
          !deviceSupportsSceneActions(*record, *descriptor)) {
        continue;
      }
      any = true;
      void* userData = reinterpret_cast<void*>(
          static_cast<uintptr_t>(record->instanceId));
      lv_obj_t* button =
          makeButton(addList, record->displayName, onChooseDevice);
      lv_obj_set_size(button, lv_pct(100), 30);
      lv_obj_remove_event_cb(button, onChooseDevice);
      lv_obj_add_event_cb(button, onChooseDevice, LV_EVENT_CLICKED, userData);
    }
    if (!any) {
      lv_obj_t* empty = lv_label_create(addList);
      lv_label_set_text(empty, "No devices");
      lv_obj_set_style_text_font(empty, UI_FONT_14, 0);
      lv_obj_set_style_text_color(empty, lv_color_hex(kColMuted), 0);
    }
    return;
  }

  const studio::DeviceRecord* device = studio::devices().find(addDeviceId);
  const studio::DriverDescriptor* descriptor =
      device != nullptr ? studio::DriverCatalog::find(device->driverId)
                        : nullptr;
  lv_label_set_text(addTitle,
                    device != nullptr ? device->displayName : "Action");
  if (device == nullptr || descriptor == nullptr) {
    lv_obj_t* empty = lv_label_create(addList);
    lv_label_set_text(empty, "Missing device");
    lv_obj_set_style_text_font(empty, UI_FONT_14, 0);
    lv_obj_set_style_text_color(empty, lv_color_hex(kColMuted), 0);
    return;
  }
  if ((descriptor->capabilities &
       studio::capabilityBit(studio::Capability::RecordStart)) != 0) {
    void* userData = reinterpret_cast<void*>(
        static_cast<uintptr_t>(studio::CommandType::RecordStart));
    lv_obj_t* button =
        makeButton(addList, "Record Start", onAddRecordAction, kColOk);
    lv_obj_set_size(button, lv_pct(100), 34);
    lv_obj_remove_event_cb(button, onAddRecordAction);
    lv_obj_add_event_cb(button, onAddRecordAction, LV_EVENT_CLICKED, userData);
  }
  if ((descriptor->capabilities &
       studio::capabilityBit(studio::Capability::RecordStop)) != 0) {
    void* userData = reinterpret_cast<void*>(
        static_cast<uintptr_t>(studio::CommandType::RecordStop));
    lv_obj_t* button =
        makeButton(addList, "Record Stop", onAddRecordAction, kColDanger);
    lv_obj_set_size(button, lv_pct(100), 34);
    lv_obj_remove_event_cb(button, onAddRecordAction);
    lv_obj_add_event_cb(button, onAddRecordAction, LV_EVENT_CLICKED, userData);
  }
}

void onOpenAddStep(lv_event_t*) {
  if (addOverlay == nullptr) {
    return;
  }
  addLevel = AddLevel::Category;
  addDeviceId = studio::kInvalidInstanceId;
  refreshAddPicker();
  lv_obj_clear_flag(addOverlay, LV_OBJ_FLAG_HIDDEN);
}

void onCloseAddStep(lv_event_t*) { closeAddOverlay(); }

void refreshList() {
  lv_obj_clean(listBody);
  if (studio::scenes().count() == 0) {
    lv_obj_t* empty = lv_label_create(listBody);
    lv_label_set_text(empty, "No sequences");
    lv_obj_set_style_text_font(empty, UI_FONT_14, 0);
    lv_obj_set_style_text_color(empty, lv_color_hex(kColMuted), 0);
  }
  for (size_t i = 0; i < studio::scenes().count(); ++i) {
    const studio::SceneRecord* record = studio::scenes().at(i);
    if (record == nullptr) {
      continue;
    }
    void* userData =
        reinterpret_cast<void*>(static_cast<uintptr_t>(record->sceneId));
    lv_obj_t* row = makeButton(listBody, record->name, onOpenScene, kColPanel);
    lv_obj_set_size(row, lv_pct(100), 34);
    lv_obj_remove_event_cb(row, onOpenScene);
    lv_obj_add_event_cb(row, onOpenScene, LV_EVENT_CLICKED, userData);
  }
}

void refreshRun() {
  const studio::SceneRecord* record = studio::scenes().find(currentScene);
  if (record == nullptr) {
    lv_label_set_text(runTitle, "Missing");
    return;
  }
  lv_label_set_text(runTitle, record->name);
  const studio::SceneProgress& progress = studio::scenes().progress();
  const bool forScene = progress.sceneId == currentScene;
  lv_label_set_text(runPhase, forScene ? phaseText(progress.phase) : "Ready");
  lv_label_set_text(runDetail, forScene ? progress.detail : "Start or edit");

  const bool busy = studio::scenes().busy();
  const bool armed = forScene && (progress.phase == studio::ScenePhase::IdleArmed ||
                                  progress.phase == studio::ScenePhase::Failed);
  if (busy && progress.phase == studio::ScenePhase::RunningStart) {
    lv_obj_add_state(startButton, LV_STATE_DISABLED);
  } else if (!busy && record->startCount > 0) {
    lv_obj_clear_state(startButton, LV_STATE_DISABLED);
  } else {
    lv_obj_add_state(startButton, LV_STATE_DISABLED);
  }
  if ((busy && progress.phase == studio::ScenePhase::RunningStop) ||
      (!busy && !armed && progress.phase != studio::ScenePhase::RunningStart &&
       progress.phase != studio::ScenePhase::Connecting)) {
    if (armed || progress.phase == studio::ScenePhase::RunningStart ||
        progress.phase == studio::ScenePhase::Connecting ||
        progress.phase == studio::ScenePhase::IdleArmed) {
      lv_obj_clear_state(stopButton, LV_STATE_DISABLED);
    } else {
      lv_obj_add_state(stopButton, LV_STATE_DISABLED);
    }
  } else {
    lv_obj_clear_state(stopButton, LV_STATE_DISABLED);
  }
}

void refreshEdit() {
  const studio::SceneRecord* record = studio::scenes().find(currentScene);
  if (record == nullptr) {
    return;
  }
  lv_label_set_text(editTitle, editingStart ? "Start steps" : "Stop steps");
  lv_obj_clean(editBody);
  const studio::SceneStep* steps =
      editingStart ? record->startSteps : record->stopSteps;
  const uint8_t count = editingStart ? record->startCount : record->stopCount;
  if (count == 0) {
    lv_obj_t* empty = lv_label_create(editBody);
    lv_label_set_text(empty, "No steps");
    lv_obj_set_style_text_color(empty, lv_color_hex(kColMuted), 0);
    lv_obj_set_style_text_font(empty, UI_FONT_14, 0);
  }
  for (uint8_t i = 0; i < count; ++i) {
    char text[48];
    formatStep(text, sizeof(text), steps[i]);
    lv_obj_t* row = lv_obj_create(editBody);
    lv_obj_set_size(row, lv_pct(100), 34);
    lv_obj_set_style_bg_color(row, lv_color_hex(kColPanel), 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_radius(row, 7, 0);
    lv_obj_set_style_pad_all(row, 2, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* label = lv_label_create(row);
    lv_label_set_text(label, text);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(label, 96);
    lv_obj_set_style_text_font(label, UI_FONT_14, 0);
    lv_obj_align(label, LV_ALIGN_LEFT_MID, 2, 0);

    void* userData = reinterpret_cast<void*>(static_cast<uintptr_t>(i));
    lv_obj_t* up = makeButton(row, LV_SYMBOL_UP, onMoveUp);
    lv_obj_set_size(up, 28, 28);
    lv_obj_align(up, LV_ALIGN_RIGHT_MID, -60, 0);
    lv_obj_remove_event_cb(up, onMoveUp);
    lv_obj_add_event_cb(up, onMoveUp, LV_EVENT_CLICKED, userData);

    lv_obj_t* down = makeButton(row, LV_SYMBOL_DOWN, onMoveDown);
    lv_obj_set_size(down, 28, 28);
    lv_obj_align(down, LV_ALIGN_RIGHT_MID, -30, 0);
    lv_obj_remove_event_cb(down, onMoveDown);
    lv_obj_add_event_cb(down, onMoveDown, LV_EVENT_CLICKED, userData);

    lv_obj_t* del = makeButton(row, LV_SYMBOL_TRASH, onDeleteStep, kColDanger);
    lv_obj_set_size(del, 28, 28);
    lv_obj_align(del, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_remove_event_cb(del, onDeleteStep);
    lv_obj_add_event_cb(del, onDeleteStep, LV_EVENT_CLICKED, userData);
  }
}

void buildList() {
  scrList = lv_obj_create(nullptr);
  styleScreen(scrList);

  lv_obj_t* back = makeButton(scrList, LV_SYMBOL_LEFT, onBackToHome);
  lv_obj_set_size(back, 34, 30);
  lv_obj_align(back, LV_ALIGN_TOP_LEFT, kRoundBackX, kRoundBackY);

  lv_obj_t* title = lv_label_create(scrList);
  lv_label_set_text(title, "Scenes");
  lv_obj_set_style_text_font(title, UI_FONT_16, 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 12, 42);

  listBody = lv_obj_create(scrList);
  lv_obj_set_size(listBody, 188, 96);
  lv_obj_align(listBody, LV_ALIGN_TOP_MID, 0, 68);
  lv_obj_set_style_bg_opa(listBody, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(listBody, 0, 0);
  lv_obj_set_style_pad_row(listBody, 4, 0);
  lv_obj_set_flex_flow(listBody, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scroll_dir(listBody, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(listBody, LV_SCROLLBAR_MODE_OFF);

  lv_obj_t* seed = makeButton(scrList, "Press Record", onSeedPressRecord, kColAccent);
  lv_obj_set_size(seed, 108, 28);
  lv_obj_align(seed, LV_ALIGN_BOTTOM_MID, -42, -16);
  lv_obj_t* add = makeButton(scrList, "+", onAddBlank);
  lv_obj_set_size(add, 40, 28);
  lv_obj_align(add, LV_ALIGN_BOTTOM_MID, 58, -16);
}

void buildRun() {
  scrRun = lv_obj_create(nullptr);
  styleScreen(scrRun);

  lv_obj_t* back = makeButton(scrRun, LV_SYMBOL_LEFT, onBackToList);
  lv_obj_set_size(back, 34, 30);
  lv_obj_align(back, LV_ALIGN_TOP_LEFT, kRoundBackX, kRoundBackY);

  runTitle = lv_label_create(scrRun);
  lv_obj_set_width(runTitle, 140);
  lv_label_set_long_mode(runTitle, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_font(runTitle, UI_FONT_16, 0);
  lv_obj_set_style_text_align(runTitle, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(runTitle, LV_ALIGN_TOP_MID, 10, 40);

  runPhase = lv_label_create(scrRun);
  lv_obj_set_style_text_font(runPhase, UI_FONT_20, 0);
  lv_obj_set_style_text_color(runPhase, lv_color_hex(kColAccent), 0);
  lv_obj_align(runPhase, LV_ALIGN_TOP_MID, 0, 72);

  runDetail = lv_label_create(scrRun);
  lv_obj_set_width(runDetail, 170);
  lv_label_set_long_mode(runDetail, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_font(runDetail, UI_FONT_14, 0);
  lv_obj_set_style_text_color(runDetail, lv_color_hex(kColMuted), 0);
  lv_obj_set_style_text_align(runDetail, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(runDetail, LV_ALIGN_TOP_MID, 0, 100);

  startButton = makeButton(scrRun, "Start", onStart, kColOk);
  lv_obj_set_size(startButton, 70, 34);
  lv_obj_align(startButton, LV_ALIGN_TOP_MID, -40, 130);

  stopButton = makeButton(scrRun, "Stop", onStop, kColDanger);
  lv_obj_set_size(stopButton, 70, 34);
  lv_obj_align(stopButton, LV_ALIGN_TOP_MID, 40, 130);

  lv_obj_t* editStart = makeButton(scrRun, "Edit Start", onEditStart);
  lv_obj_set_size(editStart, 78, 26);
  lv_obj_align(editStart, LV_ALIGN_BOTTOM_MID, -48, -42);

  lv_obj_t* editStop = makeButton(scrRun, "Edit Stop", onEditStop);
  lv_obj_set_size(editStop, 78, 26);
  lv_obj_align(editStop, LV_ALIGN_BOTTOM_MID, 48, -42);

  lv_obj_t* cancel = makeButton(scrRun, "Cancel", onCancel, kColPanel);
  lv_obj_set_size(cancel, 64, 24);
  lv_obj_align(cancel, LV_ALIGN_BOTTOM_MID, -40, -14);

  lv_obj_t* del = makeButton(scrRun, "Delete", onDeleteScene, kColDanger);
  lv_obj_set_size(del, 64, 24);
  lv_obj_align(del, LV_ALIGN_BOTTOM_MID, 40, -14);
}

void buildEdit() {
  scrEdit = lv_obj_create(nullptr);
  styleScreen(scrEdit);

  lv_obj_t* back = makeButton(scrEdit, LV_SYMBOL_LEFT, onBackToRun);
  lv_obj_set_size(back, 34, 30);
  lv_obj_align(back, LV_ALIGN_TOP_LEFT, kRoundBackX, kRoundBackY);

  editTitle = lv_label_create(scrEdit);
  lv_obj_set_style_text_font(editTitle, UI_FONT_16, 0);
  lv_obj_align(editTitle, LV_ALIGN_TOP_MID, 12, 42);

  editBody = lv_obj_create(scrEdit);
  lv_obj_set_size(editBody, 188, 118);
  lv_obj_align(editBody, LV_ALIGN_TOP_MID, 0, 68);
  lv_obj_set_style_bg_opa(editBody, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(editBody, 0, 0);
  lv_obj_set_style_pad_row(editBody, 3, 0);
  lv_obj_set_flex_flow(editBody, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scroll_dir(editBody, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(editBody, LV_SCROLLBAR_MODE_OFF);

  lv_obj_t* add = makeButton(scrEdit, "+ Step", onOpenAddStep, kColAccent);
  lv_obj_set_size(add, 90, 28);
  lv_obj_align(add, LV_ALIGN_BOTTOM_MID, 0, -16);

  addOverlay = lv_obj_create(lv_layer_top());
  lv_obj_set_size(addOverlay, 236, 236);
  lv_obj_center(addOverlay);
  lv_obj_set_style_radius(addOverlay, 118, 0);
  lv_obj_set_style_bg_color(addOverlay, lv_color_hex(kColBg), 0);
  lv_obj_set_style_border_color(addOverlay, lv_color_hex(kColAccent), 0);
  lv_obj_set_style_pad_all(addOverlay, 0, 0);
  lv_obj_clear_flag(addOverlay, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(addOverlay, LV_OBJ_FLAG_HIDDEN);

  addCloseButton = makeButton(addOverlay, LV_SYMBOL_CLOSE, onCloseAddStep);
  lv_obj_set_size(addCloseButton, 30, 30);
  lv_obj_align(addCloseButton, LV_ALIGN_TOP_LEFT, 34, 24);

  addBackButton = makeButton(addOverlay, LV_SYMBOL_LEFT, onAddPickerBack);
  lv_obj_set_size(addBackButton, 30, 30);
  lv_obj_align(addBackButton, LV_ALIGN_TOP_LEFT, 34, 24);
  lv_obj_add_flag(addBackButton, LV_OBJ_FLAG_HIDDEN);

  addTitle = lv_label_create(addOverlay);
  lv_label_set_text(addTitle, "Add step");
  lv_obj_set_style_text_font(addTitle, UI_FONT_16, 0);
  lv_obj_set_width(addTitle, 140);
  lv_label_set_long_mode(addTitle, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_align(addTitle, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(addTitle, LV_ALIGN_TOP_MID, 10, 30);

  addList = lv_obj_create(addOverlay);
  lv_obj_set_size(addList, 168, 140);
  lv_obj_align(addList, LV_ALIGN_TOP_MID, 0, 62);
  lv_obj_set_style_bg_opa(addList, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(addList, 0, 0);
  lv_obj_set_style_pad_row(addList, 3, 0);
  lv_obj_set_flex_flow(addList, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scroll_dir(addList, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(addList, LV_SCROLLBAR_MODE_OFF);
}

void ensureScreens() {
  if (scrList == nullptr) {
    buildList();
  }
  if (scrRun == nullptr) {
    buildRun();
  }
  if (scrEdit == nullptr) {
    buildEdit();
  }
}

void showListView() {
  ensureScreens();
  closeAddOverlay();
  view = View::List;
  refreshList();
  lv_scr_load(scrList);
}

void showRunView(studio::SceneId sceneId) {
  ensureScreens();
  closeAddOverlay();
  currentScene = sceneId;
  view = View::Run;
  refreshRun();
  lv_scr_load(scrRun);
}

void showEditView(studio::SceneId sceneId, bool startList) {
  ensureScreens();
  closeAddOverlay();
  currentScene = sceneId;
  editingStart = startList;
  view = View::Edit;
  refreshEdit();
  lv_scr_load(scrEdit);
}

}  // namespace

void init() {}

void tick() {
  if (!visible || view != View::Run) {
    return;
  }
  const uint32_t now = millis();
  if (now - lastRefreshMs < 200) {
    return;
  }
  lastRefreshMs = now;
  refreshRun();
}

bool active() { return visible; }

void show() {
  visible = true;
  lastRefreshMs = 0;
  ui::parkForScreenRebuild();
  showListView();
  ui::releaseInactiveScreens();
}

void hide() {
  if (!visible) {
    return;
  }
  closeAddOverlay();
  visible = false;
  view = View::List;
  currentScene = studio::kInvalidSceneId;
}

void handleShortPress() {
  if (!visible) {
    return;
  }
  if (addOverlay != nullptr && !lv_obj_has_flag(addOverlay, LV_OBJ_FLAG_HIDDEN)) {
    if (addLevel != AddLevel::Category) {
      onAddPickerBack(nullptr);
      return;
    }
    closeAddOverlay();
    return;
  }
  if (view == View::Edit) {
    showRunView(currentScene);
    return;
  }
  if (view == View::Run) {
    if (studio::scenes().busy()) {
      return;
    }
    showListView();
    return;
  }
  hide();
  ui::showHome();
}

#ifdef UI_SIMULATOR
void simShowList() {
  visible = true;
  showListView();
}

void simShowRun(studio::SceneId sceneId) {
  visible = true;
  showRunView(sceneId);
}

void simShowEditStart(studio::SceneId sceneId) {
  visible = true;
  showEditView(sceneId, true);
}

void simShowEditStop(studio::SceneId sceneId) {
  visible = true;
  showEditView(sceneId, false);
}

void simShowAddStepCategory(studio::SceneId sceneId) {
  visible = true;
  showEditView(sceneId, true);
  addLevel = AddLevel::Category;
  addDeviceId = studio::kInvalidInstanceId;
  refreshAddPicker();
  lv_obj_clear_flag(addOverlay, LV_OBJ_FLAG_HIDDEN);
}

void simShowAddStepDevice(studio::SceneId sceneId, studio::DeviceType category) {
  visible = true;
  showEditView(sceneId, true);
  addLevel = AddLevel::Device;
  addCategory = category;
  addDeviceId = studio::kInvalidInstanceId;
  refreshAddPicker();
  lv_obj_clear_flag(addOverlay, LV_OBJ_FLAG_HIDDEN);
}

void simShowAddStepAction(studio::SceneId sceneId, studio::InstanceId instanceId) {
  visible = true;
  showEditView(sceneId, true);
  const studio::DeviceRecord* record = studio::devices().find(instanceId);
  const studio::DriverDescriptor* descriptor =
      record != nullptr ? studio::DriverCatalog::find(record->driverId) : nullptr;
  addLevel = AddLevel::Action;
  addCategory = descriptor != nullptr ? descriptor->type : studio::DeviceType::Camera;
  addDeviceId = instanceId;
  refreshAddPicker();
  lv_obj_clear_flag(addOverlay, LV_OBJ_FLAG_HIDDEN);
}
#endif

}  // namespace scene_ui
