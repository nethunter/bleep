#pragma once

#include "core/scene_types.h"

namespace scene_ui {

void tick();
bool active();
bool deviceControlOpen();
void returnFromDeviceControl();
void show();
void hide();
void handleShortPress();
void handleLongPress();

#ifdef UI_SIMULATOR
void simShowList();
studio::SceneId simBeginAddFlow();
bool simAdvanceAddFlow();
bool simFinishAddFlow();
bool simAddFlowActive();
void simShowRun(studio::SceneId sceneId);
void simShowEditStart(studio::SceneId sceneId);
void simShowEditStop(studio::SceneId sceneId);
void simShowSettings(studio::SceneId sceneId);
void simOpenSettings();
void simOpenDeviceControl(studio::InstanceId instanceId);
bool simScrollSettingsToDelete();
bool simDeleteCurrentScene();
studio::SceneId simDuplicateCurrentScene();
bool simShowingList();
bool simShowingEdit();
bool simAddStepAtListEnd();
bool simDeleteStep(uint8_t index);
uint8_t simRenderedStepCount();
bool simClickStopModeAction();
const char* simEditTitleText();
void simShowAddStepCategory(studio::SceneId sceneId);
void simShowAddStepDevice(studio::SceneId sceneId, studio::DeviceType category);
void simShowAddStepAction(studio::SceneId sceneId, studio::InstanceId instanceId);
void simEditStep(studio::SceneId sceneId, bool startList, uint8_t index);
#endif

}  // namespace scene_ui
