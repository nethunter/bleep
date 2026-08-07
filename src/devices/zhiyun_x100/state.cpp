#include "devices/zhiyun_x100/state.h"

#include "devices/zhiyun_x100/protocol.h"

#include <algorithm>

namespace zhiyun_x100 {

bool validCctCommand(int kelvin, int brightness, int tintPermille) {
  return kelvin >= kMinKelvin && kelvin <= kMaxKelvin && brightness >= 0 &&
         brightness <= 100 && tintPermille == 0;
}

bool validRgbCommand(int rgb, int brightness) {
  return rgb >= 0 && rgb <= 0xffffff && brightness >= 0 && brightness <= 100;
}

void rgbToHsv(uint32_t rgb, uint16_t& hue, uint8_t& saturation) {
  const float red = static_cast<float>((rgb >> 16) & 0xff) / 255.0f;
  const float green = static_cast<float>((rgb >> 8) & 0xff) / 255.0f;
  const float blue = static_cast<float>(rgb & 0xff) / 255.0f;
  const float maximum = std::max(red, std::max(green, blue));
  const float minimum = std::min(red, std::min(green, blue));
  const float delta = maximum - minimum;
  float degrees = 0.0f;
  if (delta > 0.0f) {
    if (maximum == red) {
      degrees = 60.0f * ((green - blue) / delta);
      if (degrees < 0.0f) degrees += 360.0f;
    } else if (maximum == green) {
      degrees = 60.0f * (((blue - red) / delta) + 2.0f);
    } else {
      degrees = 60.0f * (((red - green) / delta) + 4.0f);
    }
  }
  hue = static_cast<uint16_t>(degrees + 0.5f);
  if (hue >= 360) hue = 0;
  saturation = maximum <= 0.0f
                   ? 0
                   : static_cast<uint8_t>((delta / maximum) * 100.0f + 0.5f);
}

}  // namespace zhiyun_x100
