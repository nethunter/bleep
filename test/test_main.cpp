#include <unity.h>

#include <cmath>
#include <cstring>

#include "core/config_store.h"
#include "core/device_driver.h"
#include "core/device_manager.h"
#include "core/driver_catalog.h"
#include "devices/canon_ble/protocol.h"
#include "devices/canon_ble/state.h"
#include "devices/shark_nano_ii/protocol.h"
#include "devices/shark_nano_ii/state.h"
#include "devices/tascam_x8/protocol.h"
#include "devices/tascam_x8/state.h"

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

class MemoryBackend : public studio::IConfigBackend {
 public:
  size_t read(uint8_t* destination, size_t capacity) override {
    if (length_ > capacity) {
      return 0;
    }
    std::memcpy(destination, data_, length_);
    return length_;
  }

  bool write(const uint8_t* data, size_t length) override {
    if (length > sizeof(data_)) {
      return false;
    }
    std::memcpy(data_, data, length);
    length_ = length;
    return true;
  }

  void corruptLastByte() {
    if (length_ > 0) {
      data_[length_ - 1] ^= 0xFF;
    }
  }

 private:
  uint8_t data_[studio::ConfigStore::kMaxBlobSize] = {};
  size_t length_ = 0;
};

class LegacyBackend : public studio::ILegacySharkBackend {
 public:
  bool readLegacyShark(studio::LegacySharkConfig& config) override {
    if (!available) {
      return false;
    }
    config = value;
    return true;
  }

  bool available = false;
  studio::LegacySharkConfig value;
};

class FakeDriver : public studio::DeviceDriver {
 public:
  explicit FakeDriver(
      studio::DriverId id = studio::DriverId::SharkNanoII)
      : id_(id) {}
  studio::DriverId driverId() const override { return id_; }
  void activate(const studio::DeviceRecord& record) override {
    active = true;
    activeInstance = record.instanceId;
    ++activationCount;
  }
  void deactivate() override {
    active = false;
    activeInstance = studio::kInvalidInstanceId;
    ++deactivationCount;
  }
  void loop() override { ++loopCount; }
  studio::CommandStatus dispatch(const studio::DeviceCommand& command) override {
    lastCommand = command.type;
    ++dispatchCount;
    return studio::CommandStatus::Succeeded;
  }
  studio::DeviceRuntimeState runtimeState() const override {
    studio::DeviceRuntimeState state;
    state.link = active ? studio::LinkState::Connected : studio::LinkState::Disconnected;
    return state;
  }
  const void* specializedState() const override { return nullptr; }
  void forgetPairing(const studio::DeviceRecord&) override {
    ++forgetPairingCount;
  }
  bool consumePairingUpdate(studio::DeviceRecord&) override { return false; }

  bool active = false;
  studio::InstanceId activeInstance = studio::kInvalidInstanceId;
  int activationCount = 0;
  int deactivationCount = 0;
  int loopCount = 0;
  int dispatchCount = 0;
  int forgetPairingCount = 0;
  studio::CommandType lastCommand = studio::CommandType::Refresh;

 private:
  studio::DriverId id_;
};

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

