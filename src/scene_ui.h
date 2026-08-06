#pragma once

#include "core/scene_types.h"

namespace scene_ui {

void init();
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
void simShowRun(studio::SceneId sceneId);
void simShowEditStart(studio::SceneId sceneId);
void simShowEditStop(studio::SceneId sceneId);
void simShowSettings(studio::SceneId sceneId);
void simOpenDeviceControl(studio::InstanceId instanceId);
void simDeleteCurrentScene();
bool simShowingList();
void simShowAddStepCategory(studio::SceneId sceneId);
void simShowAddStepDevice(studio::SceneId sceneId, studio::DeviceType category);
void simShowAddStepAction(studio::SceneId sceneId, studio::InstanceId instanceId);
void simEditStep(studio::SceneId sceneId, bool startList, uint8_t index);
#endif

}  // namespace scene_ui
