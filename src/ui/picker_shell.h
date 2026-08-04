#pragma once

#include "core/device_types.h"

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
                        studio::CommandType command) = nullptr;
  void (*onWaitChosen)() = nullptr;
  void (*onClosed)() = nullptr;
};

void show(Mode mode, const Callbacks& callbacks);
void hide();
bool active();
// Returns true when the short-press was consumed by the picker.
bool handleShortPress();

#ifdef UI_SIMULATOR
void simShowCategory(Mode mode);
void simShowDeviceList(Mode mode, studio::DeviceType category);
void simShowActions(Mode mode, studio::InstanceId instanceId);
#endif

}  // namespace picker_shell
