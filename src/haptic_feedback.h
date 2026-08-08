#pragma once

#include <cstdint>

namespace haptic_feedback {

enum class Pattern : uint8_t { Press, Connected, Back, Error };

using OutputFn = void (*)(bool enabled);

// Main-loop only. The output callback owns the board-specific motor pin.
void begin(OutputFn output);
void setEnabled(bool enabled);
bool enabled();
void request(Pattern pattern);
void loop(uint32_t now);

}  // namespace haptic_feedback
