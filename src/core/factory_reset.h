#pragma once

#include <cstdint>

namespace studio::factory_reset {

// Erases persistent configuration and restarts on target hardware. It returns
// only when erasure fails or under the UI simulator.
bool eraseAndRestart();

#ifdef UI_SIMULATOR
uint32_t simulatedResetCount();
void clearSimulatedResetCount();
#endif

}  // namespace studio::factory_reset
