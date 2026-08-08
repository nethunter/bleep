#include "core/factory_reset.h"

#ifdef UI_SIMULATOR

namespace studio::factory_reset {
namespace {
uint32_t resetCount = 0;
}

bool eraseAndRestart() {
  ++resetCount;
  return true;
}

uint32_t simulatedResetCount() { return resetCount; }
void clearSimulatedResetCount() { resetCount = 0; }

}  // namespace studio::factory_reset

#else

#include <Arduino.h>
#include <nvs_flash.h>

namespace studio::factory_reset {

bool eraseAndRestart() {
  if (nvs_flash_erase() != ESP_OK) return false;
  delay(50);
  ESP.restart();
  return true;
}

}  // namespace studio::factory_reset

#endif
