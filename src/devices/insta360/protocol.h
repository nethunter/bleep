#pragma once
#include <cstddef>
#include <cstdint>
namespace insta360 {
constexpr const char* kServiceUuid = "0000ce80-0000-1000-8000-00805f9b34fb";
constexpr const char* kWriteUuid = "0000ce81-0000-1000-8000-00805f9b34fb";
constexpr const char* kNotifyUuid = "0000ce82-0000-1000-8000-00805f9b34fb";
constexpr uint8_t kShutterCommand[9] = {0xfc, 0xef, 0xfe, 0x86, 0x00, 0x03, 0x01, 0x02, 0x00};
bool matchesCameraName(const char* name);
bool isGoUltra(const char* name);
}  // namespace insta360
