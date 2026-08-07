#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

namespace zhiyun_x100 {

constexpr const char* kProxyServiceUuid =
    "00001828-0000-1000-8000-00805f9b34fb";
constexpr const char* kProxyAdvertisedService = "1828";
constexpr const char* kControlServiceUuid =
    "0000fee9-0000-1000-8000-00805f9b34fb";
constexpr const char* kWriteCharacteristicUuid =
    "d44bc439-abfd-45a2-b575-925416129600";
constexpr const char* kNotifyCharacteristicUuid =
    "d44bc439-abfd-45a2-b575-925416129601";

constexpr uint16_t kCommandIdentity = 0x2003;
constexpr uint16_t kCommandFirmware = 0x8001;
constexpr uint16_t kCommandStatus = 0x2001;
constexpr uint16_t kCommandMode = 0x0006;
constexpr uint16_t kCommandBrightness = 0x1001;
constexpr uint16_t kCommandCct = 0x1002;
constexpr uint16_t kCommandPower = 0x1008;
constexpr uint16_t kMinKelvin = 2700;
constexpr uint16_t kMaxKelvin = 6500;
constexpr size_t kMaxFrameSize = 80;

struct FrameBytes {
  uint8_t bytes[kMaxFrameSize] = {};
  size_t length = 0;
};

struct ParsedFrame {
  bool response = false;
  uint16_t sequence = 0;
  uint16_t command = 0;
  const uint8_t* payload = nullptr;
  size_t payloadLength = 0;
};

uint16_t crc16Xmodem(const uint8_t* data, size_t length);
FrameBytes buildRequest(uint16_t sequence, uint16_t command,
                        const uint8_t* payload, size_t payloadLength);
FrameBytes buildReadRequest(uint16_t sequence, uint16_t command);
FrameBytes buildPowerWrite(uint16_t sequence, bool on);
FrameBytes buildBrightnessWrite(uint16_t sequence, float percent);
FrameBytes buildCctWrite(uint16_t sequence, uint16_t kelvin);

bool parseBrightness(const ParsedFrame& frame, float& percent);
bool parseCct(const ParsedFrame& frame, uint16_t& kelvin);
bool parsePower(const ParsedFrame& frame, bool& on);
bool identityIsX100(const ParsedFrame& frame);

class FrameScanner {
 public:
  using Handler = std::function<void(const ParsedFrame&)>;

  void feed(const uint8_t* data, size_t length, const Handler& handler);
  void reset() { length_ = 0; }

 private:
  void process(const Handler& handler);
  uint8_t bytes_[kMaxFrameSize] = {};
  size_t length_ = 0;
};

}  // namespace zhiyun_x100
