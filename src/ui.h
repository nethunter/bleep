#pragma once

// LVGL UI for the Shark Nano II remote on the 240x240 round display.
// All functions must be called from the main (LVGL) task only.

namespace ui {

// Build all screens and show the appropriate one. Call once after LVGL and the
// SharkClient are initialized.
void init();

// Refresh widgets from the current SharkClient state. Safe to call every loop;
// it self-throttles.
void tick();

// Physical-button short press: toggle between the Keypoints and Run screens
// (only meaningful while connected).
void toggleMainScreen();

}  // namespace ui
