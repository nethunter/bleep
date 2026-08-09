#pragma once
#include "core/device_types.h"
namespace action_camera_research_ui {
void show(studio::InstanceId instanceId);
void hide();
void release();
bool active();
void handleLongPress();
}  // namespace action_camera_research_ui
