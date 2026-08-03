#pragma once

#include "core/device_types.h"

// Application-level LVGL navigation. All functions are main-loop only.
namespace ui {

void init();
void tick();
void handleShortPress();
void showHome();
void showDevices();

#ifdef UI_SIMULATOR
void simShowManage(studio::InstanceId instanceId);
void simShowRename(studio::InstanceId instanceId);
#endif

}  // namespace ui
