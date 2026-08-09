#pragma once

#include <cstddef>
#include <cstdint>

namespace dji_osmo {

constexpr const char* kServiceUuid = "fff0";
constexpr const char* kNotifyUuid = "fff4";
constexpr const char* kWriteUuid = "fff5";
constexpr uint8_t kCmdConnection = 0x19;
constexpr uint8_t kCmdRecord = 0x03;
constexpr uint8_t kCmdStatusSubscribe = 0x05;
constexpr uint8_t kCmdStatusPush = 0x02;
constexpr uint8_t kCmdSetGeneral = 0x00;
constexpr uint8_t kCmdSetCamera = 0x1d;

struct Packet {
  uint8_t bytes[96] = {};
  size_t len = 0;
};

struct Frame {
  bool valid = false;
  uint8_t commandType = 0;
  uint16_t sequence = 0;
  uint8_t commandSet = 0;
  uint8_t commandId = 0;
  const uint8_t* payload = nullptr;
  size_t payloadLength = 0;
};

uint16_t crc16(const uint8_t* data, size_t length);
uint32_t crc32(const uint8_t* data, size_t length);
Packet buildConnectionRequest(uint16_t sequence, uint32_t deviceId,
                              const uint8_t localAddress[6],
                              uint8_t verificationMode,
                              uint16_t verificationCode);
Packet buildConnectionResponse(uint16_t sequence, uint32_t deviceId,
                               uint8_t cameraIndex = 0);
Packet buildStatusSubscription(uint16_t sequence);
Packet buildRecordControl(uint16_t sequence, bool start);
Frame parseFrame(const uint8_t* data, size_t length);
size_t declaredFrameLength(const uint8_t* data, size_t length);
bool parseConnectionApproval(const Frame& frame, bool& approved);
bool decodeCameraRecordingStatus(uint8_t cameraMode, uint8_t cameraStatus,
                                 bool& recording);

}  // namespace dji_osmo
