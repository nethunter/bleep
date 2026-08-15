#pragma once

#include "core/device_types.h"

namespace zhiyun_light_ui {

void show(studio::InstanceId instanceId);
void hide();
void release();
bool active();
void tick();
void handleShortPress();
void handleLongPress();

#ifdef UI_SIMULATOR
void simShowRgb();
#endif

}  // namespace zhiyun_light_ui
