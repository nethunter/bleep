#include "Arduino.h"

#include <chrono>

namespace {

uint32_t gMillis = 0;

}  // namespace

uint32_t millis() { return gMillis; }

void delay(uint32_t ms) { gMillis += ms; }

void simAdvanceMillis(uint32_t ms) { gMillis += ms; }
