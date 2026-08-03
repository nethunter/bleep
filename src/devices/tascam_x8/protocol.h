#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

namespace tascam_x8 {

constexpr const char* kDeviceName = "Portacapture X8";
constexpr const char* kPrimaryServiceUuid =
    "2456e1b9-26e2-8f83-e744-f34f01e9d701";
constexpr const char* kDataCharacteristicUuid =
    "2456e1b9-26e2-8f83-e744-f34f01e9d703";
constexpr const char* kSessionCharacteristicUuid =
    "2456e1b9-26e2-8f83-e744-f34f01e9d704";

constexpr uint8_t kSessionOpen = 0xFE;
constexpr uint8_t kSessionOpenResponse = 0x10;
constexpr uint8_t kSessionKeepalive = 0x7F;
constexpr uint32_t kSessionKeepaliveMs = 7000;

constexpr size_t kMaxDecodedFrame = 96;
constexpr size_t kMaxEncodedFrame = 128;
constexpr size_t kMaxWrite = 244;

struct FrameBytes {
  uint8_t bytes[kMaxWrite] = {};
  size_t len = 0;

  const uint8_t* data() const { return bytes; }
};

struct ParsedFrame {
  const uint8_t* data = nullptr;
  size_t len = 0;
};

FrameBytes encodeFrame(const uint8_t* payload, size_t payloadLen);
FrameBytes buildRecordStart();
FrameBytes buildRecordStop();
FrameBytes buildInitialization();

class FrameScanner {
 public:
  using Handler = std::function<void(const ParsedFrame&)>;

  void feed(const uint8_t* data, size_t len, const Handler& handler);
  void reset() {
    encodedLen_ = 0;
    discarding_ = false;
  }

 private:
  void finish(const Handler& handler);

  uint8_t encoded_[kMaxEncodedFrame] = {};
  size_t encodedLen_ = 0;
  bool discarding_ = false;
};

enum class RecordEvent : uint8_t {
  None,
  Started,
  Stopped,
};

RecordEvent parseRecordEvent(const ParsedFrame& frame);

}  // namespace tascam_x8
