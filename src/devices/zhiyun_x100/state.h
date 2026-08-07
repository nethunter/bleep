#pragma once

#include <cstdint>

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

  Link link = Link::Disconnected;
  Phase phase = Phase::Idle;
  bool hasSavedDevice = false;
  bool confirmed = false;
  bool on = false;
  float brightness = 0.0f;
  uint16_t kelvin = 5600;
  bool commandPending = false;
  bool lastCommandFailed = false;
  char deviceName[40] = "";
  char error[48] = "";
};

bool validCctCommand(int kelvin, int brightness, int tintPermille);

}  // namespace zhiyun_x100
