#pragma once

#include <cstddef>
#include <cstdint>

#include "core/device_types.h"

struct _lv_obj_t;
using lv_obj_t = _lv_obj_t;

namespace aputure_light_ui {
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
int simRgbSaturation();
size_t simCandidateRowCount();
lv_obj_t* simCandidateRow(size_t index);
void simScrollCandidates(int16_t delta);
int32_t simCandidateScrollY();
void simClickCandidate(size_t index);
#endif
}  // namespace aputure_light_ui
