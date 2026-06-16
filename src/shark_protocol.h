#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

// C++ port of the iFootage Shark Nano II BLE protocol, derived from the
// reverse-engineering project (docs/protocol.md, protocol.py, client.py).
//
// Frame envelope: AA BB <body> <crc32_be(body)> BB AA
//   body = <family> <code> <uint16 length_be> <data[length]>
// CRC is the standard IEEE/zlib CRC32 over the body bytes only.

namespace shark {

constexpr uint8_t kFramePrefix0 = 0xAA;
constexpr uint8_t kFramePrefix1 = 0xBB;
constexpr uint8_t kFrameSuffix0 = 0xBB;
constexpr uint8_t kFrameSuffix1 = 0xAA;

// Largest frame we build or expect to parse in a single unit. The longest
// known structured frames are the 41-byte status snapshot (06 00 0029) and the
// 29-byte timing table, both well under this bound including the 12-byte
// envelope overhead.
constexpr size_t kMaxFrame = 96;

// Keypoint model: 8 slots A..H. Slot A (index 0) is the route start; it has no
// inbound travel segment, so it carries no timing tuple.
constexpr int kKeypointCount = 8;

// Keypoint slot markers in a 03 03 0009 command.
constexpr uint8_t kMarkerDelete = 0x00;
constexpr uint8_t kMarkerSet = 0x01;
constexpr uint8_t kMarkerIdle = 0x02;
constexpr uint8_t kMarkerGo = 0x03;

// Run states for 03 0d 0003 <tx> 00 <state>.
constexpr uint8_t kRunStop = 0x00;
constexpr uint8_t kRunStandby = 0x03;
constexpr uint8_t kRunStart = 0x04;

// Route config slot indexes for 03 05 000a (0xFF means "leave unchanged").
constexpr int kRouteConfigSlots = 9;
constexpr int kRouteLoopSlot = 1;       // 0x00 once, 0x01 loop
constexpr int kRouteDirectionSlot = 2;  // 0x00 forward (A->), 0x01 reverse

// Timing table (08 001d): <tx> + 7 four-byte tuples, one per destination
// keypoint starting at B. Within a tuple: byte2 = travel speed percent,
// byte3 = hold seconds.
constexpr size_t kTimingDataLen = 29;
constexpr int kTimingTupleCount = 7;
constexpr int kTimingSpeedOffset = 2;
constexpr int kTimingHoldOffset = 3;

// Run-progress notification (16 10 0013) data length.
constexpr size_t kRunProgressDataLen = 19;

// Standard IEEE CRC32 (poly 0xEDB88320, init/final 0xFFFFFFFF) == zlib crc32.
uint32_t crc32Ieee(const uint8_t* data, size_t len);

// A small owned buffer holding a fully encoded frame ready for a GATT write.
struct FrameBytes {
  uint8_t bytes[kMaxFrame];
  size_t len = 0;

  const uint8_t* data() const { return bytes; }
};

// Encode a frame body (family, code, data) into a complete enveloped frame.
FrameBytes encodeFrame(uint8_t family, uint8_t code, const uint8_t* data, size_t dataLen);

// A parsed view over a complete frame. `data` points into the caller's buffer
// and is only valid for the lifetime of that buffer.
struct ParsedFrame {
  uint8_t family = 0;
  uint8_t code = 0;
  uint16_t kind = 0;  // equals dataLen
  const uint8_t* data = nullptr;
  size_t dataLen = 0;
};

// Streaming frame scanner. Mirrors protocol.py scan_frames_from_bytes: BLE
// notifications are buffered, complete frames are dispatched to `handler`, and
// trailing incomplete bytes are retained for the next feed.
class FrameScanner {
 public:
  using Handler = std::function<void(const ParsedFrame&)>;

  void feed(const uint8_t* data, size_t len, const Handler& handler);
  void reset() { len_ = 0; }

 private:
  // Holds the largest possible in-flight fragment plus a little slack.
  static constexpr size_t kBufCap = 512;
  uint8_t buf_[kBufCap];
  size_t len_ = 0;
};

// Frame builders. Each returns a fully encoded frame.

FrameBytes buildControlPing(uint8_t code, uint8_t tx);
FrameBytes buildKeypointSlots(uint8_t tx, const uint8_t slots[kKeypointCount]);
FrameBytes buildKeypointAction(int slotIndex, uint8_t marker, uint8_t tx);
// Delete cascades through the target slot and any later present slots, matching
// the device behavior documented in protocol.md.
FrameBytes buildKeypointDelete(int slotIndex, const bool present[kKeypointCount], uint8_t tx);
FrameBytes buildRunState(uint8_t state, uint8_t tx);
FrameBytes buildRouteConfig(uint8_t tx, const int* slotIndexes, const uint8_t* slotValues, int count);
FrameBytes buildLoop(bool loopOn, uint8_t tx);
FrameBytes buildDirection(bool reverse, uint8_t tx);
FrameBytes buildTimingQuery(uint8_t tx);
FrameBytes buildManualTracking(bool enabled, uint8_t tx);

// Read-modify-write of an 08 001d timing table. `table` is the 29-byte data
// payload of a captured 06/03 08 001d frame. `slotIndex` is A..H (0..7); A is
// rejected. speed<0 / hold<0 mean "leave unchanged". Returns false on bad input.
bool patchTimingTable(const uint8_t* table, size_t tableLen, int slotIndex, int speed,
                      int holdSeconds, uint8_t tx, FrameBytes& out);

// Decoded 16 10 0013 run-progress event.
struct RunProgress {
  uint8_t sequence = 0;
  uint8_t stateCode = 0;
  uint8_t stage = 0;
  uint8_t segment = 0;
  uint16_t progressUnits = 0;
  float progressPercent = 0.0f;
  uint8_t stepIndex = 0;
};

bool isRunProgress(const ParsedFrame& frame);
bool parseRunProgress(const ParsedFrame& frame, RunProgress& out);
const char* runStateLabel(uint8_t stateCode);

}  // namespace shark
