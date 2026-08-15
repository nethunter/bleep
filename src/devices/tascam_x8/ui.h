#pragma once

#include "core/device_types.h"

namespace tascam_x8_ui {

void show(studio::InstanceId instanceId);
void hide();
void release();
bool active();
void tick();
void handleShortPress();
void handleLongPress();

}  // namespace tascam_x8_ui
