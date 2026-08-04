#pragma once

#include "core/scene_types.h"

namespace scene_ui {

void init();
void tick();
bool active();
void show();
void hide();
void handleShortPress();

#ifdef UI_SIMULATOR
void simShowList();
void simShowRun(studio::SceneId sceneId);
void simShowEditStart(studio::SceneId sceneId);
void simShowEditStop(studio::SceneId sceneId);
void simShowSettings(studio::SceneId sceneId);
void simShowAddStepCategory(studio::SceneId sceneId);
void simShowAddStepDevice(studio::SceneId sceneId, studio::DeviceType category);
void simShowAddStepAction(studio::SceneId sceneId, studio::InstanceId instanceId);
#endif

}  // namespace scene_ui
