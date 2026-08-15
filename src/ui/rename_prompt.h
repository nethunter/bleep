#pragma once

namespace studio_ui::rename_prompt {

using Done = void (*)(const char* name);
using Cancel = void (*)();

void show(const char* initial, Done done, Cancel cancel);
void close();
void cancel();
bool active();

#ifdef UI_SIMULATOR
void simSetText(const char* name);
void simSave();
#endif

}  // namespace studio_ui::rename_prompt
