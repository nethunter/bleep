#pragma once

#include "core/device_types.h"

// LVGL UI for the Shark Nano II remote on the 240x240 round display.
// All functions must be called from the main (LVGL) task only.

namespace shark_ui {

void init();
void show(studio::InstanceId instanceId, bool preserveActivation = false);
void hide();
void release();
bool active();
void tick();
// Dispatches the hardware trigger to the current screen's primary action.
void handleShortPress();
// Navigates back or closes the current overlay.
void handleLongPress();

#ifdef UI_SIMULATOR
void simShowKeypoints();
void simShowKeypointSettings(int slot);
void simShowPositionChoice(int slot);
void simShowManualPositioning();
void simShowJoystickPositioning();
#endif

}  // namespace shark_ui
