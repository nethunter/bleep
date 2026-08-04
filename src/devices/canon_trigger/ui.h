#pragma once

#include "core/device_types.h"

namespace canon_trigger_ui {

void init();
void show(studio::InstanceId instanceId, bool preserveActivation = false);
void hide();
void release();
bool active();
void tick();
// Dispatches the hardware trigger to the current screen's primary action.
void handleShortPress();
void handleLongPress();

}  // namespace canon_trigger_ui
