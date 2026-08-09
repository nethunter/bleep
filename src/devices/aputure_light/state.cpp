#include "devices/aputure_light/state.h"

#include "devices/aputure_light/protocol.h"

namespace aputure_light {

bool validCctCommand(int kelvin, int brightness, int tintPermille) {
  return kelvin >= kCctMinKelvin && kelvin <= kCctMaxKelvin &&
         brightness >= 0 && brightness <= 100 && tintPermille >= -1000 &&
         tintPermille <= 1000;
}

bool validRgbCommand(int rgb, int brightness) {
  return rgb >= 0 && rgb <= 0xffffff && brightness >= 0 && brightness <= 100;
}

}  // namespace aputure_light
