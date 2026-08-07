#pragma once

#include "core/device_types.h"

namespace zhiyun_x100_ui {

void init();
void show(studio::InstanceId instanceId);
void hide();
void release();
bool active();
void tick();
void handleShortPress();
void handleLongPress();

}  // namespace zhiyun_x100_ui
