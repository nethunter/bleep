#include "scene_ui.h"

#include <Arduino.h>
#include <lvgl.h>

#include <cstdio>
#include <cstring>

#include "core/device_manager.h"
#include "core/scene_service.h"
#include "fonts/ui_fonts.h"
#include "ui.h"
#include "ui/picker_shell.h"

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

View view = View::List;
bool visible = false;
studio::SceneId currentScene = studio::kInvalidSceneId;
bool editingStart = true;
uint32_t lastRefreshMs = 0;

lv_obj_t* scrList = nullptr;
lv_obj_t* scrRun = nullptr;
lv_obj_t* scrEdit = nullptr;
lv_obj_t* listBody = nullptr;
lv_obj_t* runTitle = nullptr;
lv_obj_t* runPhase = nullptr;
lv_obj_t* runDetail = nullptr;
lv_obj_t* startButton = nullptr;
lv_obj_t* stopButton = nullptr;
lv_obj_t* prepareCancelButton = nullptr;
lv_obj_t* settingsButton = nullptr;
lv_obj_t* settingsOverlay = nullptr;
lv_obj_t* editBody = nullptr;
lv_obj_t* editTitle = nullptr;

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
void closeSettings();
void releaseHeldScene();

void onBackToHome(lv_event_t*) {
  hide();
  ui::showHome();
}

void onBackToList(lv_event_t*) {
  releaseHeldScene();
  showListView();
}

void onBackToRun(lv_event_t*) { showRunView(currentScene); }

void onOpenScene(lv_event_t* event) { showRunView(eventScene(event)); }

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
  return !studio::scenes().busy() && record.startCount > 0 &&
         (ready || progress.phase == studio::ScenePhase::Idle ||
          progress.phase == studio::ScenePhase::Completed || !forScene);
}

bool canStopSequence(const studio::SceneProgress& progress) {
  if (progress.sceneId != currentScene) {
    return false;
  }
  const bool armed = progress.phase == studio::ScenePhase::IdleArmed ||
                     progress.phase == studio::ScenePhase::Failed;
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
  if (!canMutateScene()) {
    return;
  }
  closeSettings();
  if (studio::scenes().holdsLinks()) {
    studio::scenes().cancel();
  }
  studio::scenes().remove(currentScene);
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
}

void onCloseSettings(lv_event_t*) { closeSettings(); }

