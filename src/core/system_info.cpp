#include "core/system_info.h"

#ifdef UI_SIMULATOR

#include "core/device_manager.h"
#include "portal_service.h"

namespace studio {

SystemInfo systemInfo() {
  return {102400, 82176, 65536, devices().bleSlotCount(),
          portal::active() ? "Portal" : "Off"};
}

}  // namespace studio

#else

#include <Arduino.h>
#include <WiFi.h>
#include <esp_heap_caps.h>

#include "core/device_manager.h"
#include "portal_service.h"

namespace studio {

SystemInfo systemInfo() {
  const wifi_mode_t mode = WiFi.getMode();
  const char* wifi = mode == WIFI_MODE_NULL
                         ? "Off"
                         : portal::active() ? "Portal" : "Device runtime";
  return {static_cast<uint32_t>(ESP.getFreeHeap()),
          static_cast<uint32_t>(ESP.getMinFreeHeap()),
          static_cast<uint32_t>(
              heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)),
          devices().bleSlotCount(), wifi};
}

}  // namespace studio

#endif
