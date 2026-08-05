#pragma once

#include "core/device_types.h"

namespace amaran_light_ui {
void show(studio::InstanceId instanceId);
void hide();
void release();
bool active();
void tick();
void handleShortPress();
void handleLongPress();
#ifdef UI_SIMULATOR
void simSetCctLook(int kelvin, int tintPermille, int brightness);
void simSetRgbLook(uint32_t rgb, int brightness);
void simShowCct();
void simShowRgb();
#endif
}  // namespace amaran_light_ui
