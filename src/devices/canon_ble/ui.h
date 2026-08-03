#pragma once

#include "core/device_types.h"

namespace canon_ble_ui {

void init();
void show(studio::InstanceId instanceId);
void hide();
bool active();
void tick();
void handleShortPress();

}  // namespace canon_ble_ui
