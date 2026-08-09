#pragma once
#include <cstdint>
namespace phone_camera {
struct PhoneCameraState {
  enum class Link : uint8_t { Disconnected, Advertising, Connecting, Connected };
  Link link = Link::Disconnected;
  bool triggerPending = false;
  bool lastTriggerSucceeded = false;
  bool lastTriggerFailed = false;
  uint32_t triggerCount = 0;
  char deviceName[40] = "";
};
}  // namespace phone_camera
