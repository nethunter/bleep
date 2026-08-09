#pragma once
#include <cstdint>
namespace insta360 {
struct State {
  enum class Link : uint8_t { Disconnected, Scanning, Connected };
  Link link = Link::Disconnected;
  bool triggerPending = false;
  bool lastTriggerFailed = false;
  bool goUltraExperimental = false;
  uint32_t triggerCount = 0;
  char deviceName[40] = "";
};
}  // namespace insta360
