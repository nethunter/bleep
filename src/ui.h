#pragma once

#include "core/device_types.h"

// Application-level LVGL navigation. All functions are main-loop only.
namespace ui {

using RenameDoneFn = void (*)(const char* name);

void init();
void tick();
void handleShortPress();
void handleLongPress();
void showHome();
void showDevices();
// Load Home/Devices so a device screen can be deleted safely, without changing
// device activation state.
void parkForScreenRebuild();
// Delete inactive device screens/overlays after a resident screen is loaded.
void releaseInactiveScreens();

// Shared round rename keypad. onDone receives the edited name; cancel clears
// without calling onDone.
void promptRename(const char* initial, RenameDoneFn onDone);
void closeRenamePrompt();
bool renamePromptActive();

#ifdef UI_SIMULATOR
void simShowAddDevice();
void simShowManage(studio::InstanceId instanceId);
void simShowRename(studio::InstanceId instanceId);
#endif

}  // namespace ui
