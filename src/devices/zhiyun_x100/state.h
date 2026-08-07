#pragma once

#include <cstdint>

#include "devices/zhiyun_x100/protocol.h"

namespace zhiyun_x100 {

struct X100State {
  enum class Link : uint8_t { Disconnected, Scanning, Connecting, Connected };
  enum class Phase : uint8_t {
    Idle,
    Provisioning,
    Initializing,
    ReadingState,
    Ready,
    Failed
  };
  enum class Mode : uint8_t { Cct, Rgb };

  Link link = Link::Disconnected;
  Phase phase = Phase::Idle;
  MolusModel model = MolusModel::Unknown;
  bool hasSavedDevice = false;
  bool confirmed = false;
  bool on = false;
  float brightness = 0.0f;
  bool brightnessLimited = false;
  uint8_t maxBrightness = 100;
  uint16_t kelvin = 5600;
  Mode mode = Mode::Cct;
  uint32_t rgb = 0xffffff;
  uint16_t hue = 0;
  uint8_t saturation = 0;
  float requestedBrightness = 0.0f;
  float readbackBrightness = 0.0f;
  uint16_t requestedKelvin = 0;
  uint16_t readbackKelvin = 0;
  uint16_t requestedHue = 0;
  uint16_t readbackHue = 0;
  uint8_t requestedSaturation = 0;
  uint8_t readbackSaturation = 0;
  uint8_t verificationField = 0;
  bool commandPending = false;
  bool lastCommandFailed = false;
  char deviceName[40] = "";
  char error[48] = "";
};

using MolusState = X100State;

bool validCctCommand(int kelvin, int brightness, int tintPermille);
bool validRgbCommand(int rgb, int brightness);
void rgbToHsv(uint32_t rgb, uint16_t& hue, uint8_t& saturation);

}  // namespace zhiyun_x100
