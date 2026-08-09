#pragma once

#include <cstdint>

#include "core/ble/ble_types.h"

#if defined(ARDUINO)
#include <Arduino.h>
#endif

namespace studio::ble {

inline void logTiming(const char* driver, LinkHandle link, const char* stage,
                      uint32_t elapsedMs, uint32_t totalMs,
                      const char* result) {
#if defined(ARDUINO)
#if ARDUINO_USB_CDC_ON_BOOT
  Print& output = Serial0;
#else
  Print& output = Serial;
#endif
  output.printf(
      "ble_timing driver=%s link=%u stage=%s elapsed_ms=%lu total_ms=%lu "
      "result=%s\n",
      driver != nullptr ? driver : "ble", static_cast<unsigned>(link),
      stage != nullptr ? stage : "unknown",
      static_cast<unsigned long>(elapsedMs),
      static_cast<unsigned long>(totalMs), result != nullptr ? result : "ok");
#else
  (void)driver;
  (void)link;
  (void)stage;
  (void)elapsedMs;
  (void)totalMs;
  (void)result;
#endif
}

inline void logEventReason(const char* driver, LinkHandle link,
                           const char* event, int reason) {
#if defined(ARDUINO)
#if ARDUINO_USB_CDC_ON_BOOT
  Print& output = Serial0;
#else
  Print& output = Serial;
#endif
  output.printf("ble_event driver=%s link=%u event=%s reason=%d\n",
                driver != nullptr ? driver : "ble",
                static_cast<unsigned>(link),
                event != nullptr ? event : "unknown", reason);
#else
  (void)driver;
  (void)link;
  (void)event;
  (void)reason;
#endif
}

}  // namespace studio::ble
