#pragma once

// Minimal Arduino stubs for host UI simulation.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

uint32_t millis();
void delay(uint32_t ms);
void simAdvanceMillis(uint32_t ms);

using byte = uint8_t;