void test_canon_smartphone_handshake_and_record_protocol() {
  const canon_ble::CommandBytes request =
      canon_ble::buildHandshakeRequest("StudioRemote");
  TEST_ASSERT_EQUAL_UINT32(13, request.len);
  TEST_ASSERT_EQUAL_HEX8(0x01, request.bytes[0]);
  TEST_ASSERT_EQUAL_UINT8_ARRAY("StudioRemote", &request.bytes[1], 12);

  uint8_t controllerId[16];
  for (size_t i = 0; i < sizeof(controllerId); ++i) {
    controllerId[i] = static_cast<uint8_t>(i + 1);
  }
  const canon_ble::CommandBytes id = canon_ble::buildControllerId(controllerId);
  TEST_ASSERT_EQUAL_UINT32(17, id.len);
  TEST_ASSERT_EQUAL_HEX8(0x03, id.bytes[0]);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(controllerId, &id.bytes[1],
                                sizeof(controllerId));

  const canon_ble::CommandBytes name =
      canon_ble::buildDeviceName("StudioRemote");
  TEST_ASSERT_EQUAL_HEX8(0x04, name.bytes[0]);
  const canon_ble::CommandBytes type = canon_ble::buildAndroidDeviceType();
  const uint8_t expectedType[] = {0x05, 0x02};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expectedType, type.bytes, sizeof(expectedType));
  const uint8_t confirmed[] = {0x02};
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(canon_ble::PairingResponse::Confirmed),
      static_cast<int>(
          canon_ble::parsePairingResponse(confirmed, sizeof(confirmed))));

  const uint8_t postPairCommands[] = {0x06, 0x07, 0x08, 0x0c};
  const uint8_t postPairResponses[] = {0x01, 0x02, 0x03, 0x07};
  for (size_t i = 0; i < sizeof(postPairCommands); ++i) {
    const canon_ble::CommandBytes command =
        canon_ble::buildPostPairCommand(postPairCommands[i]);
    TEST_ASSERT_EQUAL_UINT32(1, command.len);
    TEST_ASSERT_EQUAL_HEX8(postPairCommands[i], command.bytes[0]);
    TEST_ASSERT_TRUE(canon_ble::isPostPairResponse(
        postPairCommands[i], &postPairResponses[i], 1));
  }

  const canon_ble::CommandBytes wake =
      canon_ble::buildModeCommand(canon_ble::kWakeMode);
  const canon_ble::CommandBytes leave =
      canon_ble::buildModeCommand(canon_ble::kLeaveShootingMode);
  const canon_ble::CommandBytes powerOff =
      canon_ble::buildModeCommand(canon_ble::kPowerOffMode);
  TEST_ASSERT_EQUAL_HEX8(0x03, wake.bytes[0]);
  TEST_ASSERT_EQUAL_HEX8(0x04, leave.bytes[0]);
  TEST_ASSERT_EQUAL_HEX8(0x05, powerOff.bytes[0]);

  const uint8_t acknowledged[] = {0x01};
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(canon_ble::ModeEvent::Acknowledged),
      static_cast<int>(
          canon_ble::parseModeEvent(acknowledged, sizeof(acknowledged))));
  const uint8_t sessionReady[] = {0x05};
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(canon_ble::ModeEvent::SessionReady),
      static_cast<int>(
          canon_ble::parseModeEvent(sessionReady, sizeof(sessionReady))));
  const uint8_t shootingReady[] = {0x04};
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(canon_ble::ModeEvent::SessionReady),
      static_cast<int>(
          canon_ble::parseModeEvent(shootingReady, sizeof(shootingReady))));

  const uint8_t expectedStart[] = {0x00, 0x10};
  const uint8_t expectedStop[] = {0x00, 0x11};
  const canon_ble::CommandBytes start = canon_ble::buildRecordCommand(true);
  const canon_ble::CommandBytes stop = canon_ble::buildRecordCommand(false);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expectedStart, start.bytes,
                                sizeof(expectedStart));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expectedStop, stop.bytes, sizeof(expectedStop));
}

void test_canon_state_requires_camera_notifications() {
  canon_ble::CanonBleState state;
  canon_ble::markCommandQueued(state, true);
  TEST_ASSERT_TRUE(state.commandPending);
  TEST_ASSERT_FALSE(state.recordingConfirmed);

  const uint8_t pressed[] = {0x10, 0x10, 0x10};
  canon_ble::reduceRecordNotification(state, pressed, sizeof(pressed));
  TEST_ASSERT_TRUE(state.commandPending);
  TEST_ASSERT_FALSE(state.recordingConfirmed);

  const uint8_t recording[] = {0x01, 0x01, 0x02};
  canon_ble::reduceRecordNotification(state, recording, sizeof(recording));
  TEST_ASSERT_FALSE(state.commandPending);
  TEST_ASSERT_TRUE(state.recordingConfirmed);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(canon_ble::CanonBleState::Recording::Recording),
      static_cast<int>(state.recording));

  const uint8_t stopped[] = {0x01, 0x01, 0x01};
  canon_ble::reduceRecordNotification(state, stopped, sizeof(stopped));
  TEST_ASSERT_TRUE(state.recordingConfirmed);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(canon_ble::CanonBleState::Recording::Stopped),
      static_cast<int>(state.recording));

  canon_ble::markCommandQueued(state, true);
  canon_ble::reduceRecordNotification(state, stopped, sizeof(stopped));
  TEST_ASSERT_FALSE(state.commandPending);
  TEST_ASSERT_TRUE(state.recordingConfirmed);
  TEST_ASSERT_TRUE(state.lastCommandFailed);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(canon_ble::CanonBleState::Recording::Stopped),
      static_cast<int>(state.recording));

  canon_ble::markCommandQueued(state, false);
  canon_ble::reduceRecordNotification(state, recording, sizeof(recording));
  TEST_ASSERT_FALSE(state.commandPending);
  TEST_ASSERT_TRUE(state.recordingConfirmed);
  TEST_ASSERT_TRUE(state.lastCommandFailed);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(canon_ble::CanonBleState::Recording::Recording),
      static_cast<int>(state.recording));
}

