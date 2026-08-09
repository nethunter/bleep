#pragma once

#include "core/device_types.h"

namespace light_control_ui {

using BackFn = void (*)();

void show(studio::InstanceId instanceId, BackFn onBack,
          bool applyDisplayedLook = false);
void hide();
void release();
bool active();
void tick();
void handleShortPress();
void handleLongPress();

#ifdef UI_SIMULATOR
void simShowCct();
void simShowRgb();
void simSetCctLook(int kelvin, int tintPermille, int brightness);
void simSetRgbLook(uint32_t rgb, int brightness);
int simRgbSaturation();
#endif

}  // namespace light_control_ui
