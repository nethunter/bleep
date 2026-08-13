#pragma once

#include <cstddef>
#include <cstdint>

namespace gopro {

constexpr const char* kAdvertisedServiceUuid = "fea6";
constexpr const char* kControlServiceUuid = "fea6";
constexpr const char* kCommandCharacteristicUuid =
    "b5f90072-aa8d-11e3-9046-0002a5d5c51b";
constexpr const char* kResponseCharacteristicUuid =
    "b5f90073-aa8d-11e3-9046-0002a5d5c51b";
constexpr const char* kQueryCharacteristicUuid =
    "b5f90076-aa8d-11e3-9046-0002a5d5c51b";
constexpr const char* kQueryResponseCharacteristicUuid =
    "b5f90077-aa8d-11e3-9046-0002a5d5c51b";

constexpr uint8_t kSetShutterCommand = 0x01;
constexpr uint8_t kSetPairingStateCommand = 0x17;
constexpr uint8_t kGetHardwareInfoCommand = 0x3c;
constexpr uint8_t kGetStatusValues = 0x13;
constexpr uint8_t kRegisterStatusUpdates = 0x53;
constexpr uint8_t kNotifyStatusUpdate = 0x93;
constexpr uint8_t kGetStatusValues2Byte = 0x16;
constexpr uint8_t kRegisterStatusUpdates2Byte = 0x56;
constexpr uint8_t kNotifyStatusUpdate2Byte = 0x96;
constexpr uint8_t kEncodingStatus = 0x0a;
constexpr uint8_t kSuccessStatus = 0x00;

struct Packet {
  uint8_t bytes[8] = {};
  size_t len = 0;
};

struct Response {
  bool valid = false;
  uint8_t command = 0;
  uint8_t status = 0xff;
};

struct Message {
  uint8_t bytes[128] = {};
  size_t len = 0;
};

struct StatusResponse {
  bool valid = false;
  uint8_t operation = 0;
  uint8_t status = 0xff;
  bool hasEncoding = false;
  bool encoding = false;
};

class PacketAccumulator {
 public:
  bool feed(const uint8_t* data, size_t len, Message& message);
  void reset();

 private:
  size_t expected_ = 0;
  size_t length_ = 0;
  uint8_t continuation_ = 0;
  uint8_t bytes_[sizeof(Message::bytes)] = {};
};

Packet buildSetShutter(bool enabled);
Packet buildSetPairingState();
Packet buildGetHardwareInfo();
Packet buildGetEncoding(bool twoByteIds = false);
Packet buildRegisterEncoding(bool twoByteIds = false);
Response parseResponse(const uint8_t* data, size_t len);
Response parseCommandPayload(const uint8_t* data, size_t len);
StatusResponse parseStatusPayload(const uint8_t* data, size_t len);

}  // namespace gopro