void test_tascam_cobs_commands_match_capture() {
  const uint8_t expectedStart[] = {
      0x00, 0x05, 0x44, 0x52, 0x10, 0x41, 0x02, 0x0B, 0x01,
      0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x00,
  };
  const uint8_t expectedStop[] = {
      0x00, 0x05, 0x44, 0x52, 0x10, 0x41, 0x02, 0x08, 0x01,
      0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x00,
  };
  const tascam_x8::FrameBytes start = tascam_x8::buildRecordStart();
  const tascam_x8::FrameBytes stop = tascam_x8::buildRecordStop();
  TEST_ASSERT_EQUAL_UINT32(sizeof(expectedStart), start.len);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expectedStart, start.data(), start.len);
  TEST_ASSERT_EQUAL_UINT32(sizeof(expectedStop), stop.len);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expectedStop, stop.data(), stop.len);
  TEST_ASSERT_EQUAL_UINT32(221, tascam_x8::buildInitialization().len);
  TEST_ASSERT_EQUAL_STRING("2456e1b9-26e2-8f83-e744-f34f01e9d701",
                           tascam_x8::kPrimaryServiceUuid);
}

void test_tascam_scanner_and_confirmed_state() {
  tascam_x8::FrameScanner scanner;
  tascam_x8::TascamX8State state;
  uint8_t payload[18] = {'D', 'R', 0x20, 0x20, 0x24, 0x00};
  tascam_x8::FrameBytes release =
      tascam_x8::encodeFrame(payload, sizeof(payload));
  scanner.feed(release.data(), 4, [&](const tascam_x8::ParsedFrame&) {
    TEST_FAIL_MESSAGE("partial COBS frame");
  });
  scanner.feed(release.data() + 4, release.len - 4,
               [&](const tascam_x8::ParsedFrame& frame) {
                 tascam_x8::reduceFrame(state, frame);
               });
  TEST_ASSERT_FALSE(state.recordingConfirmed);

  payload[5] = 0x01;
  const tascam_x8::FrameBytes started =
      tascam_x8::encodeFrame(payload, sizeof(payload));
  uint8_t combined[tascam_x8::kMaxEncodedFrame * 2] = {};
  combined[0] = 0x72;
  combined[1] = 0x00;
  std::memcpy(combined + 2, started.data(), started.len);
  scanner.feed(combined, started.len + 2,
               [&](const tascam_x8::ParsedFrame& frame) {
                 tascam_x8::reduceFrame(state, frame);
               });
  TEST_ASSERT_TRUE(state.recordingConfirmed);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(tascam_x8::TascamX8State::Recording::Recording),
      static_cast<int>(state.recording));

  std::memset(payload, 0, sizeof(payload));
  payload[0] = 'D';
  payload[1] = 'R';
  payload[2] = 0x10;
  payload[3] = 0x20;
  payload[4] = 0x08;
  const tascam_x8::FrameBytes stopped =
      tascam_x8::encodeFrame(payload, sizeof(payload));
  scanner.feed(stopped.data(), stopped.len,
               [&](const tascam_x8::ParsedFrame& frame) {
                 tascam_x8::reduceFrame(state, frame);
               });
  TEST_ASSERT_TRUE(state.recordingConfirmed);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(tascam_x8::TascamX8State::Recording::Stopped),
      static_cast<int>(state.recording));

  uint8_t statusPayload[18] = {'D', 'R', 0x20, 0x20, 0x00, 0x81};
  const tascam_x8::FrameBytes recordingStatus =
      tascam_x8::encodeFrame(statusPayload, sizeof(statusPayload));
  scanner.feed(recordingStatus.data(), recordingStatus.len,
               [&](const tascam_x8::ParsedFrame& frame) {
                 tascam_x8::reduceFrame(state, frame);
               });
  TEST_ASSERT_TRUE(state.recordingConfirmed);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(tascam_x8::TascamX8State::Recording::Recording),
      static_cast<int>(state.recording));

  statusPayload[5] = 0x82;
  const tascam_x8::FrameBytes transitionalStatus =
      tascam_x8::encodeFrame(statusPayload, sizeof(statusPayload));
  scanner.feed(transitionalStatus.data(), transitionalStatus.len,
               [&](const tascam_x8::ParsedFrame& frame) {
                 tascam_x8::reduceFrame(state, frame);
               });
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(tascam_x8::TascamX8State::Recording::Recording),
      static_cast<int>(state.recording));

  statusPayload[5] = 0x10;
  const tascam_x8::FrameBytes stoppedStatus =
      tascam_x8::encodeFrame(statusPayload, sizeof(statusPayload));
  scanner.feed(stoppedStatus.data(), stoppedStatus.len,
               [&](const tascam_x8::ParsedFrame& frame) {
                 tascam_x8::reduceFrame(state, frame);
               });
  TEST_ASSERT_TRUE(state.recordingConfirmed);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(tascam_x8::TascamX8State::Recording::Stopped),
      static_cast<int>(state.recording));
}

