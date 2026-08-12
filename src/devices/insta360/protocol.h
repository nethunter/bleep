#pragma once
#include <cstddef>
#include <cstdint>
namespace insta360 {
constexpr const char* kServiceUuid = "0000ce80-0000-1000-8000-00805f9b34fb";
constexpr const char* kWriteUuid = "0000ce81-0000-1000-8000-00805f9b34fb";
constexpr const char* kNotifyUuid = "0000ce82-0000-1000-8000-00805f9b34fb";
constexpr const char* kInfoUuid = "0000ce83-0000-1000-8000-00805f9b34fb";
constexpr const char* kGattCharacteristicOrder[] = {
    kNotifyUuid, kWriteUuid, kInfoUuid};
constexpr const char* kAdvertisedName = "Insta360 Remote (Bleep)";
constexpr uint16_t kAdvertisedAppearance = 0x0180;
constexpr uint8_t kAdvertisementData[] = {
    0x02, 0x01, 0x06,
    0x18, 0x09, 'I', 'n', 's', 't', 'a', '3', '6', '0', ' ', 'R', 'e', 'm',
    'o', 't', 'e', ' ', '(', 'B', 'l', 'e', 'e', 'p', ')'};
constexpr uint8_t kScanResponseData[] = {
    0x03, 0x19, 0x80, 0x01,
    0x03, 0x03, 0x80, 0xce};
constexpr size_t kWakeAdvertisementDataLength = 31;
constexpr size_t kWakeScanResponseDataLength = 3;
constexpr uint8_t kShutterCommand[9] = {0xfc, 0xef, 0xfe, 0x86, 0x00, 0x03, 0x01, 0x02, 0x00};
constexpr uint8_t kPowerOffCommand[9] = {0xfc, 0xef, 0xfe, 0x86, 0x00, 0x03, 0x01, 0x00, 0x03};

enum class CaptureMode : uint8_t { Unknown, Video, Photo };
enum class CapturePhase : uint8_t {
  Unknown,
  Idle,
  Starting,
  Active,
  Stopping,
  Saving,
};

struct CaptureStatus {
  CaptureMode mode = CaptureMode::Unknown;
  CapturePhase phase = CapturePhase::Unknown;
};

bool matchesCameraName(const char* name);
bool isGoUltra(const char* name);
bool buildWakeAdvertisementData(
    const char* cameraName,
    uint8_t (&advertisement)[kWakeAdvertisementDataLength],
    uint8_t (&scanResponse)[kWakeScanResponseDataLength]);
bool decodeCaptureStatus(const uint8_t* data, size_t len,
                         CaptureStatus& status);
}  // namespace insta360
