#pragma once

#include "core/device_types.h"

// Application-level LVGL navigation. All functions are main-loop only.
namespace ui {

using RenameDoneFn = void (*)(const char* name);

void init();
void tick();
void handleShortPress();
// Performs one Back step. Returns false when already at Home.
bool handleLongPress();
// Safely unwinds all remaining Back paths and returns Home.
bool handleLongPressToHome();
void showHome();
void showSettings();
void showDevices();
void showPortal();
void showDevice(studio::InstanceId instanceId);
// Return to the sequence that owns the borrowed session, or Devices for a
// normal device screen.
void showDeviceParent();
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
bool simAddDeviceAtListEnd();
void simShowAddDevice();
void simShowManage(studio::InstanceId instanceId);
void simRequestManagedDisconnect();
void simShowRename(studio::InstanceId instanceId);
void simShowWifiSettings();
void simShowAbout();
void simShowSystemInfo();
void simShowFactoryReset();
void simScrollSettingsToEnd();
void simScrollAboutToEnd();
void simSetHapticEnabled(bool enabled);
void simSetFactoryResetHolding(bool holding);
bool simShowingHome();
#endif

}  // namespace ui
