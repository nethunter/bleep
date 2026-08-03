#pragma once

#include "core/device_types.h"

namespace tascam_x8_ui {

void init();
void show(studio::InstanceId instanceId);
void hide();
bool active();
void tick();
void handleShortPress();

}  // namespace tascam_x8_ui
