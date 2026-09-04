#pragma once
#include <cstddef>
#include <cstdint>
#include "devices/insta360/state.h"
namespace insta360 {
constexpr const char* kServiceUuid = "0000ce80-0000-1000-8000-00805f9b34fb";
constexpr const char* kWriteUuid = "0000ce81-0000-1000-8000-00805f9b34fb";
constexpr const char* kNotifyUuid = "0000ce82-0000-1000-8000-00805f9b34fb";
constexpr const char* kInfoUuid = "0000ce83-0000-1000-8000-00805f9b34fb";
constexpr const char* kGattCharacteristicOrder[] = {
    kNotifyUuid, kWriteUuid, kInfoUuid};
constexpr const char* kGpsAdvertisedName = "Insta360 Remote (Bleep)";
constexpr const char* kMiniAdvertisedName = "Insta360 Mini Remote";
constexpr uint16_t kAdvertisedAppearance = 0x0180;
constexpr uint8_t kGpsAdvertisementData[] = {
    0x02, 0x01, 0x06,
    0x18, 0x09, 'I', 'n', 's', 't', 'a', '3', '6', '0', ' ', 'R', 'e', 'm',
    'o', 't', 'e', ' ', '(', 'B', 'l', 'e', 'e', 'p', ')'};
constexpr uint8_t kGpsScanResponseData[] = {
    0x03, 0x19, 0x80, 0x01,
    0x03, 0x03, 0x80, 0xce};
constexpr uint8_t kMiniAdvertisementData[] = {
    0x02, 0x01, 0x06,
    0x03, 0x19, 0x80, 0x01,
    0x15, 0x09, 'I', 'n', 's', 't', 'a', '3', '6', '0', ' ', 'M', 'i', 'n',
    'i', ' ', 'R', 'e', 'm', 'o', 't', 'e'};
constexpr uint8_t kMiniScanResponseData[] = {
    0x03, 0x03, 0x12, 0x18,
    0x02, 0x0a, 0x00};
constexpr size_t kWakeAdvertisementDataLength = 31;
constexpr size_t kWakeScanResponseDataLength = 3;
constexpr uint8_t kGpsShutterCommand[9] = {
    0xfc, 0xef, 0xfe, 0x86, 0x00, 0x03, 0x01, 0x02, 0x00};
constexpr uint8_t kMiniShutterCommand[9] = {
    0xfc, 0xef, 0xfe, 0x86, 0x00, 0x03, 0x01, 0x00, 0x00};
constexpr uint8_t kPowerOffCommand[9] = {0xfc, 0xef, 0xfe, 0x86, 0x00, 0x03, 0x01, 0x00, 0x03};
constexpr uint8_t kPowerOffAccepted[7] = {
    0xfe, 0xef, 0xfe, 0x56, 0x00, 0x01, 0x13};

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
bool supportsRemoteProtocol(const char* name, RemoteProtocol protocol);
bool isGoUltra(const char* name);
bool buildWakeAdvertisementData(
    const char* cameraName,
    uint8_t (&advertisement)[kWakeAdvertisementDataLength],
    uint8_t (&scanResponse)[kWakeScanResponseDataLength]);
bool decodeCaptureStatus(const uint8_t* data, size_t len,
                         CaptureStatus& status);
bool isPowerOffAccepted(const uint8_t* data, size_t len);
}  // namespace insta360