void buildSettingsOverlay() {
  closeSettings();
  settingsOverlay = lv_obj_create(lv_layer_top());
  lv_obj_set_size(settingsOverlay, 236, 236);
  lv_obj_center(settingsOverlay);
  lv_obj_set_style_radius(settingsOverlay, 118, 0);
  lv_obj_set_style_bg_color(settingsOverlay, lv_color_hex(kColBg), 0);
  lv_obj_set_style_border_width(settingsOverlay, 0, 0);
  lv_obj_set_style_pad_all(settingsOverlay, 0, 0);
  lv_obj_clear_flag(settingsOverlay, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* close = makeButton(settingsOverlay, LV_SYMBOL_CLOSE, onCloseSettings);
  lv_obj_set_size(close, 30, 30);
  lv_obj_align(close, LV_ALIGN_TOP_LEFT, 34, 24);

  lv_obj_t* title = lv_label_create(settingsOverlay);
  lv_label_set_text(title, "Settings");
  lv_obj_set_style_text_font(title, UI_FONT_16, 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 10, 30);

  lv_obj_t* body = lv_obj_create(settingsOverlay);
  lv_obj_set_size(body, 168, 140);
  lv_obj_align(body, LV_ALIGN_TOP_MID, 0, 62);
  lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(body, 0, 0);
  lv_obj_set_style_pad_row(body, 4, 0);
  lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_scrollbar_mode(body, LV_SCROLLBAR_MODE_OFF);

  lv_obj_t* rename = makeButton(body, "Rename", onRenameScene);
  lv_obj_set_size(rename, lv_pct(100), 32);
  lv_obj_t* editStart = makeButton(body, "Edit Start", onEditStart);
  lv_obj_set_size(editStart, lv_pct(100), 32);
  lv_obj_t* editStop = makeButton(body, "Edit Stop", onEditStop);
  lv_obj_set_size(editStop, lv_pct(100), 32);
  lv_obj_t* del = makeButton(body, "Delete", onDeleteScene, kColDanger);
  lv_obj_set_size(del, lv_pct(100), 32);
}

void onOpenSettings(lv_event_t*) {
  if (ui::renamePromptActive()) {
    return;
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

void appendSceneStep(const studio::SceneStep& step) {
  studio::SceneRecord record;
  if (!loadEditable(record)) {
    return;
  }
  studio::SceneStep* steps = editingStart ? record.startSteps : record.stopSteps;
  uint8_t& count = editingStart ? record.startCount : record.stopCount;
  if (count >= CONFIG_MAX_SCENE_STEPS) {
    return;
  }
  steps[count++] = step;
  studio::scenes().replace(record);
  refreshEdit();
}

void onSceneWaitChosen() { appendSceneStep(studio::makeWaitStep(500)); }

void onSceneActionChosen(studio::InstanceId instanceId,
                         studio::CommandType command) {
  appendSceneStep(studio::makeActionStep(instanceId, command));
}

void onOpenAddStep(lv_event_t*) {
  picker_shell::Callbacks callbacks;
  callbacks.onSceneAction = onSceneActionChosen;
  callbacks.onWaitChosen = onSceneWaitChosen;
  picker_shell::show(picker_shell::Mode::SceneStep, callbacks);
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
  lv_label_set_text(runPhase, forScene ? phaseText(progress.phase) : "Idle");
  lv_label_set_text(runDetail, forScene ? progress.detail : "Open to prepare");

  const bool busy = studio::scenes().busy();
  const bool canStart = canStartSequence(*record, progress);
  if (canStart) {
    lv_obj_clear_state(startButton, LV_STATE_DISABLED);
  } else {
    lv_obj_add_state(startButton, LV_STATE_DISABLED);
  }

  const bool canStop = canStopSequence(progress);
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
                                      : (connecting ? "Cancel" : "Unlink");
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
  lv_obj_set_size(listBody, 188, 118);
  lv_obj_align(listBody, LV_ALIGN_TOP_MID, 0, 68);
  lv_obj_set_style_bg_opa(listBody, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(listBody, 0, 0);
  lv_obj_set_style_pad_row(listBody, 4, 0);
  lv_obj_set_flex_flow(listBody, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scroll_dir(listBody, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(listBody, LV_SCROLLBAR_MODE_OFF);

  lv_obj_t* add = makeButton(scrList, "+", onAddBlank, kColAccent);
  lv_obj_set_size(add, 48, 28);
  lv_obj_align(add, LV_ALIGN_BOTTOM_MID, 0, -16);
}

void buildRun() {
  scrRun = lv_obj_create(nullptr);
  styleScreen(scrRun);

  lv_obj_t* back = makeButton(scrRun, LV_SYMBOL_LEFT, onBackToList);
  lv_obj_set_size(back, 34, 30);
  lv_obj_align(back, LV_ALIGN_TOP_LEFT, 40, 22);

  settingsButton = makeButton(scrRun, LV_SYMBOL_SETTINGS, onOpenSettings);
  lv_obj_set_size(settingsButton, 34, 30);
  lv_obj_align(settingsButton, LV_ALIGN_TOP_RIGHT, -40, 22);

  runTitle = lv_label_create(scrRun);
  lv_obj_set_width(runTitle, 92);
  lv_label_set_long_mode(runTitle, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_font(runTitle, UI_FONT_16, 0);
  lv_obj_set_style_text_align(runTitle, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(runTitle, LV_ALIGN_TOP_MID, 0, 29);

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

  prepareCancelButton =
      makeButton(scrRun, "Prepare", onPrepareOrCancel, kColAccent);
  lv_obj_set_size(prepareCancelButton, 90, 28);
  lv_obj_align(prepareCancelButton, LV_ALIGN_BOTTOM_MID, 0, -18);
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
  closeSettings();
  ui::closeRenamePrompt();
  releaseHeldScene();
  visible = false;
  view = View::List;
  currentScene = studio::kInvalidSceneId;
}

void handleShortPress() {
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
  if (picker_shell::handleShortPress()) {
    return;
  }
  if (view == View::Edit) {
    showRunView(currentScene);
    return;
  }
  if (view == View::Run) {
    const studio::SceneRecord* record = studio::scenes().find(currentScene);
    if (record == nullptr) {
      return;
    }
    const studio::SceneProgress& progress = studio::scenes().progress();
    if (canStopSequence(progress)) {
      onStop(nullptr);
    } else if (canStartSequence(*record, progress)) {
      onStart(nullptr);
    }
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
#endif

}  // namespace scene_ui
