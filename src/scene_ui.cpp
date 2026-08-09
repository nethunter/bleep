#include "scene_ui.h"

#include <Arduino.h>
#include <lvgl.h>

#include <cstdio>
#include <cstring>

#include "assets/ui_icons.h"
#include "core/device_manager.h"
#include "core/driver_catalog.h"
#include "core/scene_service.h"
#include "fonts/ui_fonts.h"
#include "haptic_feedback.h"
#include "ui.h"
#include "ui/picker_shell.h"
#include "ui/round_page.h"

namespace scene_ui {
namespace {

constexpr uint32_t kColBg = 0x05070A;
constexpr uint32_t kColPanel = 0x12161D;
constexpr uint32_t kColAccent = 0x35C7F2;
constexpr uint32_t kColText = 0xF3F4F6;
constexpr uint32_t kColMuted = 0x8A94A6;
constexpr uint32_t kColDanger = 0xF26D6D;
constexpr uint32_t kColOk = 0x3DDC97;
constexpr uint32_t kColConnecting = 0x35C7F2;

enum class View : uint8_t { List, Run, Edit };

View view = View::List;
bool visible = false;
bool borrowedDeviceOpen = false;
studio::SceneId currentScene = studio::kInvalidSceneId;
bool editingStart = true;
constexpr uint8_t kNoStepIndex = 0xff;
uint8_t editingStepIndex = kNoStepIndex;
uint32_t lastRefreshMs = 0;

lv_obj_t* scrList = nullptr;
lv_obj_t* scrRun = nullptr;
lv_obj_t* scrEdit = nullptr;
lv_obj_t* listBody = nullptr;
lv_obj_t* runTitle = nullptr;
lv_obj_t* runPhase = nullptr;
lv_obj_t* runChipRow = nullptr;
lv_obj_t* startButton = nullptr;
lv_obj_t* stopButton = nullptr;
lv_obj_t* prepareCancelButton = nullptr;
lv_obj_t* settingsButton = nullptr;
lv_obj_t* settingsOverlay = nullptr;
lv_obj_t* settingsTitle = nullptr;
lv_obj_t* settingsBody = nullptr;
bool deleteSceneArmed = false;
lv_obj_t* editBody = nullptr;
lv_obj_t* editTitle = nullptr;

enum class ChipState : uint8_t {
  Unknown,
  Disconnected,
  Connecting,
  Ready,
  Failed,
};

struct RunChip {
  studio::InstanceId instanceId = studio::kInvalidInstanceId;
  lv_obj_t* button = nullptr;
  lv_obj_t* icon = nullptr;
  lv_obj_t* ring = nullptr;
  ChipState state = ChipState::Unknown;
};

RunChip runChips[CONFIG_MAX_ACTIVE_INSTANCES] = {};
uint8_t runChipCount = 0;

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

studio::InstanceId eventInstance(lv_event_t* event) {
  return static_cast<studio::InstanceId>(
      reinterpret_cast<uintptr_t>(lv_event_get_user_data(event)));
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
    case studio::DeviceType::Switch:
      return &ui_icon_cat_switches;
    case studio::DeviceType::Action:
      return &ui_icon_devices;
    case studio::DeviceType::Unknown:
      return &ui_icon_devices;
  }
  return &ui_icon_devices;
}

void setChipBorderOpacity(void* object, int32_t value) {
  lv_obj_set_style_border_opa(static_cast<lv_obj_t*>(object),
                              static_cast<lv_opa_t>(value), 0);
}

void stopChipAnimation(RunChip& chip) {
  if (chip.ring != nullptr) {
    lv_anim_del(chip.ring, setChipBorderOpacity);
  }
}

const char* phaseText(studio::ScenePhase phase) {
  switch (phase) {
    case studio::ScenePhase::Idle:
      return "Idle";
    case studio::ScenePhase::Connecting:
      return "Connecting";
    case studio::ScenePhase::Ready:
      return "Ready";
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
  return "Idle";
}

uint32_t phaseColor(studio::ScenePhase phase, bool targetsReady) {
  if (!targetsReady &&
      (phase == studio::ScenePhase::Ready ||
       phase == studio::ScenePhase::IdleArmed ||
       phase == studio::ScenePhase::Completed)) {
    return kColDanger;
  }
  switch (phase) {
    case studio::ScenePhase::Connecting:
    case studio::ScenePhase::RunningStart:
    case studio::ScenePhase::RunningStop:
      return kColAccent;
    case studio::ScenePhase::Ready:
    case studio::ScenePhase::Completed:
      return kColOk;
    case studio::ScenePhase::IdleArmed:
    case studio::ScenePhase::Failed:
      return kColDanger;
    case studio::ScenePhase::Idle:
      return kColMuted;
  }
  return kColMuted;
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
      step.command == studio::CommandType::RecordTrigger
          ? "Shutter"
          : step.command == studio::CommandType::RecordStart
          ? "Rec"
          : (step.command == studio::CommandType::RecordStop
                 ? "Stop"
                 : (step.command == studio::CommandType::SetLightCct
                        ? "Set color"
                        : (step.command == studio::CommandType::SetLightRgb
                               ? "Set color"
                               : (step.command == studio::CommandType::TurnOn
                                      ? "On"
                                      : (step.command == studio::CommandType::TurnOff
                                             ? "Off"
                                             : "Cmd")))));
  std::snprintf(buffer, capacity, "%s %s", command, name);
}

void ensureScreens();
void refreshList();
void refreshRun();
void refreshEdit();
void refreshEditDeferred(void*) {
  if (visible && view == View::Edit && editBody != nullptr) refreshEdit();
}
void showListView();
void showRunView(studio::SceneId sceneId);
void showEditView(studio::SceneId sceneId, bool startList);
void closeSettings();
void releaseHeldScene();

uint8_t collectTargets(const studio::SceneRecord& record,
                       studio::InstanceId (&targets)[CONFIG_MAX_ACTIVE_INSTANCES]) {
  uint8_t count = 0;
  const studio::SceneStep* lists[] = {record.startSteps, record.stopSteps};
  const uint8_t counts[] = {record.startCount, record.stopCount};
  for (size_t list = 0; list < 2; ++list) {
    for (uint8_t i = 0; i < counts[list]; ++i) {
      const studio::SceneStep& step = lists[list][i];
      if (step.type != studio::SceneStepType::Action) {
        continue;
      }
      bool known = false;
      for (uint8_t target = 0; target < count; ++target) {
        if (targets[target] == step.targetId) {
          known = true;
          break;
        }
      }
      if (!known && count < CONFIG_MAX_ACTIVE_INSTANCES) {
        targets[count++] = step.targetId;
      }
    }
  }
  return count;
}

bool allTargetsReady(const studio::SceneRecord& record) {
  studio::InstanceId targets[CONFIG_MAX_ACTIVE_INSTANCES] = {};
  const uint8_t count = collectTargets(record, targets);
  if (count == 0) {
    return false;
  }
  for (uint8_t i = 0; i < count; ++i) {
    const studio::DeviceRuntimeState runtime =
        studio::devices().runtimeState(targets[i]);
    if (runtime.link != studio::LinkState::Connected || !runtime.protocolReady) {
      return false;
    }
  }
  return true;
}

void onBackToHome(lv_event_t*) {
  haptic_feedback::request(haptic_feedback::Pattern::Back);
  hide();
  ui::showHome();
}

void onBackToList(lv_event_t*) {
  haptic_feedback::request(haptic_feedback::Pattern::Back);
  releaseHeldScene();
  showListView();
}

void onBackToRun(lv_event_t*) {
  haptic_feedback::request(haptic_feedback::Pattern::Back);
  showRunView(currentScene);
}

void onOpenScene(lv_event_t* event) { showRunView(eventScene(event)); }

void onOpenDeviceControl(lv_event_t* event) {
  const studio::SceneProgress& progress = studio::scenes().progress();
  if (progress.phase == studio::ScenePhase::RunningStart ||
      progress.phase == studio::ScenePhase::RunningStop) {
    return;
  }
  const studio::InstanceId instanceId = eventInstance(event);
  if (!studio::devices().isActive(instanceId)) {
    return;
  }
  borrowedDeviceOpen = true;
  ui::showDevice(instanceId);
}

void onAddBlank(lv_event_t*) {
  if (studio::scenes().busy() || studio::scenes().holdsLinks()) {
    return;
  }
  char name[studio::kDeviceNameCapacity];
  std::snprintf(name, sizeof(name), "Sequence %u",
                static_cast<unsigned>(studio::scenes().count() + 1));
  studio::SceneId id = studio::kInvalidSceneId;
  if (studio::scenes().add(name, id) == studio::SceneRegistryStatus::Ok) {
    showEditView(id, true);
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

bool canStartSequence(const studio::SceneRecord& record,
                      const studio::SceneProgress& progress) {
  const bool forScene = progress.sceneId == currentScene;
  const bool ready = forScene && progress.phase == studio::ScenePhase::Ready;
  return !studio::scenes().busy() && allTargetsReady(record) &&
         record.startCount > 0 &&
         (ready || progress.phase == studio::ScenePhase::Idle ||
          progress.phase == studio::ScenePhase::Completed || !forScene);
}

bool canStopSequence(const studio::SceneRecord& record,
                     const studio::SceneProgress& progress) {
  if (progress.sceneId != currentScene) {
    return false;
  }
  const bool armed = (progress.phase == studio::ScenePhase::IdleArmed ||
                      progress.phase == studio::ScenePhase::Failed) &&
                     allTargetsReady(record);
  return armed || progress.phase == studio::ScenePhase::RunningStart ||
         (progress.phase == studio::ScenePhase::Connecting &&
          progress.runningStart);
}

bool sequenceHoldsOrBusy() {
  const studio::SceneProgress& progress = studio::scenes().progress();
  if (progress.sceneId != currentScene) {
    return false;
  }
  return studio::scenes().holdsLinks() || studio::scenes().busy() ||
         progress.phase == studio::ScenePhase::Ready ||
         progress.phase == studio::ScenePhase::IdleArmed ||
         progress.phase == studio::ScenePhase::Failed;
}

void onPrepareOrCancel(lv_event_t*) {
  if (currentScene == studio::kInvalidSceneId) {
    return;
  }
  if (sequenceHoldsOrBusy()) {
    studio::scenes().cancel();
  } else {
    studio::scenes().prepare(currentScene);
  }
  refreshRun();
}

bool canMutateScene() {
  if (studio::scenes().busy()) {
    return false;
  }
  const studio::SceneProgress& progress = studio::scenes().progress();
  if (progress.sceneId == currentScene &&
      progress.phase == studio::ScenePhase::IdleArmed) {
    return false;
  }
  return true;
}

bool canDeleteScene() {
  const studio::SceneProgress& progress = studio::scenes().progress();
  if (progress.sceneId != currentScene) {
    return true;
  }
  return progress.phase != studio::ScenePhase::RunningStart &&
         progress.phase != studio::ScenePhase::RunningStop &&
         progress.phase != studio::ScenePhase::IdleArmed &&
         !(progress.phase == studio::ScenePhase::Connecting &&
           progress.runningStart);
}

void refreshSettingsBody();

void onEditStart(lv_event_t*) {
  if (!canMutateScene()) {
    return;
  }
  closeSettings();
  // Keep prepared links held while editing steps.
  showEditView(currentScene, true);
}

void onEditStop(lv_event_t*) {
  if (!canMutateScene()) {
    return;
  }
  closeSettings();
  showEditView(currentScene, false);
}

void onDeleteScene(lv_event_t*) {
  if (!canDeleteScene()) {
    return;
  }
  if (!deleteSceneArmed) {
    deleteSceneArmed = true;
    refreshSettingsBody();
    return;
  }
  closeSettings();
  const studio::SceneProgress& progress = studio::scenes().progress();
  if (studio::scenes().holdsLinks() || progress.sceneId == currentScene) {
    studio::scenes().cancel();
  }
  if (studio::scenes().remove(currentScene) !=
      studio::SceneRegistryStatus::Ok) {
    return;
  }
  currentScene = studio::kInvalidSceneId;
  showListView();
}

void onSceneRenameDone(const char* name) {
  if (currentScene != studio::kInvalidSceneId && name != nullptr) {
    studio::scenes().rename(currentScene, name);
  }
  refreshRun();
}

void onRenameScene(lv_event_t*) {
  if (!canMutateScene()) {
    return;
  }
  const studio::SceneRecord* record = studio::scenes().find(currentScene);
  if (record == nullptr) {
    return;
  }
  closeSettings();
  ui::promptRename(record->name, onSceneRenameDone);
}

void closeSettings() {
  if (settingsOverlay != nullptr) {
    lv_obj_del(settingsOverlay);
    settingsOverlay = nullptr;
  }
  settingsTitle = nullptr;
  settingsBody = nullptr;
  deleteSceneArmed = false;
}

void onCloseSettings(lv_event_t*) { closeSettings(); }

void onCancelDeleteScene(lv_event_t*) {
  deleteSceneArmed = false;
  refreshSettingsBody();
}

void buildSettingsOverlay() {
  closeSettings();
  settingsOverlay = lv_obj_create(lv_layer_top());
  lv_obj_set_size(settingsOverlay, 240, 240);
  lv_obj_center(settingsOverlay);
  lv_obj_set_style_radius(settingsOverlay, 120, 0);
  lv_obj_set_style_bg_color(settingsOverlay, lv_color_hex(kColBg), 0);
  lv_obj_set_style_border_width(settingsOverlay, 0, 0);
  lv_obj_set_style_pad_all(settingsOverlay, 0, 0);
  lv_obj_clear_flag(settingsOverlay, LV_OBJ_FLAG_SCROLLABLE);

  const studio::SceneRecord* record = studio::scenes().find(currentScene);
  studio_ui::RoundPageHeaderOptions header;
  header.title = record != nullptr ? record->name : "Sequence";
  header.backSymbol = LV_SYMBOL_CLOSE;
  header.onBack = onCloseSettings;
  header.panelColor = kColPanel;
  header.textColor = kColText;
  settingsTitle =
      studio_ui::createRoundPageHeader(settingsOverlay, header).title;

  settingsBody = studio_ui::createRoundPageMenuBody(settingsOverlay);
  refreshSettingsBody();
}

void refreshSettingsBody() {
  if (settingsBody == nullptr || settingsTitle == nullptr) return;
  lv_obj_clean(settingsBody);

  const studio::SceneRecord* record = studio::scenes().find(currentScene);
  if (deleteSceneArmed) {
    lv_label_set_text(settingsTitle, "Delete?");
    lv_obj_t* warning = lv_label_create(settingsBody);
    lv_obj_set_width(warning, lv_pct(100));
    lv_label_set_long_mode(warning, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(warning, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(warning, UI_FONT_14, 0);
    lv_obj_set_style_text_color(warning, lv_color_hex(kColMuted), 0);
    char message[studio::kDeviceNameCapacity + 40];
    std::snprintf(message, sizeof(message), "Delete %s?\nThis cannot be undone.",
                  record != nullptr ? record->name : "this sequence");
    lv_label_set_text(warning, message);
    lv_obj_t* cancel =
        makeButton(settingsBody, "Cancel", onCancelDeleteScene);
    lv_obj_set_size(cancel, lv_pct(100), 32);
    lv_obj_t* confirm =
        makeButton(settingsBody, "Delete sequence", onDeleteScene, kColDanger);
    lv_obj_set_size(confirm, lv_pct(100), 32);
    return;
  }

  lv_label_set_text(settingsTitle, record != nullptr ? record->name : "Sequence");

  lv_obj_t* rename = makeButton(settingsBody, "Rename", onRenameScene);
  lv_obj_set_size(rename, lv_pct(100), 32);
  lv_obj_t* editStart = makeButton(settingsBody, "Edit Start", onEditStart);
  lv_obj_set_size(editStart, lv_pct(100), 32);
  lv_obj_t* editStop = makeButton(settingsBody, "Edit Stop", onEditStop);
  lv_obj_set_size(editStop, lv_pct(100), 32);
  lv_obj_t* del = makeButton(settingsBody, "Delete", onDeleteScene, kColDanger);
  lv_obj_set_size(del, lv_pct(100), 32);
  if (!canDeleteScene()) {
    lv_obj_add_state(del, LV_STATE_DISABLED);
  }
}

void onOpenSettings(lv_event_t*) {
  if (ui::renamePromptActive()) {
    return;
  }
  const studio::SceneProgress& progress = studio::scenes().progress();
  const bool activeRun =
      progress.sceneId == currentScene &&
      (progress.phase == studio::ScenePhase::RunningStart ||
       progress.phase == studio::ScenePhase::RunningStop ||
       progress.phase == studio::ScenePhase::IdleArmed ||
       (progress.phase == studio::ScenePhase::Failed &&
        progress.stepCount > 0) ||
       (progress.phase == studio::ScenePhase::Connecting &&
        progress.runningStart));
  if (!activeRun && progress.sceneId == currentScene &&
      (studio::scenes().holdsLinks() || studio::scenes().busy() ||
       progress.phase != studio::ScenePhase::Idle)) {
    studio::scenes().cancel();
    refreshRun();
  }
  buildSettingsOverlay();
}

void releaseHeldScene() {
  const studio::SceneProgress& progress = studio::scenes().progress();
  if (progress.phase == studio::ScenePhase::RunningStop) {
    return;
  }
  if (studio::scenes().holdsLinks() ||
      progress.sceneId != studio::kInvalidSceneId) {
    studio::scenes().cancel();
  }
}

bool loadEditable(studio::SceneRecord& record) {
  const studio::SceneRecord* source = studio::scenes().find(currentScene);
  if (source == nullptr) {
    return false;
  }
  record = *source;
  return true;
}

void closeAddOverlay() {
  if (picker_shell::active()) {
    picker_shell::hide();
  }
}

void saveSceneStep(const studio::SceneStep& step) {
  studio::SceneRecord record;
  if (!loadEditable(record)) {
    return;
  }
  studio::SceneStep* steps = editingStart ? record.startSteps : record.stopSteps;
  uint8_t& count = editingStart ? record.startCount : record.stopCount;
  if (editingStepIndex == kNoStepIndex) {
    if (count >= CONFIG_MAX_SCENE_STEPS) {
      return;
    }
    steps[count++] = step;
  } else {
    if (editingStepIndex >= count) {
      return;
    }
    steps[editingStepIndex] = step;
  }
  studio::scenes().replace(record);
  refreshEdit();
}

void onSceneWaitChosen(uint32_t waitMs) {
  saveSceneStep(studio::makeWaitStep(waitMs));
}

void onSceneActionChosen(studio::InstanceId instanceId,
                         studio::CommandType command, int32_t value0,
                         int32_t value1, int32_t value2) {
  saveSceneStep(studio::makeActionStep(instanceId, command, value0, value1,
                                       value2));
}

void onScenePickerClosed() { editingStepIndex = kNoStepIndex; }

picker_shell::Callbacks scenePickerCallbacks() {
  picker_shell::Callbacks callbacks;
  callbacks.onSceneAction = onSceneActionChosen;
  callbacks.onWaitChosen = onSceneWaitChosen;
  callbacks.onClosed = onScenePickerClosed;
  return callbacks;
}

void onOpenAddStep(lv_event_t*) {
  editingStepIndex = kNoStepIndex;
  picker_shell::show(picker_shell::Mode::SceneStep, scenePickerCallbacks());
}

void onEditStep(lv_event_t* event) {
  const uint8_t index = static_cast<uint8_t>(
      reinterpret_cast<uintptr_t>(lv_event_get_user_data(event)));
  const studio::SceneRecord* record = studio::scenes().find(currentScene);
  if (record == nullptr) {
    return;
  }
  const studio::SceneStep* steps =
      editingStart ? record->startSteps : record->stopSteps;
  const uint8_t count = editingStart ? record->startCount : record->stopCount;
  if (index >= count) {
    return;
  }
  editingStepIndex = index;
  picker_shell::showSceneStep(steps[index], scenePickerCallbacks());
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
  if (studio::scenes().removeStep(currentScene, editingStart, index) ==
      studio::SceneRegistryStatus::Ok) {
    // The trash button belongs to editBody. Let LVGL finish dispatching this
    // click before rebuilding the tree that owns the event target.
    lv_async_call(refreshEditDeferred, nullptr);
  }
}

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
  const bool targetsReady = allTargetsReady(*record);
  const bool stablePhase = progress.phase == studio::ScenePhase::Ready ||
                           progress.phase == studio::ScenePhase::IdleArmed ||
                           progress.phase == studio::ScenePhase::Completed;
  const bool connectionFailed =
      forScene && progress.phase == studio::ScenePhase::Failed &&
      progress.lastStatus == studio::SceneRunStatus::ConnectTimeout;
  lv_label_set_text(
      runPhase,
      connectionFailed
          ? "Failed to connect"
          : (forScene && stablePhase && !targetsReady
                 ? "Not connected"
                 : (forScene ? phaseText(progress.phase) : "Idle")));
  lv_obj_set_style_text_color(
      runPhase,
      lv_color_hex(forScene ? phaseColor(progress.phase, targetsReady)
                            : kColMuted),
      0);

  studio::InstanceId targetIds[CONFIG_MAX_ACTIVE_INSTANCES] = {};
  const uint8_t targetCount = collectTargets(*record, targetIds);
  bool rebuildChips = targetCount != runChipCount;
  for (uint8_t i = 0; !rebuildChips && i < targetCount; ++i) {
    rebuildChips = runChips[i].instanceId != targetIds[i];
  }
  if (rebuildChips) {
    for (uint8_t i = 0; i < runChipCount; ++i) {
      stopChipAnimation(runChips[i]);
    }
    lv_obj_clean(runChipRow);
    runChipCount = targetCount;
    const lv_coord_t slotWidth =
        targetCount > 0 ? static_cast<lv_coord_t>(184 / targetCount) : 184;
    for (uint8_t i = 0; i < targetCount; ++i) {
      RunChip& chip = runChips[i];
      chip = RunChip{};
      chip.instanceId = targetIds[i];
      const studio::DeviceRecord* device = studio::devices().find(targetIds[i]);
      const studio::InstanceProfile profile =
          device != nullptr ? studio::devices().profile(device->instanceId)
                            : studio::InstanceProfile{};

      lv_obj_t* item = lv_obj_create(runChipRow);
      lv_obj_set_size(item, slotWidth, 59);
      lv_obj_set_pos(item, static_cast<lv_coord_t>(i * slotWidth), 0);
      lv_obj_set_style_bg_opa(item, LV_OPA_TRANSP, 0);
      lv_obj_set_style_border_width(item, 0, 0);
      lv_obj_set_style_pad_all(item, 0, 0);
      lv_obj_clear_flag(item, LV_OBJ_FLAG_SCROLLABLE);

      chip.button = lv_btn_create(item);
      lv_obj_set_size(chip.button, 40, 40);
      lv_obj_align(chip.button, LV_ALIGN_TOP_MID, 0, 0);
      lv_obj_set_style_radius(chip.button, LV_RADIUS_CIRCLE, 0);
      lv_obj_set_style_bg_color(chip.button, lv_color_hex(kColPanel), 0);
      lv_obj_set_style_shadow_width(chip.button, 0, 0);
      lv_obj_set_style_border_width(chip.button, 0, 0);
      lv_obj_set_style_pad_all(chip.button, 0, 0);
      void* userData = reinterpret_cast<void*>(
          static_cast<uintptr_t>(chip.instanceId));
      lv_obj_add_event_cb(chip.button, onOpenDeviceControl, LV_EVENT_CLICKED,
                          userData);

      chip.icon = lv_img_create(chip.button);
      lv_img_set_src(chip.icon,
                     categoryIcon(profile.type));
      lv_img_set_zoom(chip.icon, 176);
      lv_obj_center(chip.icon);

      // Draw the status ring after the icon. A parent border is rendered
      // before its children, so square icon artwork can otherwise cover the
      // ring at the corners even though transparent camera/recorder art does
      // not expose the layering error.
      chip.ring = lv_obj_create(chip.button);
      lv_obj_set_size(chip.ring, 40, 40);
      lv_obj_center(chip.ring);
      lv_obj_set_style_radius(chip.ring, LV_RADIUS_CIRCLE, 0);
      lv_obj_set_style_bg_opa(chip.ring, LV_OPA_TRANSP, 0);
      lv_obj_set_style_border_width(chip.ring, 2, 0);
      lv_obj_set_style_pad_all(chip.ring, 0, 0);
      lv_obj_clear_flag(chip.ring, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_clear_flag(chip.ring, LV_OBJ_FLAG_SCROLLABLE);

      lv_obj_t* name = lv_label_create(item);
      lv_label_set_text(name, device != nullptr ? device->displayName : "?");
      lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
      lv_obj_set_width(name, slotWidth - 2);
      lv_obj_set_height(name, 17);
      lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);
      lv_obj_set_style_text_font(name, UI_FONT_14, 0);
      lv_obj_set_style_text_color(name, lv_color_hex(kColMuted), 0);
      lv_obj_align(name, LV_ALIGN_BOTTOM_MID, 0, 0);
    }
    for (uint8_t i = targetCount; i < CONFIG_MAX_ACTIVE_INSTANCES; ++i) {
      runChips[i] = RunChip{};
    }
  }

  const bool chipNavigationDisabled =
      forScene && (progress.phase == studio::ScenePhase::RunningStart ||
                   progress.phase == studio::ScenePhase::RunningStop);
  for (uint8_t i = 0; i < runChipCount; ++i) {
    RunChip& chip = runChips[i];
    const studio::DeviceRuntimeState runtime =
        studio::devices().runtimeState(chip.instanceId);
    ChipState nextState = ChipState::Disconnected;
    if (runtime.link == studio::LinkState::Connected && runtime.protocolReady) {
      nextState = ChipState::Ready;
    } else if (connectionFailed) {
      nextState = ChipState::Failed;
    } else if ((forScene &&
                progress.phase == studio::ScenePhase::Connecting) ||
               runtime.link == studio::LinkState::Scanning ||
               runtime.link == studio::LinkState::Connecting ||
               runtime.link == studio::LinkState::Connected) {
      // Drivers report Disconnected during the central's bounded retry
      // backoff. The sequence phase preserves the operator-facing intent.
      nextState = ChipState::Connecting;
    }
    if (nextState != chip.state) {
      stopChipAnimation(chip);
      chip.state = nextState;
      lv_obj_set_style_border_opa(chip.ring, LV_OPA_COVER, 0);
      if (nextState == ChipState::Ready) {
        lv_obj_set_style_border_color(chip.ring, lv_color_hex(kColOk), 0);
      } else if (nextState == ChipState::Failed) {
        lv_obj_set_style_border_color(chip.ring, lv_color_hex(kColDanger), 0);
      } else if (nextState == ChipState::Disconnected) {
        lv_obj_set_style_border_color(chip.ring, lv_color_hex(kColMuted), 0);
      } else {
        lv_obj_set_style_border_color(chip.ring,
                                      lv_color_hex(kColConnecting), 0);
        lv_anim_t animation;
        lv_anim_init(&animation);
        lv_anim_set_var(&animation, chip.ring);
        lv_anim_set_exec_cb(&animation, setChipBorderOpacity);
        lv_anim_set_values(&animation, LV_OPA_30, LV_OPA_COVER);
        lv_anim_set_time(&animation, 600);
        lv_anim_set_playback_time(&animation, 600);
        lv_anim_set_repeat_count(&animation, LV_ANIM_REPEAT_INFINITE);
        lv_anim_set_path_cb(&animation, lv_anim_path_ease_in_out);
        lv_anim_start(&animation);
      }
    }
    if (chipNavigationDisabled) {
      lv_obj_add_state(chip.button, LV_STATE_DISABLED);
      lv_obj_set_style_img_opa(chip.icon, LV_OPA_50, 0);
    } else {
      lv_obj_clear_state(chip.button, LV_STATE_DISABLED);
      lv_obj_set_style_img_opa(chip.icon, LV_OPA_COVER, 0);
    }
  }

  const bool busy = studio::scenes().busy();
  const bool canStart = canStartSequence(*record, progress);
  if (canStart) {
    lv_obj_clear_state(startButton, LV_STATE_DISABLED);
  } else {
    lv_obj_add_state(startButton, LV_STATE_DISABLED);
  }

  const bool canStop = canStopSequence(*record, progress);
  if (canStop && progress.phase != studio::ScenePhase::RunningStop) {
    lv_obj_clear_state(stopButton, LV_STATE_DISABLED);
  } else {
    lv_obj_add_state(stopButton, LV_STATE_DISABLED);
  }

  if (prepareCancelButton != nullptr) {
    const bool linkedOrBusy = sequenceHoldsOrBusy();
    const bool connecting =
        forScene && progress.phase == studio::ScenePhase::Connecting;
    const char* action = !linkedOrBusy ? "Prepare"
                                      : (connecting ? "Cancel" : "Done");
    lv_label_set_text(lv_obj_get_child(prepareCancelButton, 0), action);
    lv_obj_set_style_bg_color(
        prepareCancelButton,
        lv_color_hex(linkedOrBusy ? kColPanel : kColAccent), 0);
    if (busy && progress.phase == studio::ScenePhase::RunningStop) {
      lv_obj_add_state(prepareCancelButton, LV_STATE_DISABLED);
    } else {
      lv_obj_clear_state(prepareCancelButton, LV_STATE_DISABLED);
    }
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
    lv_obj_set_size(row, lv_pct(100), 58);
    lv_obj_set_style_bg_color(row, lv_color_hex(kColPanel), 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_border_color(row, lv_color_hex(kColMuted), 0);
    lv_obj_set_style_border_opa(row, LV_OPA_60, 0);
    lv_obj_set_style_radius(row, 7, 0);
    lv_obj_set_style_pad_all(row, 2, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    void* userData = reinterpret_cast<void*>(static_cast<uintptr_t>(i));
    lv_obj_t* edit = makeButton(row, text, onEditStep);
    lv_obj_set_size(edit, lv_pct(100), 24);
    lv_obj_align(edit, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_border_width(edit, 0, 0);
    lv_obj_t* editLabel = lv_obj_get_child(edit, 0);
    lv_label_set_long_mode(editLabel, LV_LABEL_LONG_DOT);
    lv_obj_set_size(editLabel, 160, 18);
    lv_obj_set_style_text_align(editLabel, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_center(editLabel);
    lv_obj_remove_event_cb(edit, onEditStep);
    lv_obj_add_event_cb(edit, onEditStep, LV_EVENT_CLICKED, userData);

    lv_obj_t* up = makeButton(row, LV_SYMBOL_UP, onMoveUp);
    lv_obj_set_size(up, 28, 26);
    lv_obj_align(up, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_remove_event_cb(up, onMoveUp);
    lv_obj_add_event_cb(up, onMoveUp, LV_EVENT_CLICKED, userData);

    lv_obj_t* down = makeButton(row, LV_SYMBOL_DOWN, onMoveDown);
    lv_obj_set_size(down, 28, 26);
    lv_obj_align(down, LV_ALIGN_BOTTOM_LEFT, 30, 0);
    lv_obj_remove_event_cb(down, onMoveDown);
    lv_obj_add_event_cb(down, onMoveDown, LV_EVENT_CLICKED, userData);

    lv_obj_t* del = makeButton(row, LV_SYMBOL_TRASH, onDeleteStep, kColDanger);
    lv_obj_set_size(del, 28, 26);
    lv_obj_align(del, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_remove_event_cb(del, onDeleteStep);
    lv_obj_add_event_cb(del, onDeleteStep, LV_EVENT_CLICKED, userData);
  }
  lv_obj_t* addRow = lv_obj_create(editBody);
  lv_obj_set_size(addRow, lv_pct(100), count > 0 ? 34 : 28);
  lv_obj_set_style_bg_opa(addRow, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(addRow, 0, 0);
  lv_obj_set_style_pad_all(addRow, 0, 0);
  lv_obj_clear_flag(addRow, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t* add = makeButton(addRow, "+ Add step", onOpenAddStep, kColAccent);
  lv_obj_set_size(add, 120, 28);
  lv_obj_align(add, LV_ALIGN_BOTTOM_MID, 0, 0);
}

void buildList() {
  scrList = lv_obj_create(nullptr);
  styleScreen(scrList);

  studio_ui::RoundPageHeaderOptions header;
  header.title = "Scenes";
  header.onBack = onBackToHome;
  header.panelColor = kColPanel;
  header.textColor = kColText;
  studio_ui::createRoundPageHeader(scrList, header);

  listBody = lv_obj_create(scrList);
  lv_obj_set_size(listBody, 188, 118);
  lv_obj_align(listBody, LV_ALIGN_TOP_MID, 0,
               studio_ui::kRoundPageContentY);
  lv_obj_set_style_bg_opa(listBody, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(listBody, 0, 0);
  lv_obj_set_style_pad_row(listBody, 4, 0);
  lv_obj_set_flex_flow(listBody, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scroll_dir(listBody, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(listBody, LV_SCROLLBAR_MODE_OFF);

  lv_obj_t* add = makeButton(scrList, "+", onAddBlank, kColAccent);
  lv_obj_set_size(add, 48, 28);
  lv_obj_align(add, LV_ALIGN_BOTTOM_MID, 0, -26);
}

void buildRun() {
  scrRun = lv_obj_create(nullptr);
  styleScreen(scrRun);

  studio_ui::RoundPageHeaderOptions headerOptions;
  headerOptions.onBack = onBackToList;
  headerOptions.actionSymbol = LV_SYMBOL_SETTINGS;
  headerOptions.onAction = onOpenSettings;
  headerOptions.panelColor = kColPanel;
  headerOptions.textColor = kColText;
  const studio_ui::RoundPageHeader header =
      studio_ui::createRoundPageHeader(scrRun, headerOptions);
  runTitle = header.title;
  settingsButton = header.action;

  runChipRow = lv_obj_create(scrRun);
  lv_obj_set_size(runChipRow, 184, 59);
  lv_obj_align(runChipRow, LV_ALIGN_TOP_MID, 0, 60);
  lv_obj_set_style_bg_opa(runChipRow, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(runChipRow, 0, 0);
  lv_obj_set_style_pad_all(runChipRow, 0, 0);
  lv_obj_clear_flag(runChipRow, LV_OBJ_FLAG_SCROLLABLE);

  startButton = makeButton(scrRun, "Start", onStart, kColOk);
  lv_obj_set_size(startButton, 70, 34);
  lv_obj_align(startButton, LV_ALIGN_TOP_MID, -40, 123);

  stopButton = makeButton(scrRun, "Stop", onStop, kColDanger);
  lv_obj_set_size(stopButton, 70, 34);
  lv_obj_align(stopButton, LV_ALIGN_TOP_MID, 40, 123);

  runPhase = lv_label_create(scrRun);
  lv_obj_set_width(runPhase, 170);
  lv_label_set_long_mode(runPhase, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_font(runPhase, UI_FONT_14, 0);
  lv_obj_set_style_text_color(runPhase, lv_color_hex(kColMuted), 0);
  lv_obj_set_style_text_align(runPhase, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(runPhase, LV_ALIGN_TOP_MID, 0, 166);

  prepareCancelButton =
      makeButton(scrRun, "Prepare", onPrepareOrCancel, kColAccent);
  lv_obj_set_size(prepareCancelButton, 90, 28);
  lv_obj_align(prepareCancelButton, LV_ALIGN_BOTTOM_MID, 0, -18);
}

void buildEdit() {
  scrEdit = lv_obj_create(nullptr);
  styleScreen(scrEdit);

  studio_ui::RoundPageHeaderOptions headerOptions;
  headerOptions.onBack = onBackToRun;
  headerOptions.panelColor = kColPanel;
  headerOptions.textColor = kColText;
  editTitle =
      studio_ui::createRoundPageHeader(scrEdit, headerOptions).title;

  editBody = lv_obj_create(scrEdit);
  lv_obj_set_size(editBody, 188, 158);
  lv_obj_align(editBody, LV_ALIGN_TOP_MID, 0,
               studio_ui::kRoundPageContentY);
  lv_obj_set_style_bg_opa(editBody, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(editBody, 0, 0);
  lv_obj_set_style_pad_all(editBody, 2, 0);
  lv_obj_set_style_pad_row(editBody, 3, 0);
  lv_obj_set_flex_flow(editBody, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scroll_dir(editBody, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(editBody, LV_SCROLLBAR_MODE_OFF);

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
  closeSettings();
  ui::closeRenamePrompt();
  view = View::List;
  borrowedDeviceOpen = false;
  refreshList();
  lv_scr_load(scrList);
}

void showRunView(studio::SceneId sceneId) {
  ensureScreens();
  closeAddOverlay();
  closeSettings();
  ui::closeRenamePrompt();
  currentScene = sceneId;
  view = View::Run;
  borrowedDeviceOpen = false;
  const studio::SceneProgress& progress = studio::scenes().progress();
  const bool alreadyHeld =
      progress.sceneId == sceneId && studio::scenes().holdsLinks() &&
      (progress.phase == studio::ScenePhase::Ready ||
       progress.phase == studio::ScenePhase::IdleArmed ||
       progress.phase == studio::ScenePhase::Failed ||
       progress.phase == studio::ScenePhase::Completed ||
       progress.phase == studio::ScenePhase::Connecting ||
       progress.phase == studio::ScenePhase::RunningStart ||
       progress.phase == studio::ScenePhase::RunningStop);
  if (!alreadyHeld) {
    studio::scenes().prepare(sceneId);
  }
  refreshRun();
  lv_scr_load(scrRun);
}

void showEditView(studio::SceneId sceneId, bool startList) {
  ensureScreens();
  closeAddOverlay();
  closeSettings();
  ui::closeRenamePrompt();
  currentScene = sceneId;
  editingStart = startList;
  view = View::Edit;
  borrowedDeviceOpen = false;
  refreshEdit();
  lv_scr_load(scrEdit);
}

}  // namespace

void init() {}

void tick() {
  if (!visible || view != View::Run) return;
  const uint32_t now = millis();
  if (now - lastRefreshMs < 200) {
    return;
  }
  lastRefreshMs = now;
  refreshRun();
}

bool active() { return visible; }

bool deviceControlOpen() { return visible && borrowedDeviceOpen; }

void returnFromDeviceControl() {
  if (!deviceControlOpen()) {
    return;
  }
  borrowedDeviceOpen = false;
  refreshRun();
  lv_scr_load(scrRun);
  ui::releaseInactiveScreens();
}

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
  closeSettings();
  ui::closeRenamePrompt();
  releaseHeldScene();
  visible = false;
  borrowedDeviceOpen = false;
  view = View::List;
  currentScene = studio::kInvalidSceneId;
}

void handleShortPress() {
  if (!visible || view != View::Run || ui::renamePromptActive() ||
      settingsOverlay != nullptr || picker_shell::active()) {
    return;
  }
  refreshRun();
  if (!lv_obj_has_state(stopButton, LV_STATE_DISABLED)) {
    onStop(nullptr);
  } else if (!lv_obj_has_state(startButton, LV_STATE_DISABLED)) {
    onStart(nullptr);
  }
}

void handleLongPress() {
  if (!visible) {
    return;
  }
  if (ui::renamePromptActive()) {
    ui::closeRenamePrompt();
    return;
  }
  if (settingsOverlay != nullptr) {
    closeSettings();
    return;
  }
  if (picker_shell::handleBack()) {
    return;
  }
  if (view == View::Edit) {
    showRunView(currentScene);
    return;
  }
  if (view == View::Run) {
    onBackToList(nullptr);
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

void simShowSettings(studio::SceneId sceneId) {
  visible = true;
  // Avoid prepare side-effects for a static settings screenshot.
  ensureScreens();
  closeAddOverlay();
  currentScene = sceneId;
  view = View::Run;
  refreshRun();
  lv_scr_load(scrRun);
  buildSettingsOverlay();
}

void simOpenSettings() { onOpenSettings(nullptr); }

void simOpenDeviceControl(studio::InstanceId instanceId) {
  lv_event_t event{};
  event.user_data = reinterpret_cast<void*>(
      static_cast<uintptr_t>(instanceId));
  onOpenDeviceControl(&event);
}

void simDeleteCurrentScene() { onDeleteScene(nullptr); }

bool simShowingList() { return visible && view == View::List; }

bool simShowingEdit() { return visible && view == View::Edit; }

bool simAddStepAtListEnd() {
  const uint32_t count = editBody != nullptr ? lv_obj_get_child_cnt(editBody) : 0;
  if (count == 0) {
    return false;
  }
  lv_obj_t* last = lv_obj_get_child(editBody, count - 1);
  if (last == nullptr || lv_obj_get_child_cnt(last) == 0) {
    return false;
  }
  lv_obj_t* button = lv_obj_get_child(last, 0);
  if (button == nullptr || lv_obj_get_child_cnt(button) == 0) {
    return false;
  }
  const bool centered =
      lv_obj_get_x(button) + lv_obj_get_width(button) / 2 ==
      lv_obj_get_width(last) / 2;
  return centered &&
         std::strcmp(lv_label_get_text(lv_obj_get_child(button, 0)),
                     "+ Add step") == 0;
}

bool simDeleteStep(uint8_t index) {
  if (editBody == nullptr || index + 1 >= lv_obj_get_child_cnt(editBody)) {
    return false;
  }
  lv_obj_t* row = lv_obj_get_child(editBody, index);
  if (row == nullptr || lv_obj_get_child_cnt(row) < 4) return false;
  lv_obj_t* trash = lv_obj_get_child(row, 3);
  return trash != nullptr && lv_event_send(trash, LV_EVENT_CLICKED, nullptr) ==
                                 LV_RES_OK;
}

uint8_t simRenderedStepCount() {
  const uint32_t children =
      editBody != nullptr ? lv_obj_get_child_cnt(editBody) : 0;
  return children > 0 ? static_cast<uint8_t>(children - 1) : 0;
}

void simShowAddStepCategory(studio::SceneId sceneId) {
  visible = true;
  showEditView(sceneId, true);
  picker_shell::simShowCategory(picker_shell::Mode::SceneStep);
}

void simShowAddStepDevice(studio::SceneId sceneId, studio::DeviceType category) {
  visible = true;
  showEditView(sceneId, true);
  picker_shell::simShowDeviceList(picker_shell::Mode::SceneStep, category);
}

void simShowAddStepAction(studio::SceneId sceneId, studio::InstanceId instanceId) {
  visible = true;
  showEditView(sceneId, true);
  picker_shell::simShowActions(picker_shell::Mode::SceneStep, instanceId);
}

void simEditStep(studio::SceneId sceneId, bool startList, uint8_t index) {
  visible = true;
  showEditView(sceneId, startList);
  lv_event_t event{};
  event.user_data = reinterpret_cast<void*>(static_cast<uintptr_t>(index));
  onEditStep(&event);
}
#endif

}  // namespace scene_ui
