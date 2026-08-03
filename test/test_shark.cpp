#include <unity.h>

#include <cmath>
#include <cstring>

#include "shark_protocol.h"
#include "shark_state.h"

using namespace shark;

namespace {

void assertFrameEquals(const FrameBytes& frame, const uint8_t* expected, size_t len) {
  TEST_ASSERT_EQUAL_UINT32(len, frame.len);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, frame.data(), len);
}

ParsedFrame parsed(uint8_t family, uint8_t code, const uint8_t* data, size_t len) {
  ParsedFrame frame;
  frame.family = family;
  frame.code = code;
  frame.kind = static_cast<uint16_t>(len);
  frame.data = data;
  frame.dataLen = len;
  return frame;
}

void test_crc_and_known_frame_fixture() {
  const uint8_t standard[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
  TEST_ASSERT_EQUAL_HEX32(0xCBF43926u, crc32Ieee(standard, sizeof(standard)));

  const uint8_t expected[] = {
      0xAA, 0xBB, 0x06, 0x15, 0x00, 0x01, 0x42, 0xAF, 0x6C, 0xB4, 0xED, 0xBB, 0xAA,
  };
  assertFrameEquals(buildControlPing(0x15, 0x42), expected, sizeof(expected));

  TEST_ASSERT_EQUAL_UINT32(0, encodeFrame(1, 2, nullptr, 1).len);
  uint8_t tooLarge[kMaxFrame] = {};
  TEST_ASSERT_EQUAL_UINT32(0, encodeFrame(1, 2, tooLarge, sizeof(tooLarge)).len);
}

void test_scanner_handles_fragmentation_noise_and_multiple_frames() {
  const FrameBytes first = buildControlPing(0x15, 0x42);
  const FrameBytes second = buildRunState(kRunStandby, 0x43);
  FrameScanner scanner;
  int count = 0;
  uint8_t codes[2] = {};

  scanner.feed(first.data(), 1, [&](const ParsedFrame&) { TEST_FAIL_MESSAGE("partial frame"); });
  scanner.feed(first.data() + 1, first.len - 1, [&](const ParsedFrame& frame) {
    codes[count++] = frame.code;
    TEST_ASSERT_EQUAL_HEX8(0x42, frame.data[0]);
  });

  uint8_t combined[2 + kMaxFrame * 2] = {0x00, 0x7F};
  std::memcpy(combined + 2, first.data(), first.len);
  std::memcpy(combined + 2 + first.len, second.data(), second.len);
  scanner.feed(combined, 2 + first.len + second.len, [&](const ParsedFrame& frame) {
    if (count < 2) {
      codes[count] = frame.code;
    }
    ++count;
  });

  TEST_ASSERT_EQUAL_INT(3, count);
  TEST_ASSERT_EQUAL_HEX8(0x15, codes[0]);
  TEST_ASSERT_EQUAL_HEX8(0x15, codes[1]);
}

void test_scanner_resynchronizes_after_bad_candidates() {
  const FrameBytes valid = buildControlPing(0x03, 0x17);
  uint8_t corrupt[kMaxFrame] = {};
  std::memcpy(corrupt, valid.data(), valid.len);
  corrupt[valid.len - 3] ^= 0x01;

  uint8_t input[kMaxFrame * 2] = {};
  std::memcpy(input, corrupt, valid.len);
  std::memcpy(input + valid.len, valid.data(), valid.len);

  FrameScanner scanner;
  int count = 0;
  scanner.feed(input, valid.len * 2, [&](const ParsedFrame&) { ++count; });
  TEST_ASSERT_EQUAL_INT(1, count);

  const uint8_t oversizedHeader[] = {0xAA, 0xBB, 0x06, 0x00, 0x01, 0xF4};
  uint8_t oversizedThenValid[kMaxFrame * 2] = {};
  std::memcpy(oversizedThenValid, oversizedHeader, sizeof(oversizedHeader));
  std::memcpy(oversizedThenValid + sizeof(oversizedHeader), valid.data(), valid.len);
  count = 0;
  scanner.feed(oversizedThenValid, sizeof(oversizedHeader) + valid.len,
               [&](const ParsedFrame&) { ++count; });
  TEST_ASSERT_EQUAL_INT(1, count);
}

void test_command_builders_and_timing_patch() {
  const FrameBytes motion = buildMotionVector(150, -150, 0x09);
  FrameScanner scanner;
  scanner.feed(motion.data(), motion.len, [](const ParsedFrame& frame) {
    TEST_ASSERT_EQUAL_HEX8(0x03, frame.family);
    TEST_ASSERT_EQUAL_HEX8(0x04, frame.code);
    TEST_ASSERT_EQUAL_HEX8(100, frame.data[1]);
    TEST_ASSERT_EQUAL_HEX8(static_cast<uint8_t>(-100), frame.data[2]);
  });

  uint8_t table[kTimingDataLen] = {};
  table[0] = 0x01;
  table[3] = 25;
  table[4] = 8;
  FrameBytes patched;
  TEST_ASSERT_TRUE(patchTimingTable(table, sizeof(table), 1, 80, 12, 0x22, patched));
  scanner.feed(patched.data(), patched.len, [](const ParsedFrame& frame) {
    TEST_ASSERT_EQUAL_HEX8(0x03, frame.family);
    TEST_ASSERT_EQUAL_HEX8(0x08, frame.code);
    TEST_ASSERT_EQUAL_HEX8(0x22, frame.data[0]);
    TEST_ASSERT_EQUAL_UINT8(80, frame.data[3]);
    TEST_ASSERT_EQUAL_UINT8(12, frame.data[4]);
  });
  TEST_ASSERT_FALSE(patchTimingTable(table, sizeof(table), 0, 50, 2, 1, patched));
  TEST_ASSERT_EQUAL_UINT32(0, buildKeypointSlots(1, nullptr).len);
  TEST_ASSERT_EQUAL_UINT32(0, buildRouteConfig(1, nullptr, nullptr, 1).len);
}

void test_state_reducer_decodes_snapshots_and_progress() {
  SharkState state;

  uint8_t status[41] = {};
  status[2] = 73;
  reduceFrame(state, parsed(0x06, 0x00, status, sizeof(status)));
  TEST_ASSERT_EQUAL_INT(73, state.battery);

  const uint8_t presence[] = {0x10, 1, 1, 1, 0, 0, 0, 0, 0};
  reduceFrame(state, parsed(0x06, 0x03, presence, sizeof(presence)));
  TEST_ASSERT_TRUE(state.presenceKnown);
  TEST_ASSERT_TRUE(state.present[2]);
  TEST_ASSERT_FALSE(state.present[3]);

  uint8_t timing[kTimingDataLen] = {};
  timing[3] = 65;
  timing[4] = 9;
  reduceFrame(state, parsed(0x06, 0x08, timing, sizeof(timing)));
  TEST_ASSERT_TRUE(state.timingKnown);
  TEST_ASSERT_EQUAL_INT(65, state.speed[1]);
  TEST_ASSERT_EQUAL_INT(9, state.hold[1]);

  uint8_t progress[kRunProgressDataLen] = {};
  progress[2] = kRunStart;
  progress[3] = 1;
  progress[5] = 0x01;
  progress[6] = 0xF4;
  reduceFrame(state, parsed(0x16, 0x10, progress, sizeof(progress)));
  TEST_ASSERT_EQUAL_HEX8(kRunStart, state.runStateCode);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 75.0f, state.runPercent);
  TEST_ASSERT_EQUAL_STRING("running", state.runText);

  progress[2] = kRunStop;
  reduceFrame(state, parsed(0x16, 0x10, progress, sizeof(progress)));
  TEST_ASSERT_EQUAL_HEX8(kRunStop, state.runStateCode);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, state.runPercent);
}

