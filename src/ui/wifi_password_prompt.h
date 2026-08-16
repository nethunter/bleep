#pragma once

namespace studio_ui::wifi_password_prompt {

using Done = void (*)(const char* password);
using Cancel = void (*)();

void show(const char* ssid, Done done, Cancel cancel = nullptr);
void close();
bool active();

#ifdef UI_SIMULATOR
void simSetPassword(const char* password);
void simSave();
#endif

}  // namespace studio_ui::wifi_password_prompt
