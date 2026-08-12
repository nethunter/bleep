#include "devices/insta360/protocol.h"
#include <cstring>
namespace insta360 {
namespace { bool begins(const char* value, const char* prefix) { return value && std::strncmp(value, prefix, std::strlen(prefix)) == 0; } }
bool matchesCameraName(const char* name) {
  return begins(name, "X3 ") || begins(name, "X4 ") || begins(name, "X5 ") ||
         begins(name, "RS ") || begins(name, "ONE ") || begins(name, "GO 3") ||
         begins(name, "Insta360 GO 3") || begins(name, "GO Ultra") ||
         begins(name, "Insta360 GO Ultra");
}
bool isGoUltra(const char* name) { return name && (std::strstr(name, "GO Ultra") || std::strstr(name, "GO ULTRA")); }

bool buildWakeAdvertisementData(
    const char* cameraName,
    uint8_t (&advertisement)[kWakeAdvertisementDataLength],
    uint8_t (&scanResponse)[kWakeScanResponseDataLength]) {
  if (!matchesCameraName(cameraName)) return false;
  const char* serial = std::strrchr(cameraName, ' ');
  if (serial == nullptr || serial == cameraName || std::strlen(++serial) != 6) {
    return false;
  }
  for (size_t i = 0; i < 6; ++i) {
    const char value = serial[i];
    if (!((value >= '0' && value <= '9') ||
          (value >= 'A' && value <= 'Z') ||
          (value >= 'a' && value <= 'z'))) {
      return false;
    }
  }

  const uint8_t captured[kWakeAdvertisementDataLength] = {
      0x02, 0x01, 0x06, 0x1b, 0xff, 0x4c, 0x00, 0x02,
      0x15, 0x09, 'O',  'R',  'B',  'I',  'T',  0x09,
      0xff, 0x0f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0xe4, 0x01};
  std::memcpy(advertisement, captured, sizeof(captured));
  std::memcpy(advertisement + 19, serial, 6);
  const uint8_t capturedScanResponse[kWakeScanResponseDataLength] = {
      0x02, 0x0a, 0x00};
  std::memcpy(scanResponse, capturedScanResponse,
              sizeof(capturedScanResponse));
  return true;
}

namespace {

bool isDurationText(const uint8_t* data, size_t len) {
  if (data == nullptr || len == 0 || (data[len - 1] != 'm' &&
                                      data[len - 1] != 'h')) {
    return false;
  }
  for (size_t i = 0; i < len; ++i) {
    if ((data[i] < '0' || data[i] > '9') && data[i] != 'h' &&
        data[i] != 'm') {
      return false;
    }
  }
  return true;
}

bool isPhotoCountText(const uint8_t* data, size_t len) {
  if (data == nullptr || len < 2 || data[0] != ' ') return false;
  for (size_t i = 1; i < len; ++i) {
    if ((data[i] < '0' || data[i] > '9') && data[i] != '+') return false;
  }
  return true;
}

}  // namespace

bool decodeCaptureStatus(const uint8_t* data, size_t len,
                         CaptureStatus& status) {
  if (data == nullptr || len < 6 || data[0] != 0xfe || data[1] != 0xef ||
      data[2] != 0xfe) {
    return false;
  }
  CaptureStatus decoded;
  if (data[3] == 0x10 && data[4] == 0x80 && data[5] == 0x0d &&
      len == 19 && data[10] == '.' && data[13] == ':' && data[16] == ':') {
    decoded.mode = CaptureMode::Video;
    decoded.phase = CapturePhase::Active;
  } else if (data[3] == 0x10 && data[4] == 0x80 && data[5] == 0x07 &&
             len > 10 && data[6] == 0x01 && data[8] == 0x46 &&
             data[9] == 0x01 && isDurationText(data + 10, len - 10)) {
    decoded.mode = CaptureMode::Video;
    decoded.phase = CapturePhase::Idle;
  } else if (data[3] == 0x10 && data[4] == 0x80 && data[5] == 0x09 &&
             len > 11 && data[6] == 0x01 && data[8] == 0x46 &&
             data[9] == 0x01 && isPhotoCountText(data + 10, len - 10)) {
    decoded.mode = CaptureMode::Photo;
    decoded.phase = CapturePhase::Idle;
  } else if (data[3] == 0x10 && data[4] == 0x80 && data[5] == 0x05 &&
             len == 11) {
    decoded.mode = CaptureMode::Photo;
    decoded.phase = CapturePhase::Saving;
  } else if (data[3] == 0x55 && len == 13 && data[4] == 0x00 &&
             data[5] == 0x07 && data[6] == 0x00) {
    decoded.mode = CaptureMode::Video;
    switch (data[7]) {
      case 0x00: decoded.phase = CapturePhase::Idle; break;
      case 0x01: decoded.phase = CapturePhase::Starting; break;
      case 0x02: decoded.phase = CapturePhase::Active; break;
      case 0x04: decoded.phase = CapturePhase::Stopping; break;
      default: return false;
    }
  } else if (data[3] == 0x55 && len == 13 && data[4] == 0x00 &&
             data[5] == 0x07 && data[6] == 0x01) {
    decoded.mode = CaptureMode::Photo;
    switch (data[7]) {
      case 0x00: decoded.phase = CapturePhase::Idle; break;
      case 0x01: decoded.phase = CapturePhase::Starting; break;
      case 0x02: decoded.phase = CapturePhase::Active; break;
      case 0x05: decoded.phase = CapturePhase::Saving; break;
      default: return false;
    }
  } else {
    return false;
  }
  status = decoded;
  return true;
}
}  // namespace insta360
