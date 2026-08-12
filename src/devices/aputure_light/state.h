#pragma once

#include <cstdint>

namespace aputure_light {

struct AputureLightState {
  enum class Phase : uint8_t {
    Unprovisioned,
    Scanning,
    ConnectingProvisioning,
    Provisioning,
    PendingConfig,
    ConnectingProxy,
    Ready,
    Failed,
  };
  enum class Mode : uint8_t { Cct, Rgb };

  Phase phase = Phase::Unprovisioned;
  Mode mode = Mode::Cct;
  bool proxyConnected = false;
  bool commandPending = false;
  bool lastCommandFailed = false;
  bool optimistic = false;
  bool powerOptimistic = false;
  bool nodeReachable = false;
  bool powerConfirmed = false;
  bool on = false;
  uint32_t lastSeenMs = 0;
  uint16_t kelvin = 5600;
  int16_t tintPermille = 0;
  uint8_t cctBrightness = 50;
  uint32_t rgb = 0xff0000;
  uint8_t rgbBrightness = 50;
  char error[48] = "";
};

bool validCctCommand(int kelvin, int brightness, int tintPermille);
bool validRgbCommand(int rgb, int brightness);

}  // namespace aputure_light