void test_reset_preserves_link_identity_and_preferences() {
  SharkState state;
  state.link = SharkState::Link::Connected;
  std::strcpy(state.deviceName, "Shark");
  state.hasSavedDevice = true;
  state.loopOn = true;
  state.battery = 50;
  state.present[0] = true;

  resetDeviceState(state);

  TEST_ASSERT_EQUAL_INT(static_cast<int>(SharkState::Link::Connected),
                        static_cast<int>(state.link));
  TEST_ASSERT_EQUAL_STRING("Shark", state.deviceName);
  TEST_ASSERT_TRUE(state.hasSavedDevice);
  TEST_ASSERT_TRUE(state.loopOn);
  TEST_ASSERT_EQUAL_INT(-1, state.battery);
  TEST_ASSERT_FALSE(state.present[0]);
}

}  // namespace

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_crc_and_known_frame_fixture);
  RUN_TEST(test_scanner_handles_fragmentation_noise_and_multiple_frames);
  RUN_TEST(test_scanner_resynchronizes_after_bad_candidates);
  RUN_TEST(test_command_builders_and_timing_patch);
  RUN_TEST(test_state_reducer_decodes_snapshots_and_progress);
  RUN_TEST(test_reset_preserves_link_identity_and_preferences);
  return UNITY_END();
}
