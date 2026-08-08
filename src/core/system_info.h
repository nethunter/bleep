#pragma once

#include <cstddef>
#include <cstdint>

namespace studio {

struct SystemInfo {
  constexpr SystemInfo(uint32_t free = 0, uint32_t minimum = 0,
                       uint32_t largest = 0, size_t bleGroups = 0,
                       const char* wifi = "Off")
      : freeHeap(free),
        minimumFreeHeap(minimum),
        largestFreeBlock(largest),
        activeBleGroups(bleGroups),
        wifiState(wifi) {}

  uint32_t freeHeap = 0;
  uint32_t minimumFreeHeap = 0;
  uint32_t largestFreeBlock = 0;
  size_t activeBleGroups = 0;
  const char* wifiState = "Off";
};

SystemInfo systemInfo();

}  // namespace studio
