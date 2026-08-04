#pragma once

#include "core/device_types.h"

namespace canon_ble_ui {

void init();
void show(studio::InstanceId instanceId);
void hide();
void release();
bool active();
void tick();
// Dispatches the hardware trigger to the current screen's primary action.
void handleShortPress();
void handleLongPress();

}  // namespace canon_ble_ui