void test_driver_catalog_exposes_shark_and_canon() {
  TEST_ASSERT_EQUAL_UINT32(3, studio::DriverCatalog::count());
  const studio::DriverDescriptor* descriptor =
      studio::DriverCatalog::find(studio::DriverId::SharkNanoII);
  TEST_ASSERT_NOT_NULL(descriptor);
  TEST_ASSERT_EQUAL_STRING("ifootage.shark_nano_ii", descriptor->stableId);
  TEST_ASSERT_EQUAL_UINT8(1, descriptor->maxInstances);
  const studio::DriverDescriptor* canon =
      studio::DriverCatalog::find(studio::DriverId::CanonBle);
  TEST_ASSERT_NOT_NULL(canon);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::DeviceType::Camera),
                        static_cast<int>(canon->type));
  TEST_ASSERT_EQUAL_STRING("canon.eos_r6.smartphone_ble", canon->stableId);
  TEST_ASSERT_BITS_HIGH(
      studio::capabilityBit(studio::Capability::RecordStart) |
          studio::capabilityBit(studio::Capability::RecordStop) |
          studio::capabilityBit(studio::Capability::RecordingState),
      canon->capabilities);
  const studio::DriverDescriptor* tascam =
      studio::DriverCatalog::find(studio::DriverId::TascamX8);
  TEST_ASSERT_NOT_NULL(tascam);
  TEST_ASSERT_EQUAL_STRING("tascam.portacapture_x8", tascam->stableId);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::DeviceType::Recorder),
                        static_cast<int>(tascam->type));
  TEST_ASSERT_BITS_HIGH(studio::capabilityBit(studio::Capability::RecordStart) |
                            studio::capabilityBit(studio::Capability::RecordStop) |
                            studio::capabilityBit(studio::Capability::RecordingState),
                        tascam->capabilities);
  TEST_ASSERT_NULL(studio::DriverCatalog::find(static_cast<studio::DriverId>(99)));
}

void test_registry_crud_and_single_shark_limit() {
  studio::DeviceRegistry registry;
  studio::InstanceId first = studio::kInvalidInstanceId;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(registry.add(studio::DriverId::SharkNanoII, "Main Slider", 1, first)));
  TEST_ASSERT_NOT_EQUAL(studio::kInvalidInstanceId, first);

  studio::InstanceId duplicate = studio::kInvalidInstanceId;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::DuplicateDriver),
      static_cast<int>(
          registry.add(studio::DriverId::SharkNanoII, "Second Slider", 1, duplicate)));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(registry.rename(first, "A-Cam Slider")));
  TEST_ASSERT_EQUAL_STRING("A-Cam Slider", registry.find(first)->displayName);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(registry.setEnabled(first, false)));
  TEST_ASSERT_FALSE(registry.find(first)->enabled);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(registry.remove(first)));
  TEST_ASSERT_EQUAL_UINT32(0, registry.count());
  TEST_ASSERT_TRUE(registry.initialized());
}

