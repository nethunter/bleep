#pragma once

#include "core/device_types.h"

namespace home_assistant_ui {

void show(studio::InstanceId instanceId);
void hide();
void release();
bool active();
void tick();
void handleShortPress();
void handleLongPress();

}  // namespace home_assistant_ui
