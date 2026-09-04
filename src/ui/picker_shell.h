#pragma once

#include "core/scene_types.h"

// Shared Category → Device/Driver → Action drill-down for Devices "Add device"
// and Scenes "+ Step". Category level uses a 2x2 icon grid.
namespace picker_shell {

enum class Mode : uint8_t {
  AddDriver,  // Category → compiled driver model
  SceneStep,  // Category → enabled device → Record Start/Stop (+ Wait)
};

struct Callbacks {
  void (*onDriverChosen)(studio::DriverId driverId) = nullptr;
  void (*onSceneAction)(studio::InstanceId instanceId,
                        studio::CommandType command, int32_t value0,
                        int32_t value1, int32_t value2) = nullptr;
  void (*onWaitChosen)(uint32_t waitMs) = nullptr;
  void (*onClosed)() = nullptr;
};

void show(Mode mode, const Callbacks& callbacks);
void showSceneStep(const studio::SceneStep& step, const Callbacks& callbacks);
void hide();
bool active();
void tick();
// Returns true when the short-press was consumed by the picker.
bool handleBack();

#ifdef UI_SIMULATOR
void simShowCategory(Mode mode);
void simShowDeviceList(Mode mode, studio::DeviceType category);
void simScrollDeviceList(int32_t y);
void simShowActions(Mode mode, studio::InstanceId instanceId);
void simShowLightColor(Mode mode, studio::InstanceId instanceId, bool rgb);
void simShowWait();
uint32_t simWaitValue();
void simSaveWait(uint32_t waitMs);
void simSaveLightCct(int32_t kelvin, int32_t brightness, int32_t tint);
void simSetLightRgb(uint16_t hue, int32_t saturation, int32_t brightness);
void simSaveCurrentLight();
bool simLightEditorRgb();
void simChooseDriver(studio::DriverId driverId);
#endif

}  // namespace picker_shell