void test_config_round_trip_preserves_dormant_records_and_detects_corruption() {
  MemoryBackend backend;
  studio::ConfigStore store(backend);
  studio::DeviceRegistry source;
  studio::DeviceRecord dormant;
  dormant.instanceId = 7;
  dormant.driverId = static_cast<studio::DriverId>(77);
  dormant.enabled = false;
  std::strcpy(dormant.displayName, "Future Device");
  TEST_ASSERT_TRUE(source.restore(&dormant, 1, 8, true));
  TEST_ASSERT_TRUE(store.save(source));

  studio::DeviceRegistry restored;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::ConfigLoadStatus::Loaded),
      static_cast<int>(store.load(restored)));
  TEST_ASSERT_EQUAL_UINT32(1, restored.count());
  TEST_ASSERT_EQUAL_INT(77, static_cast<int>(restored.at(0)->driverId));
  TEST_ASSERT_EQUAL_STRING("Future Device", restored.at(0)->displayName);

  backend.corruptLastByte();
  studio::DeviceRegistry rejected;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::ConfigLoadStatus::Corrupt),
      static_cast<int>(store.load(rejected)));
}

void test_manager_migrates_legacy_without_boot_activation() {
  MemoryBackend backend;
  LegacyBackend legacy;
  legacy.available = true;
  legacy.value.paired = true;
  std::strcpy(legacy.value.address, "11:22:33:44:55:66");
  legacy.value.addressType = 1;
  std::strcpy(legacy.value.advertisedName, "Nano Legacy");
  FakeDriver driver;
  studio::DeviceDriver* drivers[] = {&driver};
  studio::DeviceManager manager(backend, legacy, drivers, 1);

  TEST_ASSERT_TRUE(manager.begin());
  TEST_ASSERT_EQUAL_UINT32(1, manager.count());
  TEST_ASSERT_EQUAL_INT(0, driver.activationCount);
  TEST_ASSERT_EQUAL(studio::kInvalidInstanceId, manager.activeInstance());
  const studio::DeviceRecord* record = manager.at(0);
  TEST_ASSERT_TRUE(record->paired);
  TEST_ASSERT_EQUAL_STRING("11:22:33:44:55:66", record->bleAddress);
}

void test_manager_routes_commands_and_blocks_disabled_device() {
  MemoryBackend backend;
  LegacyBackend legacy;
  FakeDriver driver;
  studio::DeviceDriver* drivers[] = {&driver};
  studio::DeviceManager manager(backend, legacy, drivers, 1);
  TEST_ASSERT_TRUE(manager.begin());
  const studio::InstanceId instanceId = manager.at(0)->instanceId;
  TEST_ASSERT_TRUE(manager.activate(instanceId));
  TEST_ASSERT_EQUAL_INT(1, driver.activationCount);

  studio::DeviceCommand command;
  command.instanceId = instanceId;
  command.type = studio::CommandType::Refresh;
  TEST_ASSERT_TRUE(manager.enqueue(command));
  manager.loop();

  studio::CommandResult result;
  TEST_ASSERT_TRUE(manager.popResult(result));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::CommandStatus::Succeeded),
                        static_cast<int>(result.status));
  TEST_ASSERT_EQUAL_INT(1, driver.dispatchCount);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::CommandType::Refresh),
                        static_cast<int>(driver.lastCommand));

  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(manager.setEnabled(instanceId, false)));
  TEST_ASSERT_FALSE(driver.active);
  TEST_ASSERT_TRUE(manager.enqueue(command));
  manager.loop();
  TEST_ASSERT_TRUE(manager.popResult(result));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::CommandStatus::Disabled),
                        static_cast<int>(result.status));
}

