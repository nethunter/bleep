#pragma once

#include "core/device_types.h"

// LVGL UI for the Shark Nano II remote on the 240x240 round display.
// All functions must be called from the main (LVGL) task only.

namespace shark_ui {

void init();
void show(studio::InstanceId instanceId);
void hide();
bool active();
void tick();
void handleShortPress();

}  // namespace shark_ui
