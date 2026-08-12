#pragma once

#include <cstdint>

namespace portal {

enum class Status : uint8_t { Inactive, Starting, Ready, Testing, Saved, Error };

struct SavedWifiSummary {
  bool configured = false;
  char ssid[33] = "";
};

bool begin();
void loop();
void stop();
bool active();
Status status();
const char* statusText();
const char* ssid();
const char* password();
const char* url();
const char* qrPayload();
const char* unitId();
SavedWifiSummary savedWifiSummary();

#ifdef UI_SIMULATOR
void simSetLan(bool connected);
void simSetWifiFeedback(Status status, const char* message);
void simSetSavedWifi(const char* ssid);
#endif

}  // namespace portal