void test_manager_routes_to_canon_driver() {
  MemoryBackend backend;
  LegacyBackend legacy;
  FakeDriver sharkDriver;
  FakeDriver canonDriver(studio::DriverId::CanonBle);
  studio::DeviceDriver* drivers[] = {&sharkDriver, &canonDriver};
  studio::DeviceManager manager(backend, legacy, drivers, 2);
  TEST_ASSERT_TRUE(manager.begin());

  studio::InstanceId canonId = studio::kInvalidInstanceId;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(
          manager.add(studio::DriverId::CanonBle, "R6 Mark III", canonId)));
  TEST_ASSERT_TRUE(manager.activate(canonId));
  TEST_ASSERT_EQUAL_INT(0, sharkDriver.activationCount);
  TEST_ASSERT_EQUAL_INT(1, canonDriver.activationCount);

  studio::DeviceCommand command;
  command.instanceId = canonId;
  command.type = studio::CommandType::RecordStart;
  TEST_ASSERT_TRUE(manager.enqueue(command));
  manager.loop();
  studio::CommandResult result;
  TEST_ASSERT_TRUE(manager.popResult(result));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::CommandStatus::Succeeded),
                        static_cast<int>(result.status));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::CommandType::RecordStart),
      static_cast<int>(canonDriver.lastCommand));
  command.type = studio::CommandType::RecordStop;
  TEST_ASSERT_TRUE(manager.enqueue(command));
  manager.loop();
  TEST_ASSERT_TRUE(manager.popResult(result));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::CommandType::RecordStop),
      static_cast<int>(canonDriver.lastCommand));
  command.type = studio::CommandType::CameraPowerOff;
  TEST_ASSERT_TRUE(manager.enqueue(command));
  manager.loop();
  TEST_ASSERT_TRUE(manager.popResult(result));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::CommandType::CameraPowerOff),
      static_cast<int>(canonDriver.lastCommand));
  command.type = studio::CommandType::CameraPowerOn;
  TEST_ASSERT_TRUE(manager.enqueue(command));
  manager.loop();
  TEST_ASSERT_TRUE(manager.popResult(result));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::CommandType::CameraPowerOn),
      static_cast<int>(canonDriver.lastCommand));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(manager.clearPairing(canonId)));
  TEST_ASSERT_EQUAL_INT(1, canonDriver.forgetPairingCount);
  TEST_ASSERT_EQUAL_INT(0, sharkDriver.forgetPairingCount);
}

void test_manager_routes_explicit_tascam_record_commands() {
  MemoryBackend backend;
  LegacyBackend legacy;
  FakeDriver tascamDriver(studio::DriverId::TascamX8);
  studio::DeviceDriver* drivers[] = {&tascamDriver};
  studio::DeviceManager manager(backend, legacy, drivers, 1);
  TEST_ASSERT_TRUE(manager.begin());

  studio::InstanceId id = studio::kInvalidInstanceId;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(
          manager.add(studio::DriverId::TascamX8, "Recorder", id)));
  TEST_ASSERT_TRUE(manager.activate(id));

  studio::DeviceCommand command;
  command.instanceId = id;
  command.type = studio::CommandType::RecordStart;
  TEST_ASSERT_TRUE(manager.enqueue(command));
  manager.loop();
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::CommandType::RecordStart),
      static_cast<int>(tascamDriver.lastCommand));

  command.type = studio::CommandType::RecordStop;
  TEST_ASSERT_TRUE(manager.enqueue(command));
  manager.loop();
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::CommandType::RecordStop),
      static_cast<int>(tascamDriver.lastCommand));
}

void test_removed_registry_stays_empty_after_restart() {
  MemoryBackend backend;
  LegacyBackend legacy;
  FakeDriver firstDriver;
  studio::DeviceDriver* firstDrivers[] = {&firstDriver};
  studio::DeviceManager first(backend, legacy, firstDrivers, 1);
  TEST_ASSERT_TRUE(first.begin());
  TEST_ASSERT_EQUAL_UINT32(1, first.count());
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(first.remove(first.at(0)->instanceId)));

  FakeDriver secondDriver;
  studio::DeviceDriver* secondDrivers[] = {&secondDriver};
  studio::DeviceManager second(backend, legacy, secondDrivers, 1);
  TEST_ASSERT_TRUE(second.begin());
  TEST_ASSERT_EQUAL_UINT32(0, second.count());
  TEST_ASSERT_EQUAL_INT(0, secondDriver.activationCount);
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
  RUN_TEST(test_canon_smartphone_handshake_and_record_protocol);
  RUN_TEST(test_canon_state_requires_camera_notifications);
  RUN_TEST(test_tascam_cobs_commands_match_capture);
  RUN_TEST(test_tascam_scanner_and_confirmed_state);
  RUN_TEST(test_driver_catalog_exposes_shark_and_canon);
  RUN_TEST(test_registry_crud_and_single_shark_limit);
  RUN_TEST(test_config_round_trip_preserves_dormant_records_and_detects_corruption);
  RUN_TEST(test_manager_migrates_legacy_without_boot_activation);
  RUN_TEST(test_manager_routes_commands_and_blocks_disabled_device);
  RUN_TEST(test_manager_routes_to_canon_driver);
  RUN_TEST(test_manager_routes_explicit_tascam_record_commands);
  RUN_TEST(test_removed_registry_stays_empty_after_restart);
  return UNITY_END();
}
