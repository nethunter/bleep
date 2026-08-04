#pragma once

#include <cstdint>

namespace portal {

enum class Status : uint8_t { Inactive, Starting, Ready, Testing, Saved, Error };

bool begin();
void loop();
void stop();
bool active();
Status status();
const char* statusText();
const char* ssid();
const char* password();
const char* url();

#ifdef UI_SIMULATOR
void simSetLan(bool connected);
void simSetWifiFeedback(Status status, const char* message);
#endif

}  // namespace portal
