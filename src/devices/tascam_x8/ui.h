#pragma once

#include "core/device_types.h"

namespace tascam_x8_ui {

void init();
void show(studio::InstanceId instanceId, bool preserveActivation = false);
void hide();
void release();
bool active();
void tick();
void handleShortPress();
void handleLongPress();

}  // namespace tascam_x8_ui
