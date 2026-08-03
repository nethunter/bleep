#pragma once

// Application-level LVGL navigation. All functions are main-loop only.
namespace ui {

void init();
void tick();
void handleShortPress();
void showHome();
void showDevices();

}  // namespace ui
