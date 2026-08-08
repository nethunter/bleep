#include <unity.h>

#include <cmath>
#include <cstring>

#include "core/ble/ble_central.h"
#include "core/ble/fake_ble_backend.h"
#include "core/config_store.h"
#include "core/device_driver.h"
#include "core/device_manager.h"
#include "core/driver_catalog.h"
#include "core/home_assistant_config.h"
#include "core/panel_settings.h"
#include "core/scene_service.h"
#include "core/scene_store.h"
#include "devices/canon_ble/ble_match.h"
#include "devices/canon_ble/protocol.h"
#include "devices/canon_ble/state.h"
#include "devices/canon_trigger/ble_match.h"
#include "devices/canon_trigger/protocol.h"
#include "devices/canon_trigger/state.h"
#include "devices/shark_nano_ii/ble_match.h"
#include "devices/shark_nano_ii/protocol.h"
#include "devices/shark_nano_ii/state.h"
#include "devices/tascam_x8/ble_match.h"
#include "devices/tascam_x8/protocol.h"
#include "devices/tascam_x8/state.h"
#include "devices/home_assistant/protocol.h"
#include "devices/amaran_light/crypto.h"
#include "devices/amaran_light/protocol.h"
#include "devices/amaran_light/state.h"
#include "devices/amaran_light/store.h"
#include "devices/zhiyun_x100/ble_match.h"
#include "devices/zhiyun_x100/protocol.h"
#include "devices/zhiyun_x100/state.h"
#include "core/mesh/provisioning_policy.h"

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
    if (failWrites) {
      return false;
    }
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

  bool containsText(const char* text) const {
    const size_t textLength = std::strlen(text);
    if (textLength == 0 || textLength > length_) return false;
    for (size_t i = 0; i + textLength <= length_; ++i) {
      if (std::memcmp(data_ + i, text, textLength) == 0) return true;
    }
    return false;
  }

  bool failWrites = false;

 private:
  uint8_t data_[studio::ConfigStore::kMaxBlobSize] = {};
  size_t length_ = 0;
};

class V1MeshBackend : public studio::IConfigBackend {
 public:
  V1MeshBackend() {
    uint8_t* out = data_;
    const uint8_t magic[] = {'A', 'M', 'S', 'H'};
    std::memcpy(out, magic, sizeof(magic));
    out += sizeof(magic);
    put16(out, 1);
    *out++ = 2;
    *out++ = 0;
    *out++ = 1;
    std::memset(out, 0, 32);
    out += 32;
    put32(out, 0);
    put16(out, 1);
    put16(out, 0xc000);
    put16(out, 4);
    put32(out, 256);
    putNode(out, 41, 2);
    putNode(out, 42, 3);
    length_ = static_cast<size_t>(out - data_) + 4;
    put32(out, checksum(data_, length_ - 4));
  }

  size_t read(uint8_t* destination, size_t capacity) override {
    if (length_ > capacity) return 0;
    std::memcpy(destination, data_, length_);
    return length_;
  }

  bool write(const uint8_t*, size_t) override { return false; }

 private:
  static void put16(uint8_t*& out, uint16_t value) {
    *out++ = static_cast<uint8_t>(value);
    *out++ = static_cast<uint8_t>(value >> 8);
  }
  static void put32(uint8_t*& out, uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8) {
      *out++ = static_cast<uint8_t>(value >> shift);
    }
  }
  static uint32_t checksum(const uint8_t* bytes, size_t length) {
    uint32_t value = 2166136261u;
    for (size_t i = 0; i < length; ++i) {
      value = (value ^ bytes[i]) * 16777619u;
    }
    return value;
  }
  static void putNode(uint8_t*& out, studio::InstanceId instanceId,
                      uint16_t unicastAddress) {
    put32(out, instanceId);
    put16(out, static_cast<uint16_t>(studio::DriverId::ZhiyunLight));
    put16(out, unicastAddress);
    *out++ = 1;
    *out++ = 1;
    std::memset(out, 0, 32 + studio::kBleAddressCapacity);
    out += 32 + studio::kBleAddressCapacity;
    *out++ = 0;
  }

  uint8_t data_[256] = {};
  size_t length_ = 0;
};

class V1DeviceBackend : public studio::IConfigBackend {
 public:
  V1DeviceBackend() {
    uint8_t* out = data_;
    const uint8_t magic[] = {'S', 'T', 'D', 'V'};
    std::memcpy(out, magic, sizeof(magic));
    out += sizeof(magic);
    putU16(out, 1);
    *out++ = 1;
    *out++ = 1;
    putU32(out, 43);
    putU32(out, 42);
    putU16(out, static_cast<uint16_t>(studio::DriverId::CanonBle));
    *out++ = 0x03;
    *out++ = 1;
    copyFixed(out, studio::kDeviceNameCapacity, "Legacy camera");
    copyFixed(out, studio::kBleAddressCapacity, "11:22:33:44:55:66");
    copyFixed(out, studio::kBleNameCapacity, "EOS legacy");
    const uint32_t crc = checksum(data_, static_cast<size_t>(out - data_));
    putU32(out, crc);
    length_ = static_cast<size_t>(out - data_);
  }

  size_t read(uint8_t* destination, size_t capacity) override {
    if (capacity < length_) return 0;
    std::memcpy(destination, data_, length_);
    return length_;
  }
  bool write(const uint8_t*, size_t) override { return false; }

 private:
  static void putU16(uint8_t*& out, uint16_t value) {
    *out++ = static_cast<uint8_t>(value);
    *out++ = static_cast<uint8_t>(value >> 8);
  }
  static void putU32(uint8_t*& out, uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8) {
      *out++ = static_cast<uint8_t>(value >> shift);
    }
  }
  static void copyFixed(uint8_t*& out, size_t capacity, const char* value) {
    std::memset(out, 0, capacity);
    std::strncpy(reinterpret_cast<char*>(out), value, capacity - 1);
    out += capacity;
  }
  static uint32_t checksum(const uint8_t* bytes, size_t length) {
    uint32_t value = 2166136261u;
    for (size_t i = 0; i < length; ++i) {
      value = (value ^ bytes[i]) * 16777619u;
    }
    return value;
  }

  uint8_t data_[128] = {};
  size_t length_ = 0;
};

class V1SceneBackend : public studio::IConfigBackend {
 public:
  V1SceneBackend() {
    uint8_t* out = data_;
    std::memcpy(out, "SCN1", 4); out += 4;
    put32(out, 0x01010001);  // version 1, initialized, one scene
    put32(out, 2);           // next scene id
    put32(out, 1);           // scene id
    *out++ = 1;
    std::memset(out, 0, studio::kDeviceNameCapacity);
    std::memcpy(out, "Legacy scene", 12); out += studio::kDeviceNameCapacity;
    *out++ = 1; *out++ = 0;
    for (size_t list = 0; list < 2; ++list) {
      for (size_t step = 0; step < CONFIG_MAX_SCENE_STEPS; ++step) {
        *out++ = static_cast<uint8_t>(step == 0 && list == 0
            ? studio::SceneStepType::Action : studio::SceneStepType::Wait);
        put32(out, step == 0 && list == 0 ? 7 : 0);
        *out++ = static_cast<uint8_t>(step == 0 && list == 0
            ? studio::CommandType::TurnOn : studio::CommandType::Refresh);
        put32(out, 0);
      }
    }
    put32(out, checksum(data_, static_cast<size_t>(out - data_)));
    length_ = static_cast<size_t>(out - data_);
  }
  size_t read(uint8_t* destination, size_t capacity) override {
    if (capacity < length_) return 0;
    std::memcpy(destination, data_, length_); return length_;
  }
  bool write(const uint8_t*, size_t) override { return false; }

 private:
  static void put32(uint8_t*& out, uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8) *out++ = value >> shift;
  }
  static uint32_t checksum(const uint8_t* bytes, size_t length) {
    uint32_t value = 2166136261u;
    for (size_t i = 0; i < length; ++i) value = (value ^ bytes[i]) * 16777619u;
    return value;
  }
  uint8_t data_[512] = {};
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
  studio::BleSlotKey bleSlotKey(
      const studio::DeviceRecord& record) const override {
    if (noBleSlot || id_ == studio::DriverId::HomeAssistant) return {};
    return {sharedBleFamily != studio::DriverId::Unknown ? sharedBleFamily
                                                         : id_,
            sharedBleGroup != 0 ? sharedBleGroup : record.instanceId};
  }
  bool activate(const studio::DeviceRecord& record) override {
    for (studio::InstanceId id : activeInstances) {
      if (id == record.instanceId) {
        return true;
      }
    }
    for (studio::InstanceId& id : activeInstances) {
      if (id != studio::kInvalidInstanceId) {
        continue;
      }
      id = record.instanceId;
      active = true;
      activeInstance = record.instanceId;
      ++activationCount;
      if (activationSequence != nullptr && firstActivationOrder == 0) {
        firstActivationOrder = ++(*activationSequence);
      }
      return true;
    }
    return false;
  }
  bool resume(const studio::DeviceRecord& record) override {
    ++resumeCount;
    lastResumedInstance = record.instanceId;
    return resumeSucceeds;
  }
  bool retainWhileDisconnected(studio::InstanceId) const override {
    return intentionalOffline;
  }
  void deactivate(studio::InstanceId instanceId) override {
    for (studio::InstanceId& id : activeInstances) {
      if (id != instanceId) {
        continue;
      }
      id = studio::kInvalidInstanceId;
      ++deactivationCount;
      break;
    }
    active = false;
    activeInstance = studio::kInvalidInstanceId;
    for (studio::InstanceId id : activeInstances) {
      if (id != studio::kInvalidInstanceId) {
        active = true;
        activeInstance = id;
        break;
      }
    }
  }
  void loop() override { ++loopCount; }
  studio::CommandStatus dispatch(const studio::DeviceCommand& command) override {
    lastCommand = command.type;
    ++dispatchCount;
    if (failCommandCount > 0 && command.type == failCommand) {
      --failCommandCount;
      return studio::CommandStatus::Unavailable;
    }
    return studio::CommandStatus::Succeeded;
  }
  studio::DeviceRuntimeState runtimeState(studio::InstanceId instanceId) const override {
    studio::DeviceRuntimeState state;
    bool found = false;
    for (studio::InstanceId id : activeInstances) {
      found = found || id == instanceId;
    }
    state.link = found && !forceDisconnected ? studio::LinkState::Connected
                                             : studio::LinkState::Disconnected;
    state.protocolReady = found && ready && !forceDisconnected;
    state.commandPending = commandPending;
    state.recordingConfirmed = recordingConfirmed;
    state.recording = recording;
    return state;
  }
  const void* specializedState(studio::InstanceId) const override { return nullptr; }
  void forgetPairing(const studio::DeviceRecord&) override {
    ++forgetPairingCount;
  }
  void cancelOnboarding(const studio::DeviceRecord&) override {
    ++cancelOnboardingCount;
  }
  bool consumePairingUpdate(studio::InstanceId,
                            studio::DeviceRecord& record) override {
    if (!pairingUpdatePending) {
      return false;
    }
    pairingUpdatePending = false;
    record.paired = pairingValue;
    if (pairingValue) {
      std::strcpy(record.bleAddress, "11:22:33:44:55:66");
      std::strcpy(record.bleName, "Test device");
    }
    return true;
  }

  bool active = false;
  bool ready = true;
  bool forceDisconnected = false;
  bool intentionalOffline = false;
  bool noBleSlot = false;
  uint32_t sharedBleGroup = 0;
  studio::DriverId sharedBleFamily = studio::DriverId::Unknown;
  bool commandPending = false;
  bool recordingConfirmed = false;
  bool recording = false;
  studio::InstanceId activeInstance = studio::kInvalidInstanceId;
  studio::InstanceId activeInstances[studio::DeviceManager::kMaxActiveLinks] = {};
  int activationCount = 0;
  int resumeCount = 0;
  bool resumeSucceeds = true;
  studio::InstanceId lastResumedInstance = studio::kInvalidInstanceId;
  int* activationSequence = nullptr;
  int firstActivationOrder = 0;
  int deactivationCount = 0;
  int loopCount = 0;
  int dispatchCount = 0;
  studio::CommandType failCommand = studio::CommandType::Refresh;
  int failCommandCount = 0;
  int forgetPairingCount = 0;
  int cancelOnboardingCount = 0;
  bool pairingUpdatePending = false;
  bool pairingValue = true;
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
      canon_ble::buildHandshakeRequest("Ble(e)p");
  TEST_ASSERT_EQUAL_UINT32(8, request.len);
  TEST_ASSERT_EQUAL_HEX8(0x01, request.bytes[0]);
  TEST_ASSERT_EQUAL_UINT8_ARRAY("Ble(e)p", &request.bytes[1], 7);

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
      canon_ble::buildDeviceName("Ble(e)p");
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

void test_canon_trigger_pairing_name_and_trigger_bytes() {
  TEST_ASSERT_EQUAL_STRING("00050000-0000-1000-0000-d8492fffa821",
                           canon_trigger::kPrimaryServiceUuid);
  TEST_ASSERT_EQUAL_HEX8(0x88, canon_trigger::kRecordTriggerPress);
  TEST_ASSERT_EQUAL_HEX8(0x08, canon_trigger::kRecordTriggerRelease);
  TEST_ASSERT_EQUAL_UINT32(200, canon_trigger::kTriggerHoldMs);

  const canon_trigger::PairingName name =
      canon_trigger::buildPairingName("Studio Panel");
  TEST_ASSERT_EQUAL_UINT32(13, name.len);
  TEST_ASSERT_EQUAL_HEX8(0x03, name.bytes[0]);
  TEST_ASSERT_EQUAL_STRING("Studio Panel",
                           reinterpret_cast<const char*>(&name.bytes[1]));
}

void test_canon_trigger_state_tracks_trigger_outcome() {
  canon_trigger::CanonTriggerState state;
  canon_trigger::markTriggerQueued(state);
  TEST_ASSERT_TRUE(state.triggerPending);
  TEST_ASSERT_EQUAL_UINT32(0, state.triggerCount);

  canon_trigger::markTriggerComplete(state, true);
  TEST_ASSERT_FALSE(state.triggerPending);
  TEST_ASSERT_TRUE(state.lastTriggerSucceeded);
  TEST_ASSERT_EQUAL_UINT32(1, state.triggerCount);

  canon_trigger::markTriggerQueued(state);
  canon_trigger::markTriggerComplete(state, false);
  TEST_ASSERT_FALSE(state.triggerPending);
  TEST_ASSERT_FALSE(state.lastTriggerSucceeded);
  TEST_ASSERT_EQUAL_UINT32(1, state.triggerCount);
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
  TEST_ASSERT_TRUE(canon_ble::completeStopIfAlreadyStopped(state));
  TEST_ASSERT_FALSE(state.lastCommandFailed);

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
  state.lastCommandFailed = true;
  TEST_ASSERT_TRUE(tascam_x8::completeStopIfAlreadyStopped(state));
  TEST_ASSERT_FALSE(state.lastCommandFailed);
}

void test_driver_catalog_exposes_shark_and_canon() {
  TEST_ASSERT_EQUAL_UINT32(9, studio::DriverCatalog::count());
  const studio::DriverDescriptor* descriptor =
      studio::DriverCatalog::find(studio::DriverId::SharkNanoII);
  TEST_ASSERT_NOT_NULL(descriptor);
  TEST_ASSERT_EQUAL_STRING("ifootage.shark_nano_ii", descriptor->stableId);
  TEST_ASSERT_EQUAL_UINT8(1, descriptor->maxInstances);
  const studio::DriverDescriptor* trigger =
      studio::DriverCatalog::find(studio::DriverId::CanonTrigger);
  TEST_ASSERT_NOT_NULL(trigger);
  TEST_ASSERT_EQUAL_STRING("Canon (Trigger)", trigger->model);
  TEST_ASSERT_EQUAL_STRING("canon.eos_r6.trigger", trigger->stableId);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::DeviceType::Camera),
                        static_cast<int>(trigger->type));
  TEST_ASSERT_BITS_HIGH(
      studio::capabilityBit(studio::Capability::Link) |
          studio::capabilityBit(studio::Capability::RecordTrigger),
      trigger->capabilities);
  const studio::DriverDescriptor* canon =
      studio::DriverCatalog::find(studio::DriverId::CanonBle);
  TEST_ASSERT_NOT_NULL(canon);
  TEST_ASSERT_EQUAL_STRING("Canon (Smart)", canon->model);
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
  const studio::DriverDescriptor* homeAssistant =
      studio::DriverCatalog::find(studio::DriverId::HomeAssistant);
  TEST_ASSERT_NOT_NULL(homeAssistant);
  TEST_ASSERT_EQUAL_UINT8(4, homeAssistant->maxInstances);
  const studio::DriverDescriptor* amaran =
      studio::DriverCatalog::find(studio::DriverId::AmaranLight);
  TEST_ASSERT_NOT_NULL(amaran);
  TEST_ASSERT_EQUAL_STRING("Amaran Light", amaran->model);
  TEST_ASSERT_TRUE(amaran->discoverable);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::DeviceType::Light),
                        static_cast<int>(amaran->type));
  TEST_ASSERT_BITS_HIGH(
      studio::capabilityBit(studio::Capability::SetLightCct) |
          studio::capabilityBit(studio::Capability::SetLightRgb),
      amaran->capabilities);
  TEST_ASSERT_FALSE(studio::DriverCatalog::find(
      studio::DriverId::AmaranPano120c)->discoverable);
  TEST_ASSERT_FALSE(studio::DriverCatalog::find(
      studio::DriverId::AmaranAce25c)->discoverable);
  const studio::DriverDescriptor* zhiyun =
      studio::DriverCatalog::find(studio::DriverId::ZhiyunLight);
  TEST_ASSERT_NOT_NULL(zhiyun);
  TEST_ASSERT_EQUAL_STRING("zhiyun.light", zhiyun->stableId);
  TEST_ASSERT_EQUAL_STRING("Zhiyun Light", zhiyun->model);
  TEST_ASSERT_EQUAL_UINT8(4, zhiyun->maxInstances);
  TEST_ASSERT_BITS_HIGH(
      studio::capabilityBit(studio::Capability::TurnOn) |
          studio::capabilityBit(studio::Capability::TurnOff) |
          studio::capabilityBit(studio::Capability::SetLightCct) |
          studio::capabilityBit(studio::Capability::SetLightRgb),
      zhiyun->capabilities);
  TEST_ASSERT_BITS_LOW(
      studio::capabilityBit(studio::Capability::SetLightTint),
      zhiyun->capabilities);
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

void test_transactional_add_commits_only_after_pairing_and_readiness() {
  MemoryBackend backend;
  LegacyBackend legacy;
  FakeDriver driver(studio::DriverId::CanonBle);
  studio::DeviceDriver* drivers[] = {&driver};
  studio::DeviceManager devices(backend, legacy, drivers, 1);
  TEST_ASSERT_TRUE(devices.begin());
  const size_t initialCount = devices.count();

  studio::InstanceId id = studio::kInvalidInstanceId;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(devices.beginAdd(studio::DriverId::CanonBle,
                                        "Pending Camera", id)));
  TEST_ASSERT_TRUE(devices.isPendingAdd(id));
  TEST_ASSERT_EQUAL_UINT32(initialCount, devices.count());
  TEST_ASSERT_NOT_NULL(devices.find(id));
  TEST_ASSERT_FALSE(backend.containsText("Pending Camera"));
  TEST_ASSERT_TRUE(devices.acquire(id, studio::ConnectionOwner::Foreground));

  driver.ready = false;
  driver.pairingUpdatePending = true;
  devices.loop();
  TEST_ASSERT_TRUE(devices.find(id)->paired);
  TEST_ASSERT_TRUE(devices.isPendingAdd(id));
  TEST_ASSERT_EQUAL_UINT32(initialCount, devices.count());
  TEST_ASSERT_FALSE(backend.containsText("Pending Camera"));

  driver.ready = true;
  devices.loop();
  TEST_ASSERT_FALSE(devices.isPendingAdd(id));
  TEST_ASSERT_EQUAL_UINT32(initialCount + 1, devices.count());
  TEST_ASSERT_TRUE(backend.containsText("Pending Camera"));
  TEST_ASSERT_TRUE(devices.ownedBy(id, studio::ConnectionOwner::Foreground));
}

void test_transactional_add_cancel_and_failed_save_do_not_register_device() {
  MemoryBackend backend;
  LegacyBackend legacy;
  FakeDriver driver(studio::DriverId::CanonBle);
  studio::DeviceDriver* drivers[] = {&driver};
  studio::DeviceManager devices(backend, legacy, drivers, 1);
  TEST_ASSERT_TRUE(devices.begin());
  const size_t initialCount = devices.count();

  studio::InstanceId id = studio::kInvalidInstanceId;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(devices.beginAdd(studio::DriverId::CanonBle,
                                        "Canceled Camera", id)));
  TEST_ASSERT_TRUE(devices.acquire(id, studio::ConnectionOwner::Foreground));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Invalid),
      static_cast<int>(devices.beginAdd(studio::DriverId::CanonBle,
                                        "Second Draft", id)));
  const studio::InstanceId canceledId = devices.pendingAdd();
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(devices.cancelPendingAdd(canceledId)));
  TEST_ASSERT_EQUAL_INT(1, driver.cancelOnboardingCount);
  TEST_ASSERT_FALSE(devices.isActive(canceledId));
  TEST_ASSERT_EQUAL_UINT32(initialCount, devices.count());
  TEST_ASSERT_FALSE(backend.containsText("Canceled Camera"));

  studio::DeviceManager restarted(backend, legacy, drivers, 1);
  TEST_ASSERT_TRUE(restarted.begin());
  TEST_ASSERT_EQUAL_UINT32(initialCount, restarted.count());

  studio::InstanceId failedId = studio::kInvalidInstanceId;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(devices.beginAdd(studio::DriverId::CanonBle,
                                        "Save Retry Camera", failedId)));
  TEST_ASSERT_TRUE(
      devices.acquire(failedId, studio::ConnectionOwner::Foreground));
  backend.failWrites = true;
  driver.ready = true;
  driver.pairingUpdatePending = true;
  devices.loop();
  TEST_ASSERT_TRUE(devices.pendingAddCommitFailed(failedId));
  TEST_ASSERT_EQUAL_UINT32(initialCount, devices.count());
  TEST_ASSERT_FALSE(backend.containsText("Save Retry Camera"));

  backend.failWrites = false;
  TEST_ASSERT_TRUE(devices.retryPendingAdd(failedId));
  TEST_ASSERT_FALSE(devices.isPendingAdd(failedId));
  TEST_ASSERT_EQUAL_UINT32(initialCount + 1, devices.count());
  TEST_ASSERT_TRUE(backend.containsText("Save Retry Camera"));
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

void test_home_assistant_config_is_separate_checksummed_and_local_only() {
  MemoryBackend deviceBackend;
  MemoryBackend secretBackend;
  studio::ConfigStore deviceStore(deviceBackend);
  studio::HomeAssistantConfigStore secretStore(secretBackend);
  studio::DeviceRegistry registry;
  studio::InstanceId id = studio::kInvalidInstanceId;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(registry.add(studio::DriverId::HomeAssistant,
                                    "Key Light", 4, id)));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(registry.configureHomeAssistant(
          id, studio::HomeAssistantDomain::Light, "light.key_light")));
  TEST_ASSERT_TRUE(deviceStore.save(registry));

  studio::HomeAssistantConfig config;
  config.configured = true;
  std::strcpy(config.wifiSsid, "StudioNet");
  std::strcpy(config.wifiPassword, "wifi-secret");
  std::strcpy(config.baseUrl, "http://homeassistant.local:8123");
  std::strcpy(config.token, "long-lived-secret-token");
  TEST_ASSERT_TRUE(secretStore.save(config));
  TEST_ASSERT_FALSE(deviceBackend.containsText("wifi-secret"));
  TEST_ASSERT_FALSE(deviceBackend.containsText("long-lived-secret-token"));

  studio::HomeAssistantConfig restored;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::ConfigLoadStatus::Loaded),
      static_cast<int>(secretStore.load(restored)));
  TEST_ASSERT_EQUAL_STRING(config.wifiSsid, restored.wifiSsid);
  TEST_ASSERT_EQUAL_STRING(config.baseUrl, restored.baseUrl);
  TEST_ASSERT_EQUAL_STRING(config.token, restored.token);
  secretBackend.corruptLastByte();
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::ConfigLoadStatus::Corrupt),
      static_cast<int>(secretStore.load(restored)));
  TEST_ASSERT_FALSE(studio::validLocalHomeAssistantUrl("https://ha.local"));
  TEST_ASSERT_FALSE(studio::validLocalHomeAssistantUrl("ws://ha.local"));
  TEST_ASSERT_TRUE(studio::validLocalHomeAssistantUrl("http://10.0.0.5:8123"));
}

void test_panel_settings_default_round_trip_corruption_and_rollback() {
  MemoryBackend backend;
  studio::PanelSettingsStore store(backend);
  studio::PanelSettings settings;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::ConfigLoadStatus::Missing),
      static_cast<int>(store.load(settings)));
  TEST_ASSERT_TRUE(settings.hapticEnabled);

  studio::PanelSettingsService service(backend);
  TEST_ASSERT_TRUE(service.begin());
  TEST_ASSERT_TRUE(service.get().hapticEnabled);
  TEST_ASSERT_TRUE(service.setHapticEnabled(false));
  TEST_ASSERT_FALSE(service.get().hapticEnabled);

  studio::PanelSettings restored;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::ConfigLoadStatus::Loaded),
      static_cast<int>(store.load(restored)));
  TEST_ASSERT_FALSE(restored.hapticEnabled);

  backend.failWrites = true;
  TEST_ASSERT_FALSE(service.setHapticEnabled(true));
  TEST_ASSERT_FALSE(service.get().hapticEnabled);
  backend.failWrites = false;
  backend.corruptLastByte();
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::ConfigLoadStatus::Corrupt),
      static_cast<int>(store.load(restored)));
  TEST_ASSERT_TRUE(restored.hapticEnabled);
}

void test_v1_device_blob_migrates_without_changing_ble_identity() {
  V1DeviceBackend backend;
  studio::ConfigStore store(backend);
  studio::DeviceRegistry registry;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::ConfigLoadStatus::Loaded),
      static_cast<int>(store.load(registry)));
  TEST_ASSERT_EQUAL_UINT32(1, registry.count());
  const studio::DeviceRecord* record = registry.at(0);
  TEST_ASSERT_NOT_NULL(record);
  TEST_ASSERT_EQUAL_UINT32(42, record->instanceId);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::DriverId::CanonBle),
                        static_cast<int>(record->driverId));
  TEST_ASSERT_TRUE(record->enabled);
  TEST_ASSERT_TRUE(record->paired);
  TEST_ASSERT_EQUAL_STRING("Legacy camera", record->displayName);
  TEST_ASSERT_EQUAL_STRING("11:22:33:44:55:66", record->bleAddress);
  TEST_ASSERT_EQUAL_UINT8(1, record->bleAddressType);
  TEST_ASSERT_EQUAL_STRING("EOS legacy", record->bleName);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::HomeAssistantDomain::None),
                        static_cast<int>(record->homeAssistantDomain));
  TEST_ASSERT_EQUAL_STRING("", record->homeAssistantEntityId);
}

void test_home_assistant_profiles_protocol_capacity_and_scene_validation() {
  MemoryBackend deviceBackend;
  MemoryBackend sceneBackend;
  LegacyBackend legacy;
  FakeDriver haDriver(studio::DriverId::HomeAssistant);
  studio::DeviceDriver* drivers[] = {&haDriver};
  studio::DeviceManager devices(deviceBackend, legacy, drivers, 1);
  TEST_ASSERT_TRUE(devices.begin());

  const studio::HomeAssistantDomain domains[] = {
      studio::HomeAssistantDomain::Light,
      studio::HomeAssistantDomain::InputBoolean,
      studio::HomeAssistantDomain::Button,
      studio::HomeAssistantDomain::Scene};
  const char* ids[] = {"light.key", "input_boolean.live", "button.slate",
                       "scene.ready"};
  studio::InstanceId entityIds[4] = {};
  for (size_t i = 0; i < 4; ++i) {
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(studio::RegistryStatus::Ok),
        static_cast<int>(devices.addHomeAssistantEntity(
            domains[i], ids[i], ids[i], entityIds[i])));
  }
  studio::InstanceId overflow = studio::kInvalidInstanceId;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::DuplicateDriver),
      static_cast<int>(devices.addHomeAssistantEntity(
          studio::HomeAssistantDomain::Script, "script.extra", "Extra",
          overflow)));

  const studio::InstanceProfile light = devices.profile(entityIds[0]);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::DeviceType::Light),
                        static_cast<int>(light.type));
  TEST_ASSERT_BITS_HIGH(
      studio::capabilityBit(studio::Capability::TurnOn) |
          studio::capabilityBit(studio::Capability::TurnOff),
      light.capabilities);
  const studio::InstanceProfile inputBoolean = devices.profile(entityIds[1]);
  TEST_ASSERT_BITS_HIGH(
      studio::capabilityBit(studio::Capability::TurnOn) |
          studio::capabilityBit(studio::Capability::TurnOff),
      inputBoolean.capabilities);
  const studio::InstanceProfile button = devices.profile(entityIds[2]);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::DeviceType::Action),
                        static_cast<int>(button.type));
  TEST_ASSERT_BITS_HIGH(studio::capabilityBit(studio::Capability::Press),
                        button.capabilities);
  TEST_ASSERT_BITS_LOW(studio::capabilityBit(studio::Capability::Activate),
                       button.capabilities);

  TEST_ASSERT_TRUE(home_assistant::supportedEntityId("input_boolean.live"));
  TEST_ASSERT_FALSE(home_assistant::supportedEntityId("sensor.temperature"));
  TEST_ASSERT_TRUE(home_assistant::commandSupported(
      studio::HomeAssistantDomain::Scene, studio::CommandType::Activate));
  TEST_ASSERT_FALSE(home_assistant::commandSupported(
      studio::HomeAssistantDomain::Scene, studio::CommandType::TurnOn));
  char payload[256];
  TEST_ASSERT_TRUE(home_assistant::buildAuth(payload, sizeof(payload), "token"));
  TEST_ASSERT_EQUAL_STRING(
      "{\"type\":\"auth\",\"access_token\":\"token\"}", payload);
  TEST_ASSERT_TRUE(home_assistant::buildServiceCall(
      payload, sizeof(payload), 17, studio::HomeAssistantDomain::Button,
      studio::CommandType::Press, "button.slate"));
  TEST_ASSERT_NOT_NULL(std::strstr(payload, "\"service\":\"press\""));
  TEST_ASSERT_NOT_NULL(std::strstr(payload, "\"entity_id\":\"button.slate\""));
  TEST_ASSERT_TRUE(home_assistant::buildServiceCall(
      payload, sizeof(payload), 18, studio::HomeAssistantDomain::InputBoolean,
      studio::CommandType::TurnOn, "input_boolean.live"));
  TEST_ASSERT_NOT_NULL(std::strstr(payload, "\"domain\":\"input_boolean\""));
  TEST_ASSERT_NOT_NULL(std::strstr(payload, "\"service\":\"turn_on\""));
  TEST_ASSERT_TRUE(home_assistant::buildServiceCall(
      payload, sizeof(payload), 19, studio::HomeAssistantDomain::InputBoolean,
      studio::CommandType::TurnOff, "input_boolean.live"));
  TEST_ASSERT_NOT_NULL(std::strstr(payload, "\"service\":\"turn_off\""));

  studio::SceneService scenes(sceneBackend, devices);
  TEST_ASSERT_TRUE(scenes.begin());
  studio::SceneRecord scene;
  std::strcpy(scene.name, "HA actions");
  scene.startCount = 3;
  scene.startSteps[0] =
      studio::makeActionStep(entityIds[0], studio::CommandType::TurnOn);
  scene.startSteps[1] =
      studio::makeActionStep(entityIds[2], studio::CommandType::Press);
  scene.startSteps[2] =
      studio::makeActionStep(entityIds[1], studio::CommandType::TurnOn);
  scene.stopCount = 2;
  scene.stopSteps[0] =
      studio::makeActionStep(entityIds[0], studio::CommandType::TurnOff);
  scene.stopSteps[1] =
      studio::makeActionStep(entityIds[1], studio::CommandType::TurnOff);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::SceneValidationStatus::Ok),
                        static_cast<int>(scenes.validate(scene)));
  scene.startSteps[1] =
      studio::makeActionStep(entityIds[3], studio::CommandType::TurnOn);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::SceneValidationStatus::MissingCapability),
      static_cast<int>(scenes.validate(scene)));
}

void test_mixed_scene_activates_physical_transport_before_home_assistant() {
  MemoryBackend deviceBackend;
  MemoryBackend sceneBackend;
  LegacyBackend legacy;
  FakeDriver physicalDriver(studio::DriverId::CanonBle);
  FakeDriver homeAssistantDriver(studio::DriverId::HomeAssistant);
  int activationSequence = 0;
  physicalDriver.activationSequence = &activationSequence;
  homeAssistantDriver.activationSequence = &activationSequence;
  studio::DeviceDriver* drivers[] = {&physicalDriver, &homeAssistantDriver};
  studio::DeviceManager devices(deviceBackend, legacy, drivers, 2);
  TEST_ASSERT_TRUE(devices.begin());

  studio::InstanceId cameraId = studio::kInvalidInstanceId;
  studio::InstanceId helperId = studio::kInvalidInstanceId;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(devices.add(studio::DriverId::CanonBle, "Camera",
                                   cameraId)));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(devices.addHomeAssistantEntity(
          studio::HomeAssistantDomain::InputBoolean, "input_boolean.live",
          "Live", helperId)));

  studio::SceneService scenes(sceneBackend, devices);
  TEST_ASSERT_TRUE(scenes.begin());
  studio::SceneId sceneId = studio::kInvalidSceneId;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::SceneRegistryStatus::Ok),
      static_cast<int>(scenes.add("Mixed order", sceneId)));
  studio::SceneRecord record = *scenes.find(sceneId);
  record.startCount = 2;
  // Authored HA-first order must not dictate transport initialization order.
  record.startSteps[0] =
      studio::makeActionStep(helperId, studio::CommandType::TurnOn);
  record.startSteps[1] =
      studio::makeActionStep(cameraId, studio::CommandType::RecordStart);
  record.stopCount = 2;
  record.stopSteps[0] =
      studio::makeActionStep(helperId, studio::CommandType::TurnOff);
  record.stopSteps[1] =
      studio::makeActionStep(cameraId, studio::CommandType::RecordStop);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::SceneRegistryStatus::Ok),
      static_cast<int>(scenes.replace(record)));

  // Reproduce navigation from a retained HA entity screen into the sequence.
  TEST_ASSERT_TRUE(
      devices.acquire(helperId, studio::ConnectionOwner::Foreground));
  devices.release(helperId, studio::ConnectionOwner::Foreground);
  TEST_ASSERT_TRUE(devices.isActive(helperId));
  activationSequence = 0;
  physicalDriver.firstActivationOrder = 0;
  homeAssistantDriver.firstActivationOrder = 0;

  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::SceneRunStatus::Ok),
                        static_cast<int>(scenes.prepare(sceneId)));
  TEST_ASSERT_EQUAL_INT(1, homeAssistantDriver.deactivationCount);
  TEST_ASSERT_EQUAL_INT(1, physicalDriver.firstActivationOrder);
  TEST_ASSERT_EQUAL_INT(2, homeAssistantDriver.firstActivationOrder);
  scenes.cancel();
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
  TEST_ASSERT_EQUAL(studio::kInvalidInstanceId, manager.foregroundInstance());
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
  TEST_ASSERT_TRUE(manager.acquire(instanceId, studio::ConnectionOwner::Foreground));
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
  FakeDriver triggerDriver(studio::DriverId::CanonTrigger);
  studio::DeviceDriver* drivers[] = {&sharkDriver, &canonDriver, &triggerDriver};
  studio::DeviceManager manager(backend, legacy, drivers, 3);
  TEST_ASSERT_TRUE(manager.begin());

  studio::InstanceId canonId = studio::kInvalidInstanceId;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(
          manager.add(studio::DriverId::CanonBle, "R6 Mark III", canonId)));
  TEST_ASSERT_TRUE(manager.acquire(canonId, studio::ConnectionOwner::Foreground));
  TEST_ASSERT_EQUAL_INT(0, sharkDriver.activationCount);
  TEST_ASSERT_EQUAL_INT(1, canonDriver.activationCount);
  TEST_ASSERT_EQUAL_INT(0, triggerDriver.activationCount);

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
  TEST_ASSERT_EQUAL_INT(0, triggerDriver.forgetPairingCount);

  studio::InstanceId triggerId = studio::kInvalidInstanceId;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(manager.add(studio::DriverId::CanonTrigger,
                                   "R6 Trigger", triggerId)));
  manager.release(canonId, studio::ConnectionOwner::Foreground);
  TEST_ASSERT_TRUE(manager.acquire(triggerId, studio::ConnectionOwner::Foreground));
  TEST_ASSERT_EQUAL_INT(1, triggerDriver.activationCount);
  TEST_ASSERT_FALSE(canonDriver.active);
  command.instanceId = triggerId;
  command.type = studio::CommandType::RecordTrigger;
  TEST_ASSERT_TRUE(manager.enqueue(command));
  manager.loop();
  TEST_ASSERT_TRUE(manager.popResult(result));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::CommandStatus::Succeeded),
                        static_cast<int>(result.status));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::CommandType::RecordTrigger),
      static_cast<int>(triggerDriver.lastCommand));
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
  TEST_ASSERT_TRUE(manager.acquire(id, studio::ConnectionOwner::Foreground));

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
  const studio::InstanceId removedId = first.at(0)->instanceId;
  TEST_ASSERT_TRUE(
      first.acquire(removedId, studio::ConnectionOwner::Foreground));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(first.remove(removedId)));
  TEST_ASSERT_FALSE(first.isActive(removedId));
  TEST_ASSERT_EQUAL_INT(1, firstDriver.deactivationCount);
  TEST_ASSERT_EQUAL_INT(1, firstDriver.forgetPairingCount);

  FakeDriver secondDriver;
  studio::DeviceDriver* secondDrivers[] = {&secondDriver};
  studio::DeviceManager second(backend, legacy, secondDrivers, 1);
  TEST_ASSERT_TRUE(second.begin());
  TEST_ASSERT_EQUAL_UINT32(0, second.count());
  TEST_ASSERT_EQUAL_INT(0, secondDriver.activationCount);
}

void test_admin_mutations_roll_back_when_persistence_fails() {
  MemoryBackend deviceBackend;
  MemoryBackend sceneBackend;
  LegacyBackend legacy;
  FakeDriver driver(studio::DriverId::CanonBle);
  studio::DeviceDriver* drivers[] = {&driver};
  studio::DeviceManager devices(deviceBackend, legacy, drivers, 1);
  TEST_ASSERT_TRUE(devices.begin());

  studio::InstanceId cameraId = studio::kInvalidInstanceId;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(devices.add(studio::DriverId::CanonBle, "Camera",
                                   cameraId)));
  deviceBackend.failWrites = true;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Invalid),
      static_cast<int>(devices.update(cameraId, "Renamed", false)));
  TEST_ASSERT_EQUAL_STRING("Camera", devices.find(cameraId)->displayName);
  TEST_ASSERT_TRUE(devices.find(cameraId)->enabled);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Invalid),
      static_cast<int>(devices.remove(cameraId)));
  TEST_ASSERT_NOT_NULL(devices.find(cameraId));
  TEST_ASSERT_EQUAL_INT(0, driver.forgetPairingCount);

  studio::SceneService scenes(sceneBackend, devices);
  TEST_ASSERT_TRUE(scenes.begin());
  studio::SceneId sceneId = studio::kInvalidSceneId;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::SceneRegistryStatus::Ok),
      static_cast<int>(scenes.add("Take", sceneId)));
  studio::SceneRecord original = *scenes.find(sceneId);
  original.startCount = 1;
  original.startSteps[0] =
      studio::makeActionStep(cameraId, studio::CommandType::RecordStart);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::SceneRegistryStatus::Ok),
      static_cast<int>(scenes.replace(original)));

  sceneBackend.failWrites = true;
  studio::SceneId copyId = studio::kInvalidSceneId;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::SceneRegistryStatus::Invalid),
      static_cast<int>(scenes.duplicate(sceneId, "Take copy", copyId)));
  TEST_ASSERT_EQUAL(studio::kInvalidSceneId, copyId);
  TEST_ASSERT_EQUAL_UINT32(1, scenes.count());
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::SceneRegistryStatus::Invalid),
      static_cast<int>(scenes.rename(sceneId, "Changed")));
  TEST_ASSERT_EQUAL_STRING("Take", scenes.find(sceneId)->name);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::SceneRegistryStatus::Invalid),
      static_cast<int>(scenes.setEnabled(sceneId, false)));
  TEST_ASSERT_TRUE(scenes.find(sceneId)->enabled);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::SceneRegistryStatus::Invalid),
      static_cast<int>(scenes.remove(sceneId)));
  TEST_ASSERT_NOT_NULL(scenes.find(sceneId));
}

void test_manager_holds_concurrent_active_links() {
  MemoryBackend backend;
  LegacyBackend legacy;
  FakeDriver canonDriver(studio::DriverId::CanonBle);
  FakeDriver tascamDriver(studio::DriverId::TascamX8);
  studio::DeviceDriver* drivers[] = {&canonDriver, &tascamDriver};
  studio::DeviceManager manager(backend, legacy, drivers, 2);
  TEST_ASSERT_TRUE(manager.begin());

  studio::InstanceId canonId = studio::kInvalidInstanceId;
  studio::InstanceId tascamId = studio::kInvalidInstanceId;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(
          manager.add(studio::DriverId::CanonBle, "Camera", canonId)));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(
          manager.add(studio::DriverId::TascamX8, "Recorder", tascamId)));
  TEST_ASSERT_TRUE(manager.acquire(canonId, studio::ConnectionOwner::Sequence));
  TEST_ASSERT_TRUE(manager.acquire(tascamId, studio::ConnectionOwner::Sequence));
  TEST_ASSERT_EQUAL_UINT32(2, manager.activeCount());
  TEST_ASSERT_TRUE(manager.isActive(canonId));
  TEST_ASSERT_TRUE(manager.isActive(tascamId));
  TEST_ASSERT_TRUE(canonDriver.active);
  TEST_ASSERT_TRUE(tascamDriver.active);

  studio::DeviceCommand command;
  command.instanceId = canonId;
  command.type = studio::CommandType::RecordStart;
  TEST_ASSERT_TRUE(manager.enqueue(command));
  manager.loop();
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::CommandType::RecordStart),
      static_cast<int>(canonDriver.lastCommand));
  command.instanceId = tascamId;
  command.type = studio::CommandType::RecordStart;
  TEST_ASSERT_TRUE(manager.enqueue(command));
  manager.loop();
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::CommandType::RecordStart),
      static_cast<int>(tascamDriver.lastCommand));

  manager.deactivateAll();
  TEST_ASSERT_EQUAL_UINT32(0, manager.activeCount());
  TEST_ASSERT_FALSE(canonDriver.active);
  TEST_ASSERT_FALSE(tascamDriver.active);
}

void test_manager_retains_ready_sessions_and_evicts_safe_lru() {
  MemoryBackend backend;
  LegacyBackend legacy;
  FakeDriver sharkDriver;
  FakeDriver canonDriver(studio::DriverId::CanonBle);
  FakeDriver tascamDriver(studio::DriverId::TascamX8);
  FakeDriver homeAssistantDriver(studio::DriverId::HomeAssistant);
  studio::DeviceDriver* drivers[] = {&sharkDriver, &canonDriver, &tascamDriver,
                                     &homeAssistantDriver};
  studio::DeviceManager manager(backend, legacy, drivers, 4);
  TEST_ASSERT_TRUE(manager.begin());
  const studio::InstanceId sharkId = manager.at(0)->instanceId;
  studio::InstanceId canonIds[3] = {};
  studio::InstanceId tascamId = studio::kInvalidInstanceId;
  for (studio::InstanceId& id : canonIds) {
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(studio::RegistryStatus::Ok),
        static_cast<int>(manager.add(studio::DriverId::CanonBle, "Camera", id)));
  }
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(
          manager.add(studio::DriverId::TascamX8, "Recorder", tascamId)));
  studio::InstanceId haIds[4] = {};
  for (size_t i = 0; i < 4; ++i) {
    char entityId[32];
    std::snprintf(entityId, sizeof(entityId), "switch.test_%u",
                  static_cast<unsigned>(i));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(studio::RegistryStatus::Ok),
        static_cast<int>(manager.addHomeAssistantEntity(
            studio::HomeAssistantDomain::Switch, entityId, "HA switch",
            haIds[i])));
  }

  const studio::InstanceId initial[] = {
      sharkId, canonIds[0], canonIds[1], tascamId,
      haIds[0], haIds[1], haIds[2], haIds[3]};
  for (studio::InstanceId id : initial) {
    TEST_ASSERT_TRUE(manager.acquire(id, studio::ConnectionOwner::Foreground));
    manager.loop();
    manager.release(id, studio::ConnectionOwner::Foreground);
    TEST_ASSERT_TRUE(manager.isRetained(id));
  }
  TEST_ASSERT_EQUAL_UINT32(CONFIG_MAX_ACTIVE_INSTANCES, manager.activeCount());
  TEST_ASSERT_EQUAL_UINT32(CONFIG_MAX_ACTIVE_LINKS, manager.bleSlotCount());

  // The oldest retained-idle session is evicted for a fifth device, while
  // multiple instances backed by the same driver remain active together.
  TEST_ASSERT_TRUE(
      manager.acquire(canonIds[2], studio::ConnectionOwner::Foreground));
  TEST_ASSERT_FALSE(manager.isActive(sharkId));
  TEST_ASSERT_TRUE(manager.isActive(canonIds[0]));
  TEST_ASSERT_TRUE(manager.isActive(canonIds[1]));
  TEST_ASSERT_TRUE(manager.isActive(canonIds[2]));
  TEST_ASSERT_EQUAL_INT(3, canonDriver.activationCount);
  manager.loop();
  manager.release(canonIds[2], studio::ConnectionOwner::Foreground);

  // Make the recorder oldest, then protect it with confirmed recording. The
  // next acquisition must evict the oldest safe camera instead.
  tascamDriver.recordingConfirmed = true;
  tascamDriver.recording = true;
  for (studio::InstanceId id : canonIds) {
    TEST_ASSERT_TRUE(manager.acquire(id, studio::ConnectionOwner::Foreground));
    manager.release(id, studio::ConnectionOwner::Foreground);
  }
  TEST_ASSERT_TRUE(
      manager.acquire(sharkId, studio::ConnectionOwner::Foreground));
  TEST_ASSERT_TRUE(manager.isActive(tascamId));
  TEST_ASSERT_FALSE(manager.isActive(canonIds[0]));
  TEST_ASSERT_TRUE(manager.isActive(haIds[0]));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::CommandStatus::ConfirmationRequired),
      static_cast<int>(manager.disconnect(tascamId)));
  TEST_ASSERT_TRUE(manager.isActive(tascamId));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::CommandStatus::Succeeded),
                        static_cast<int>(manager.disconnect(tascamId, true)));
  TEST_ASSERT_FALSE(manager.isActive(tascamId));
}

void test_manager_counts_shared_mesh_as_one_ble_slot() {
  MemoryBackend backend;
  LegacyBackend legacy;
  FakeDriver sharkDriver;
  FakeDriver canonDriver(studio::DriverId::CanonBle);
  FakeDriver tascamDriver(studio::DriverId::TascamX8);
  FakeDriver meshDriver(studio::DriverId::AmaranLight);
  FakeDriver zhiyunDriver(studio::DriverId::ZhiyunLight);
  meshDriver.sharedBleGroup = 1;
  meshDriver.sharedBleFamily = studio::DriverId::PanelOwnedMesh;
  zhiyunDriver.sharedBleGroup = 1;
  zhiyunDriver.sharedBleFamily = studio::DriverId::PanelOwnedMesh;
  studio::DeviceDriver* drivers[] = {
      &sharkDriver, &canonDriver, &tascamDriver, &meshDriver, &zhiyunDriver};
  studio::DeviceManager manager(backend, legacy, drivers, 5);
  TEST_ASSERT_TRUE(manager.begin());
  const studio::InstanceId sharkId = manager.at(0)->instanceId;
  studio::InstanceId canonId = studio::kInvalidInstanceId;
  studio::InstanceId tascamId = studio::kInvalidInstanceId;
  studio::InstanceId meshIds[2] = {};
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(
          manager.add(studio::DriverId::CanonBle, "Camera", canonId)));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(
          manager.add(studio::DriverId::TascamX8, "Recorder", tascamId)));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(manager.add(studio::DriverId::AmaranLight,
                                   "Sidus mesh light", meshIds[0])));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(manager.add(studio::DriverId::ZhiyunLight,
                                   "Zhiyun mesh light", meshIds[1])));

  const studio::InstanceId initial[] = {
      meshIds[0], sharkId, canonId, tascamId};
  for (studio::InstanceId id : initial) {
    TEST_ASSERT_TRUE(manager.acquire(id, studio::ConnectionOwner::Foreground));
    manager.loop();
    manager.release(id, studio::ConnectionOwner::Foreground);
  }
  TEST_ASSERT_EQUAL_UINT32(4, manager.activeCount());
  TEST_ASSERT_EQUAL_UINT32(4, manager.bleSlotCount());

  // A second logical member joins the already-counted mesh transport even
  // while all four physical BLE slots are occupied.
  TEST_ASSERT_TRUE(
      manager.acquire(meshIds[1], studio::ConnectionOwner::Foreground));
  manager.loop();
  manager.release(meshIds[1], studio::ConnectionOwner::Foreground);
  TEST_ASSERT_EQUAL_UINT32(5, manager.activeCount());
  TEST_ASSERT_EQUAL_UINT32(4, manager.bleSlotCount());
  TEST_ASSERT_TRUE(manager.isActive(meshIds[0]));
  TEST_ASSERT_TRUE(manager.isActive(meshIds[1]));

  // Make each single-instance group newer than the mesh group so the next
  // distinct connection evicts the complete shared group.
  const studio::InstanceId newer[] = {sharkId, canonId, tascamId};
  for (studio::InstanceId id : newer) {
    TEST_ASSERT_TRUE(manager.acquire(id, studio::ConnectionOwner::Foreground));
    manager.release(id, studio::ConnectionOwner::Foreground);
  }

  studio::InstanceId secondCanon = studio::kInvalidInstanceId;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(manager.add(studio::DriverId::CanonBle, "Camera 2",
                                   secondCanon)));
  TEST_ASSERT_TRUE(
      manager.acquire(secondCanon, studio::ConnectionOwner::Foreground));
  TEST_ASSERT_FALSE(manager.isActive(meshIds[0]));
  TEST_ASSERT_FALSE(manager.isActive(meshIds[1]));
  TEST_ASSERT_EQUAL_UINT32(4, manager.bleSlotCount());
  TEST_ASSERT_EQUAL_INT(1, meshDriver.deactivationCount);
  TEST_ASSERT_EQUAL_INT(1, zhiyunDriver.deactivationCount);
}

void test_manager_cancels_unready_release_and_reuses_ready_session() {
  MemoryBackend backend;
  LegacyBackend legacy;
  FakeDriver driver(studio::DriverId::CanonBle);
  studio::DeviceDriver* drivers[] = {&driver};
  studio::DeviceManager manager(backend, legacy, drivers, 1);
  TEST_ASSERT_TRUE(manager.begin());
  studio::InstanceId id = studio::kInvalidInstanceId;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(manager.add(studio::DriverId::CanonBle, "Camera", id)));

  driver.ready = false;
  TEST_ASSERT_TRUE(manager.acquire(id, studio::ConnectionOwner::Foreground));
  manager.release(id, studio::ConnectionOwner::Foreground);
  TEST_ASSERT_FALSE(manager.isActive(id));
  TEST_ASSERT_EQUAL_INT(1, driver.deactivationCount);

  driver.ready = true;
  TEST_ASSERT_TRUE(manager.acquire(id, studio::ConnectionOwner::Foreground));
  manager.loop();
  manager.release(id, studio::ConnectionOwner::Foreground);
  TEST_ASSERT_TRUE(manager.isRetained(id));
  const int activations = driver.activationCount;
  driver.resumeSucceeds = false;
  TEST_ASSERT_FALSE(manager.acquire(id, studio::ConnectionOwner::Foreground));
  TEST_ASSERT_FALSE(
      manager.ownedBy(id, studio::ConnectionOwner::Foreground));
  TEST_ASSERT_EQUAL_INT(activations, driver.activationCount);
  TEST_ASSERT_EQUAL_INT(1, driver.resumeCount);
  TEST_ASSERT_EQUAL_INT(id, driver.lastResumedInstance);

  driver.resumeSucceeds = true;
  TEST_ASSERT_TRUE(manager.acquire(id, studio::ConnectionOwner::Foreground));
  TEST_ASSERT_EQUAL_INT(activations, driver.activationCount);
  TEST_ASSERT_EQUAL_INT(2, driver.resumeCount);
  TEST_ASSERT_EQUAL_INT(id, driver.lastResumedInstance);
  manager.release(id, studio::ConnectionOwner::Foreground);
}

void test_manager_parks_ownerless_drop_but_keeps_intentional_offline() {
  MemoryBackend backend;
  LegacyBackend legacy;
  FakeDriver driver(studio::DriverId::CanonBle);
  studio::DeviceDriver* drivers[] = {&driver};
  studio::DeviceManager manager(backend, legacy, drivers, 1);
  TEST_ASSERT_TRUE(manager.begin());
  studio::InstanceId id = studio::kInvalidInstanceId;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(manager.add(studio::DriverId::CanonBle, "Camera", id)));

  TEST_ASSERT_TRUE(manager.acquire(id, studio::ConnectionOwner::Foreground));
  manager.loop();
  manager.release(id, studio::ConnectionOwner::Foreground);
  TEST_ASSERT_TRUE(manager.isRetained(id));

  driver.forceDisconnected = true;
  driver.intentionalOffline = true;
  manager.loop();
  TEST_ASSERT_TRUE(manager.isActive(id));

  driver.intentionalOffline = false;
  manager.loop();
  TEST_ASSERT_FALSE(manager.isActive(id));
  TEST_ASSERT_EQUAL_INT(1, driver.deactivationCount);
}

void test_scene_store_round_trip_and_corruption() {
  MemoryBackend backend;
  studio::SceneStore store(backend);
  studio::SceneRegistry source;
  studio::SceneId id = studio::kInvalidSceneId;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::SceneRegistryStatus::Ok),
      static_cast<int>(source.add("Press Record", id)));
  studio::SceneRecord* record = source.find(id);
  TEST_ASSERT_NOT_NULL(record);
  record->startCount = 3;
  record->startSteps[0] =
      studio::makeActionStep(2, studio::CommandType::SetLightCct, 5600, 72, -125);
  record->startSteps[1] = studio::makeWaitStep(500);
  record->startSteps[2] =
      studio::makeActionStep(3, studio::CommandType::RecordStart);
  record->stopCount = 2;
  record->stopSteps[0] =
      studio::makeActionStep(2, studio::CommandType::RecordStop);
  record->stopSteps[1] =
      studio::makeActionStep(3, studio::CommandType::RecordStop);
  TEST_ASSERT_TRUE(store.save(source));

  studio::SceneRegistry restored;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::ConfigLoadStatus::Loaded),
      static_cast<int>(store.load(restored)));
  TEST_ASSERT_EQUAL_UINT32(1, restored.count());
  TEST_ASSERT_EQUAL_STRING("Press Record", restored.at(0)->name);
  TEST_ASSERT_EQUAL_UINT8(3, restored.at(0)->startCount);
  TEST_ASSERT_EQUAL_UINT8(2, restored.at(0)->stopCount);
  TEST_ASSERT_EQUAL_UINT32(500, restored.at(0)->startSteps[1].waitMs);
  TEST_ASSERT_EQUAL_INT32(5600, restored.at(0)->startSteps[0].value0);
  TEST_ASSERT_EQUAL_INT32(72, restored.at(0)->startSteps[0].value1);
  TEST_ASSERT_EQUAL_INT32(-125, restored.at(0)->startSteps[0].value2);

  backend.corruptLastByte();
  studio::SceneRegistry rejected;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::ConfigLoadStatus::Corrupt),
      static_cast<int>(store.load(rejected)));
}

void test_scene_v1_migration_zeroes_action_arguments() {
  V1SceneBackend backend;
  studio::SceneStore store(backend);
  studio::SceneRegistry restored;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::ConfigLoadStatus::Loaded),
                        static_cast<int>(store.load(restored)));
  TEST_ASSERT_EQUAL_UINT32(1, restored.count());
  const studio::SceneStep& step = restored.at(0)->startSteps[0];
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::CommandType::TurnOn),
                        static_cast<int>(step.command));
  TEST_ASSERT_EQUAL_INT32(0, step.value0);
  TEST_ASSERT_EQUAL_INT32(0, step.value1);
  TEST_ASSERT_EQUAL_INT32(0, step.value2);
}

void test_press_record_start_and_authored_stop() {
  MemoryBackend deviceBackend;
  MemoryBackend sceneBackend;
  LegacyBackend legacy;
  FakeDriver canonDriver(studio::DriverId::CanonBle);
  FakeDriver tascamDriver(studio::DriverId::TascamX8);
  studio::DeviceDriver* drivers[] = {&canonDriver, &tascamDriver};
  studio::DeviceManager devices(deviceBackend, legacy, drivers, 2);
  TEST_ASSERT_TRUE(devices.begin());
  // Drop the seeded Shark-less registry noise: begin may seed only Shark when
  // compiled; with no Shark driver the registry starts empty/missing.
  studio::InstanceId canonId = studio::kInvalidInstanceId;
  studio::InstanceId tascamId = studio::kInvalidInstanceId;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(
          devices.add(studio::DriverId::CanonBle, "R6 II", canonId)));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(
          devices.add(studio::DriverId::TascamX8, "X8", tascamId)));

  studio::SceneService scenes(sceneBackend, devices);
  TEST_ASSERT_TRUE(scenes.begin());
  studio::SceneId sceneId = studio::kInvalidSceneId;
  TEST_ASSERT_TRUE(scenes.seedPressRecord(sceneId));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::SceneValidationStatus::Ok),
      static_cast<int>(scenes.validate(sceneId)));

  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::SceneRunStatus::Ok),
                        static_cast<int>(scenes.start(sceneId)));
  // Fake drivers report Connected as soon as activated.
  scenes.loop(0);
  devices.loop();
  scenes.loop(1);
  devices.loop();
  scenes.loop(2);
  // After connect + first action result processing.
  for (uint32_t t = 3; t < 20; ++t) {
    devices.loop();
    scenes.loop(t);
  }
  // Wait 500 ms from when wait began; advance far enough.
  for (uint32_t t = 20; t < 600; ++t) {
    devices.loop();
    scenes.loop(t);
  }
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::ScenePhase::IdleArmed),
      static_cast<int>(scenes.progress().phase));
  TEST_ASSERT_TRUE(devices.isActive(canonId));
  TEST_ASSERT_TRUE(devices.isActive(tascamId));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::CommandType::RecordStart),
      static_cast<int>(canonDriver.lastCommand));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::CommandType::RecordStart),
      static_cast<int>(tascamDriver.lastCommand));

  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::SceneRunStatus::Ok),
                        static_cast<int>(scenes.stop()));
  for (uint32_t t = 600; t < 620; ++t) {
    devices.loop();
    scenes.loop(t);
  }
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::ScenePhase::Completed),
      static_cast<int>(scenes.progress().phase));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::CommandType::RecordStop),
      static_cast<int>(canonDriver.lastCommand));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::CommandType::RecordStop),
      static_cast<int>(tascamDriver.lastCommand));
  TEST_ASSERT_TRUE(scenes.holdsLinks());
  TEST_ASSERT_TRUE(devices.isActive(canonId));
  TEST_ASSERT_TRUE(devices.isActive(tascamId));
  scenes.cancel();
  TEST_ASSERT_EQUAL_UINT32(2, devices.activeCount());
  TEST_ASSERT_FALSE(devices.ownedBy(canonId, studio::ConnectionOwner::Sequence));
  TEST_ASSERT_FALSE(devices.ownedBy(tascamId, studio::ConnectionOwner::Sequence));
}

void test_partial_start_failure_can_stop_and_restart() {
  MemoryBackend deviceBackend;
  MemoryBackend sceneBackend;
  LegacyBackend legacy;
  FakeDriver canonDriver(studio::DriverId::CanonBle);
  FakeDriver tascamDriver(studio::DriverId::TascamX8);
  studio::DeviceDriver* drivers[] = {&canonDriver, &tascamDriver};
  studio::DeviceManager devices(deviceBackend, legacy, drivers, 2);
  TEST_ASSERT_TRUE(devices.begin());

  studio::InstanceId canonId = studio::kInvalidInstanceId;
  studio::InstanceId tascamId = studio::kInvalidInstanceId;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(
          devices.add(studio::DriverId::CanonBle, "R6 II", canonId)));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(
          devices.add(studio::DriverId::TascamX8, "X8", tascamId)));

  studio::SceneService scenes(sceneBackend, devices);
  TEST_ASSERT_TRUE(scenes.begin());
  studio::SceneId sceneId = studio::kInvalidSceneId;
  TEST_ASSERT_TRUE(scenes.seedPressRecord(sceneId));

  tascamDriver.failCommand = studio::CommandType::RecordStart;
  tascamDriver.failCommandCount = 1;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::SceneRunStatus::Ok),
                        static_cast<int>(scenes.start(sceneId)));
  for (uint32_t t = 1; t < 700; ++t) {
    devices.loop();
    scenes.loop(t);
  }
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::ScenePhase::Failed),
                        static_cast<int>(scenes.progress().phase));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::SceneRunStatus::ActionFailed),
                        static_cast<int>(scenes.progress().lastStatus));

  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::SceneRunStatus::Ok),
                        static_cast<int>(scenes.stop()));
  for (uint32_t t = 700; t < 730; ++t) {
    devices.loop();
    scenes.loop(t);
  }
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::ScenePhase::Completed),
                        static_cast<int>(scenes.progress().phase));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::CommandType::RecordStop),
                        static_cast<int>(canonDriver.lastCommand));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::CommandType::RecordStop),
                        static_cast<int>(tascamDriver.lastCommand));

  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::SceneRunStatus::Ok),
                        static_cast<int>(scenes.start(sceneId)));
  for (uint32_t t = 730; t < 1400; ++t) {
    devices.loop();
    scenes.loop(t);
  }
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::ScenePhase::IdleArmed),
                        static_cast<int>(scenes.progress().phase));
}

void test_prepare_ready_then_start_from_held_links() {
  MemoryBackend deviceBackend;
  MemoryBackend sceneBackend;
  LegacyBackend legacy;
  FakeDriver canonDriver(studio::DriverId::CanonBle);
  FakeDriver tascamDriver(studio::DriverId::TascamX8);
  studio::DeviceDriver* drivers[] = {&canonDriver, &tascamDriver};
  studio::DeviceManager devices(deviceBackend, legacy, drivers, 2);
  TEST_ASSERT_TRUE(devices.begin());
  studio::InstanceId canonId = studio::kInvalidInstanceId;
  studio::InstanceId tascamId = studio::kInvalidInstanceId;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(
          devices.add(studio::DriverId::CanonBle, "R6 II", canonId)));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(
          devices.add(studio::DriverId::TascamX8, "X8", tascamId)));

  studio::SceneService scenes(sceneBackend, devices);
  TEST_ASSERT_TRUE(scenes.begin());
  studio::SceneId sceneId = studio::kInvalidSceneId;
  TEST_ASSERT_TRUE(scenes.seedPressRecord(sceneId));

  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::SceneRunStatus::Ok),
                        static_cast<int>(scenes.prepare(sceneId)));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::ScenePhase::Connecting),
      static_cast<int>(scenes.progress().phase));
  tascamDriver.ready = false;
  devices.loop();
  scenes.loop(1);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::ScenePhase::Connecting),
                        static_cast<int>(scenes.progress().phase));
  // A recovering link may legitimately pass the old 20-second boundary;
  // preparation remains active until the bounded 30-second terminal timeout.
  scenes.loop(20001);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::ScenePhase::Connecting),
                        static_cast<int>(scenes.progress().phase));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::SceneRunStatus::Busy),
                        static_cast<int>(scenes.start(sceneId)));
  tascamDriver.ready = true;
  scenes.loop(20002);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::ScenePhase::Ready),
                        static_cast<int>(scenes.progress().phase));
  TEST_ASSERT_TRUE(scenes.holdsLinks());
  TEST_ASSERT_TRUE(devices.isActive(canonId));
  TEST_ASSERT_TRUE(devices.isActive(tascamId));
  TEST_ASSERT_FALSE(scenes.busy());

  const studio::SceneRecord original = *scenes.find(sceneId);
  studio::SceneRecord edited = original;
  edited.startSteps[1] = studio::makeWaitStep(650);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::SceneRegistryStatus::Ok),
      static_cast<int>(scenes.replace(edited)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::ScenePhase::Ready),
                        static_cast<int>(scenes.progress().phase));
  TEST_ASSERT_TRUE(devices.isActive(canonId));
  TEST_ASSERT_TRUE(devices.isActive(tascamId));

  studio::SceneRecord cameraOnly = edited;
  cameraOnly.startCount = 1;
  cameraOnly.startSteps[0] =
      studio::makeActionStep(canonId, studio::CommandType::RecordStart);
  cameraOnly.stopCount = 1;
  cameraOnly.stopSteps[0] =
      studio::makeActionStep(canonId, studio::CommandType::RecordStop);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::SceneRegistryStatus::Ok),
      static_cast<int>(scenes.replace(cameraOnly)));
  TEST_ASSERT_TRUE(devices.isActive(canonId));
  TEST_ASSERT_TRUE(devices.isActive(tascamId));
  TEST_ASSERT_FALSE(
      devices.ownedBy(tascamId, studio::ConnectionOwner::Sequence));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::SceneRegistryStatus::Ok),
      static_cast<int>(scenes.replace(original)));
  TEST_ASSERT_TRUE(devices.isActive(canonId));
  TEST_ASSERT_TRUE(devices.isActive(tascamId));

  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::SceneRunStatus::Ok),
                        static_cast<int>(scenes.start(sceneId)));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::ScenePhase::RunningStart),
      static_cast<int>(scenes.progress().phase));
  for (uint32_t t = 3; t < 601; ++t) {
    devices.loop();
    scenes.loop(t);
  }
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::ScenePhase::IdleArmed),
      static_cast<int>(scenes.progress().phase));
  TEST_ASSERT_TRUE(devices.isActive(canonId));
  TEST_ASSERT_TRUE(devices.isActive(tascamId));

  scenes.cancel();
  TEST_ASSERT_FALSE(scenes.holdsLinks());
  TEST_ASSERT_EQUAL_UINT32(2, devices.activeCount());
}

class BleTestDelegate : public studio::ble::BleCentralDelegate {
 public:
  void onBleAdvertisement(
      studio::ble::LinkHandle link,
      const studio::ble::Advertisement& advertisement) override {
    ++advertisements;
    lastAdvertisement = advertisement;
    if (central != nullptr && selectAdvertisements) {
      selected = central->selectAdvertisement(link, advertisement);
    }
  }

  void onBleEvent(studio::ble::LinkHandle link,
                  const studio::ble::Event& event) override {
    ++events;
    lastEvent = event.type;
    if (central != nullptr && requestSecurityOnConnect &&
        event.type == studio::ble::EventType::Connected) {
      securityRequested = central->requestSecurity(link);
    }
  }

  studio::ble::BleCentral* central = nullptr;
  studio::ble::Advertisement lastAdvertisement;
  studio::ble::EventType lastEvent = studio::ble::EventType::ScanEnded;
  uint32_t advertisements = 0;
  uint32_t events = 0;
  bool selectAdvertisements = false;
  bool requestSecurityOnConnect = false;
  bool selected = false;
  bool securityRequested = false;
};

studio::ble::Address bleAddress(const char* value, uint8_t type = 0) {
  studio::ble::Address address;
  std::strncpy(address.value, value, sizeof(address.value) - 1);
  address.type = type;
  return address;
}

studio::ble::Advertisement bleAdvertisement(const char* address,
                                             const char* name,
                                             uint16_t service) {
  studio::ble::Advertisement advertisement;
  advertisement.address = bleAddress(address);
  size_t offset = 0;
  const size_t nameLength = std::strlen(name);
  advertisement.payload[offset++] = static_cast<uint8_t>(nameLength + 1);
  advertisement.payload[offset++] = 0x09;
  std::memcpy(advertisement.payload + offset, name, nameLength);
  offset += nameLength;
  advertisement.payload[offset++] = 3;
  advertisement.payload[offset++] = 0x03;
  advertisement.payload[offset++] = static_cast<uint8_t>(service & 0xff);
  advertisement.payload[offset++] = static_cast<uint8_t>(service >> 8);
  advertisement.payloadLength = static_cast<uint8_t>(offset);
  return advertisement;
}

studio::ble::Advertisement bleAdvertisement128(
    const uint8_t canonicalUuid[16]) {
  studio::ble::Advertisement advertisement;
  advertisement.address = bleAddress("11:22:33:44:55:66");
  advertisement.payload[0] = 17;
  advertisement.payload[1] = 0x07;
  for (size_t i = 0; i < 16; ++i) {
    advertisement.payload[2 + i] = canonicalUuid[15 - i];
  }
  advertisement.payloadLength = 18;
  return advertisement;
}

void test_ble_central_lazy_lifetime_and_slot_exhaustion() {
  studio::ble::FakeBleBackend backend;
  studio::ble::BleCentral central(backend);
  BleTestDelegate delegates[CONFIG_MAX_ACTIVE_LINKS + 1];
  studio::ble::LinkHandle handles[CONFIG_MAX_ACTIVE_LINKS] = {};

  TEST_ASSERT_FALSE(backend.initialized());
  for (size_t i = 0; i < CONFIG_MAX_ACTIVE_LINKS; ++i) {
    handles[i] = central.acquire(delegates[i], {});
    TEST_ASSERT_NOT_EQUAL(studio::ble::kInvalidLinkHandle, handles[i]);
  }
  TEST_ASSERT_TRUE(backend.initialized());
  TEST_ASSERT_EQUAL_UINT32(1, backend.beginCalls());
  TEST_ASSERT_EQUAL_UINT32(
      studio::ble::kInvalidLinkHandle,
      central.acquire(delegates[CONFIG_MAX_ACTIVE_LINKS], {}));

  for (studio::ble::LinkHandle handle : handles) {
    central.release(handle);
  }
  TEST_ASSERT_FALSE(backend.initialized());
  TEST_ASSERT_EQUAL_UINT32(1, backend.shutdownCalls());
}

void test_ble_central_timing_and_readiness_reset_on_release() {
  studio::ble::FakeBleBackend backend;
  studio::ble::BleCentral central(backend);
  BleTestDelegate delegate;
  studio::ble::LinkHandle link = central.acquire(delegate, {});
  central.loop(25);
  TEST_ASSERT_TRUE(central.requestScan(link));
  TEST_ASSERT_EQUAL_UINT32(25, central.timingStartedAt(link));
  central.markProtocolReady(link);
  TEST_ASSERT_TRUE(central.protocolReady(link));

  central.release(link);
  TEST_ASSERT_FALSE(central.protocolReady(link));
  TEST_ASSERT_EQUAL_UINT32(0, central.timingStartedAt(link));

  link = central.acquire(delegate, {});
  central.loop(100);
  TEST_ASSERT_TRUE(central.requestScan(link));
  TEST_ASSERT_EQUAL_UINT32(100, central.timingStartedAt(link));
  TEST_ASSERT_FALSE(central.protocolReady(link));
  central.release(link);
}

void test_ble_central_shared_scan_claims_and_independent_release() {
  studio::ble::FakeBleBackend backend;
  studio::ble::BleCentral central(backend);
  BleTestDelegate first;
  BleTestDelegate second;
  first.central = &central;
  first.selectAdvertisements = true;
  const studio::ble::LinkHandle firstLink = central.acquire(first, {});
  const studio::ble::LinkHandle secondLink = central.acquire(second, {});
  TEST_ASSERT_TRUE(central.requestScan(firstLink));
  TEST_ASSERT_TRUE(central.requestScan(secondLink));
  TEST_ASSERT_EQUAL_UINT32(1, backend.scanStarts());

  const studio::ble::Advertisement advertisement =
      bleAdvertisement("11:22:33:44:55:66", "Portacapture X8", 0xfff0);
  backend.emitAdvertisement(advertisement);
  central.loop(10);
  TEST_ASSERT_EQUAL_UINT32(1, first.advertisements);
  TEST_ASSERT_EQUAL_UINT32(1, second.advertisements);
  TEST_ASSERT_TRUE(first.selected);
  TEST_ASSERT_TRUE(backend.scanRunning());
  TEST_ASSERT_FALSE(
      central.selectAdvertisement(secondLink, advertisement));

  central.release(secondLink);
  TEST_ASSERT_FALSE(backend.scanRunning());
  TEST_ASSERT_EQUAL_UINT32(1, central.activeCount());
  central.release(firstLink);
}

void test_ble_central_concurrent_links_retry_watchdog_and_security() {
  studio::ble::FakeBleBackend backend;
  studio::ble::BleCentral central(backend);
  BleTestDelegate first;
  BleTestDelegate second;
  studio::ble::ConnectPolicy policy;
  policy.security = studio::ble::SecurityPolicy::BondSecure;
  policy.connectWatchdogMs = 6000;
  const studio::ble::LinkHandle firstLink = central.acquire(first, policy);
  const studio::ble::LinkHandle secondLink = central.acquire(second, {});
  first.central = &central;
  first.requestSecurityOnConnect = true;
  TEST_ASSERT_TRUE(
      central.requestConnect(firstLink, bleAddress("11:22:33:44:55:66")));
  TEST_ASSERT_TRUE(
      central.requestConnect(secondLink, bleAddress("22:33:44:55:66:77")));
  TEST_ASSERT_EQUAL_UINT32(1, backend.connectCalls(firstLink));
  TEST_ASSERT_EQUAL_UINT32(0, backend.connectCalls(secondLink));

  studio::ble::Event connected;
  connected.type = studio::ble::EventType::Connected;
  connected.link = firstLink;
  backend.emit(connected);
  central.loop(100);
  TEST_ASSERT_FALSE(central.protocolReady(firstLink));
  TEST_ASSERT_EQUAL_UINT32(1, backend.parameterUpdateCalls(firstLink));
  TEST_ASSERT_EQUAL_UINT16(6, backend.lastParameters(firstLink).minInterval);
  TEST_ASSERT_TRUE(first.securityRequested);
  TEST_ASSERT_EQUAL_UINT32(1, backend.secureCalls(firstLink));
  TEST_ASSERT_EQUAL_UINT32(0, backend.connectCalls(secondLink));
  studio::ble::Event secured;
  secured.type = studio::ble::EventType::SecurityComplete;
  secured.link = firstLink;
  secured.succeeded = true;
  backend.emit(secured);
  central.loop(200);
  TEST_ASSERT_EQUAL_UINT32(1, backend.connectCalls(secondLink));
  backend.setParameterUpdateResult(false);
  central.markProtocolReady(firstLink);
  TEST_ASSERT_TRUE(central.protocolReady(firstLink));
  TEST_ASSERT_EQUAL_UINT32(2, backend.parameterUpdateCalls(firstLink));
  TEST_ASSERT_EQUAL_UINT16(24, backend.lastParameters(firstLink).minInterval);

  studio::ble::Event failed;
  failed.type = studio::ble::EventType::ConnectFailed;
  failed.link = secondLink;
  backend.emit(failed);
  central.loop(200);
  central.loop(1699);
  TEST_ASSERT_EQUAL_UINT32(1, backend.connectCalls(secondLink));
  central.loop(1700);
  TEST_ASSERT_EQUAL_UINT32(2, backend.connectCalls(secondLink));

  backend.emit(failed);
  central.loop(1701);
  central.loop(4700);
  TEST_ASSERT_EQUAL_UINT32(2, backend.connectCalls(secondLink));
  central.loop(4701);
  TEST_ASSERT_EQUAL_UINT32(3, backend.connectCalls(secondLink));

  // After three failed direct attempts, rediscover the peer rather than
  // retrying a potentially stale saved address forever.
  backend.emit(failed);
  central.loop(4702);
  central.loop(9201);
  TEST_ASSERT_FALSE(backend.scanRunning());
  central.loop(9202);
  TEST_ASSERT_TRUE(backend.scanRunning());

  TEST_ASSERT_TRUE(central.requestConnect(
      secondLink, bleAddress("22:33:44:55:66:77")));
  central.loop(15203);
  TEST_ASSERT_TRUE(backend.disconnectCalls(secondLink) > 0);
  central.release(firstLink);
  TEST_ASSERT_FALSE(central.protocolReady(firstLink));
  central.release(secondLink);
}

void test_ble_central_uses_bounded_scan_bursts() {
  studio::ble::FakeBleBackend backend;
  studio::ble::BleCentral central(backend);
  BleTestDelegate delegate;
  const studio::ble::LinkHandle link = central.acquire(delegate, {});
  central.loop(100);
  TEST_ASSERT_TRUE(central.requestScan(link));
  TEST_ASSERT_TRUE(backend.scanRunning());
  TEST_ASSERT_EQUAL_UINT32(1, backend.scanStarts());

  central.loop(4099);
  TEST_ASSERT_TRUE(backend.scanRunning());
  central.loop(4100);
  TEST_ASSERT_FALSE(backend.scanRunning());
  central.loop(5599);
  TEST_ASSERT_FALSE(backend.scanRunning());
  central.loop(5600);
  TEST_ASSERT_TRUE(backend.scanRunning());
  TEST_ASSERT_EQUAL_UINT32(2, backend.scanStarts());
  central.release(link);
}

void test_ble_central_parser_bonds_and_queue_overflow() {
  const studio::ble::Advertisement advertisement =
      bleAdvertisement("11:22:33:44:55:66", "Shark Nano II", 0xfff0);
  TEST_ASSERT_TRUE(
      studio::ble::advertisementNameEquals(advertisement, "Shark Nano II"));
  TEST_ASSERT_TRUE(
      studio::ble::advertisementNameContains(advertisement, "Nano"));
  TEST_ASSERT_TRUE(studio::ble::advertisesService(advertisement, "fff0"));

  studio::ble::FakeBleBackend backend;
  studio::ble::BleCentral central(backend);
  TEST_ASSERT_TRUE(
      central.deleteBond(bleAddress("11:22:33:44:55:66")));
  TEST_ASSERT_EQUAL_UINT32(1, backend.bondDeleteCalls());
  TEST_ASSERT_FALSE(backend.initialized());

  for (size_t i = 0; i < CONFIG_BLE_EVENT_QUEUE_SIZE + 2; ++i) {
    backend.emitAdvertisement(advertisement);
  }
  TEST_ASSERT_EQUAL_UINT32(2, backend.droppedEvents());
}

void test_ble_device_advertisement_matchers() {
  const studio::ble::Advertisement sharkAdvertisement =
      bleAdvertisement("11:22:33:44:55:66", "Shark Nano II", 0xfff0);
  TEST_ASSERT_TRUE(shark::matchesAdvertisement(sharkAdvertisement));
  const studio::ble::Advertisement tascamAdvertisement =
      bleAdvertisement("22:33:44:55:66:77", tascam_x8::kDeviceName, 0x1800);
  TEST_ASSERT_TRUE(tascam_x8::matchesAdvertisement(tascamAdvertisement));
  const uint8_t tascamUuid[16] = {
      0x24, 0x56, 0xe1, 0xb9, 0x26, 0xe2, 0x8f, 0x83,
      0xe7, 0x44, 0xf3, 0x4f, 0x01, 0xe9, 0xd7, 0x01};
  TEST_ASSERT_TRUE(
      tascam_x8::matchesAdvertisement(bleAdvertisement128(tascamUuid)));
  TEST_ASSERT_FALSE(tascam_x8::matchesAdvertisement(
      bleAdvertisement("22:33:44:55:66:77", "ANNA-B1-BC5A07", 0x1800)));
  const studio::ble::Advertisement unprovisionedAmaran =
      bleAdvertisement("44:55:66:77:88:99", "Amaran", 0x1827);
  TEST_ASSERT_TRUE(studio::ble::advertisesService(unprovisionedAmaran, "1827"));

  const uint8_t triggerUuid[16] = {
      0x00, 0x05, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00,
      0x00, 0x00, 0xd8, 0x49, 0x2f, 0xff, 0xa8, 0x21};
  TEST_ASSERT_TRUE(canon_trigger::matchesAdvertisement(
      bleAdvertisement128(triggerUuid)));

  studio::ble::Advertisement canonAdvertisement;
  canonAdvertisement.address = bleAddress("33:44:55:66:77:88");
  canonAdvertisement.payload[0] = 3;
  canonAdvertisement.payload[1] = 0xff;
  canonAdvertisement.payload[2] = 0xa9;
  canonAdvertisement.payload[3] = 0x01;
  canonAdvertisement.payloadLength = 4;
  TEST_ASSERT_TRUE(
      canon_ble::matchesAdvertisement(canonAdvertisement));
}

void test_amaran_crypto_and_network_vectors() {
  const uint8_t aesKey[16] = {0x2b,0x7e,0x15,0x16,0x28,0xae,0xd2,0xa6,
      0xab,0xf7,0x15,0x88,0x09,0xcf,0x4f,0x3c};
  const uint8_t aesInput[16] = {0x6b,0xc1,0xbe,0xe2,0x2e,0x40,0x9f,0x96,
      0xe9,0x3d,0x7e,0x11,0x73,0x93,0x17,0x2a};
  const uint8_t aesExpected[16] = {0x3a,0xd7,0x7b,0xb4,0x0d,0x7a,0x36,0x60,
      0xa8,0x9e,0xca,0xf3,0x24,0x66,0xef,0x97};
  uint8_t block[16];
  amaran_light::aes128EncryptBlock(aesKey, aesInput, block);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(aesExpected, block, 16);

  const uint8_t cmacExpected[16] = {0xbb,0x1d,0x69,0x29,0xe9,0x59,0x37,0x28,
      0x7f,0xa3,0x7d,0x12,0x9b,0x75,0x67,0x46};
  uint8_t cmac[16];
  amaran_light::aesCmac(aesKey, nullptr, 0, cmac);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(cmacExpected, cmac, 16);

  const uint8_t networkKey[16] = {0x00,0x11,0x22,0x33,0x44,0x55,0x66,0x77,
      0x88,0x99,0xaa,0xbb,0xcc,0xdd,0xee,0xff};
  const uint8_t appKey[16] = {0xff,0xee,0xdd,0xcc,0xbb,0xaa,0x99,0x88,
      0x77,0x66,0x55,0x44,0x33,0x22,0x11,0x00};
  amaran_light::AccessPayload blue;
  TEST_ASSERT_TRUE(amaran_light::buildRgbAccess(0x0000ff, 0, blue));
  // Lock the exact captured blue payload independently of the brightness-aware path.
  const uint8_t blueAccess[11] = {0x26,0xd3,0xa0,0,0,0,0xa0,0x0f,0,0,0x84};
  amaran_light::NetworkPdu network;
  TEST_ASSERT_TRUE(amaran_light::encodeAccessMessage(
      networkKey, appKey, blueAccess, sizeof(blueAccess), 1, 1, 0xc000, 0,
      network));
  const uint8_t expectedNetwork[] = {0x1e,0x72,0xa3,0xec,0xac,0x74,0x86,0xe5,
      0xfa,0x7f,0xf4,0x21,0x57,0xd4,0x22,0xd0,0xe9,0x15,0x35,0x5a,0x48,0x70,
      0xd9,0xfc,0xce,0xd7,0xc1,0xe3,0xba};
  TEST_ASSERT_EQUAL_UINT32(sizeof(expectedNetwork), network.length);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expectedNetwork, network.bytes, network.length);

  const uint8_t mcOff[] = {0x26,0xe8,0,0,0,0,0x20,0xa4,0x28,0xfa,0x02};
  TEST_ASSERT_TRUE(amaran_light::encodeAccessMessage(
      networkKey, appKey, mcOff, sizeof(mcOff), 0x1234, 2, 1, 0, network));
  uint8_t proxy[70] = {};
  size_t proxyLength = 0;
  TEST_ASSERT_TRUE(amaran_light::wrapProxyPdu(
      network, proxy, sizeof(proxy), proxyLength));
  amaran_light::DecodedAccessMessage decoded;
  TEST_ASSERT_TRUE(amaran_light::decodeProxyAccessMessage(
      networkKey, appKey, proxy, proxyLength, 0, decoded));
  TEST_ASSERT_EQUAL_UINT32(0x1234, decoded.sequence);
  TEST_ASSERT_EQUAL_UINT16(2, decoded.source);
  TEST_ASSERT_EQUAL_UINT16(1, decoded.destination);
  TEST_ASSERT_EQUAL_UINT32(sizeof(mcOff), decoded.accessLength);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(mcOff, decoded.access, decoded.accessLength);

  proxy[proxyLength - 1] ^= 1;
  TEST_ASSERT_FALSE(amaran_light::decodeProxyAccessMessage(
      networkKey, appKey, proxy, proxyLength, 0, decoded));
}

void test_amaran_access_payloads_and_validation() {
  amaran_light::AccessPayload payload;
  TEST_ASSERT_TRUE(amaran_light::buildPowerAccess(true, payload));
  const uint8_t powerOn[] = {0x26,0x8d,0,0,0,0,0,0,0,1,0x8c};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(powerOn, payload.bytes, sizeof(powerOn));
  TEST_ASSERT_TRUE(amaran_light::buildPowerStatusGetAccess(payload));
  const uint8_t powerGet[] = {0x26,0x0e,0,0,0,0,0,0,0,0,0x0e};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(powerGet, payload.bytes, sizeof(powerGet));
  const uint8_t mcOff[] = {0x26,0xe8,0,0,0,0,0x20,0xa4,0x28,0xfa,0x02};
  const uint8_t aceOn[] = {0x26,0xec,1,0,0,0,0x80,0x56,0x1a,0xfa,0x01};
  amaran_light::VendorPowerStatus status;
  TEST_ASSERT_TRUE(
      amaran_light::parseVendorPowerStatus(mcOff, sizeof(mcOff), status));
  TEST_ASSERT_FALSE(status.on);
  TEST_ASSERT_EQUAL_UINT8(250, status.storedIntensity);
  TEST_ASSERT_EQUAL_UINT8(2, status.profile);
  TEST_ASSERT_TRUE(
      amaran_light::parseVendorPowerStatus(aceOn, sizeof(aceOn), status));
  TEST_ASSERT_TRUE(status.on);
  TEST_ASSERT_EQUAL_UINT8(1, status.profile);
  uint8_t corruptStatus[sizeof(aceOn)];
  std::memcpy(corruptStatus, aceOn, sizeof(aceOn));
  corruptStatus[6] ^= 1;
  TEST_ASSERT_FALSE(amaran_light::parseVendorPowerStatus(
      corruptStatus, sizeof(corruptStatus), status));
  TEST_ASSERT_TRUE(amaran_light::buildCctAccess(5000, 0, 89, payload));
  const uint8_t cct[] = {0x26,0x80,0,0,0,0,0x40,0x41,0x9f,0xde,0x82};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(cct, payload.bytes, sizeof(cct));
  TEST_ASSERT_TRUE(amaran_light::buildRgbAccess(0xff0000, 50, payload));
  const uint8_t rgb[] = {0x26,0xdd,0x40,0x1f,0,0,0,0,0,0xfa,0x84};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(rgb, payload.bytes, sizeof(rgb));
  TEST_ASSERT_TRUE(amaran_light::buildRgbAccess(0xff0000, 5, payload));
  const uint8_t mcRed5[] = {
      0x26,0x87,0x06,0x03,0,0,0,0,0,0xfa,0x84};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(mcRed5, payload.bytes, sizeof(mcRed5));
  TEST_ASSERT_TRUE(amaran_light::buildRgbAccess(0x00ff00, 5, payload));
  const uint8_t aceGreen5[] = {
      0x26,0x4b,0x06,0x03,0,0,0,0x80,0x3e,0,0x84};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(aceGreen5, payload.bytes, sizeof(aceGreen5));
  TEST_ASSERT_TRUE(amaran_light::validCctCommand(2300, 0, -1000));
  TEST_ASSERT_FALSE(amaran_light::validCctCommand(2299, 50, 0));
  TEST_ASSERT_TRUE(amaran_light::validRgbCommand(0xffffff, 100));
  TEST_ASSERT_FALSE(amaran_light::validRgbCommand(0x1000000, 50));

  const uint8_t networkKey[16] = {};
  const uint8_t deviceKey[16] = {1};
  uint8_t appKeyAdd[20] = {};
  const uint32_t sequences[] = {0x123, 0x124};
  amaran_light::NetworkPduBatch segmented;
  TEST_ASSERT_TRUE(amaran_light::encodeSegmentedDeviceMessage(
      networkKey, deviceKey, appKeyAdd, sizeof(appKeyAdd), sequences, 2,
      1, 2, 0, segmented));
  TEST_ASSERT_EQUAL_UINT8(2, segmented.count);
  TEST_ASSERT_TRUE(segmented.pdus[0].length > 0);
  TEST_ASSERT_TRUE(segmented.pdus[1].length > 0);
  TEST_ASSERT_FALSE(amaran_light::encodeDeviceMessage(
      networkKey, deviceKey, appKeyAdd, sizeof(appKeyAdd), sequences[0],
      1, 2, 0, segmented.pdus[0]));
}

void test_amaran_store_and_sequence_reservation_survive_restart() {
  MemoryBackend backend;
  amaran_light::MeshStore store(backend);
  amaran_light::MeshStoreData data;
  data.network.initialized = true;
  for (uint8_t i = 0; i < 16; ++i) {
    data.network.networkKey[i] = i;
    data.network.applicationKey[i] = static_cast<uint8_t>(0xff - i);
  }
  amaran_light::MeshNodeRecord node;
  node.instanceId = 42;
  node.model = studio::DriverId::AmaranLight;
  node.unicastAddress = 2;
  node.configured = true;
  node.controlGroupAddress = 0xc001;
  node.vendorCompanyId = 0x03f6;
  node.vendorModelId = 0x1000;
  TEST_ASSERT_TRUE(amaran_light::upsertNode(data, node));
  amaran_light::MeshNodeRecord zhiyun;
  zhiyun.instanceId = 43;
  zhiyun.model = studio::DriverId::ZhiyunLight;
  zhiyun.unicastAddress = 3;
  zhiyun.configured = true;
  zhiyun.routingSelector =
      amaran_light::nextZhiyunRoutingSelector(data);
  TEST_ASSERT_EQUAL_UINT8(0, zhiyun.routingSelector);
  TEST_ASSERT_TRUE(amaran_light::upsertNode(data, zhiyun));
  TEST_ASSERT_EQUAL_UINT8(1,
      amaran_light::nextZhiyunRoutingSelector(data));
  TEST_ASSERT_TRUE(store.save(data));
  amaran_light::MeshStoreData loaded;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::ConfigLoadStatus::Loaded),
                        static_cast<int>(store.load(loaded)));
  const amaran_light::MeshNodeRecord* loadedNode =
      amaran_light::findNode(loaded, 42);
  TEST_ASSERT_NOT_NULL(loadedNode);
  TEST_ASSERT_EQUAL_HEX16(0xc001, loadedNode->controlGroupAddress);
  TEST_ASSERT_EQUAL_HEX16(0x03f6, loadedNode->vendorCompanyId);
  TEST_ASSERT_EQUAL_HEX16(0x1000, loadedNode->vendorModelId);
  TEST_ASSERT_EQUAL_HEX16(
      0xc001, amaran_light::defaultControlGroupAddress(loaded, *loadedNode));
  const amaran_light::MeshNodeRecord fallbackGroupNode = {
      44, studio::DriverId::AmaranLight, 4};
  TEST_ASSERT_EQUAL_HEX16(
      0xc003,
      amaran_light::defaultControlGroupAddress(loaded, fallbackGroupNode));
  const amaran_light::MeshNodeRecord* loadedZhiyun =
      amaran_light::findNode(loaded, 43);
  TEST_ASSERT_NOT_NULL(loadedZhiyun);
  TEST_ASSERT_EQUAL_UINT8(0, loadedZhiyun->routingSelector);
  amaran_light::SequenceAllocator first;
  TEST_ASSERT_TRUE(first.begin(store, loaded));
  uint32_t sequence = 99;
  TEST_ASSERT_TRUE(first.next(sequence));
  TEST_ASSERT_EQUAL_UINT32(0, sequence);
  amaran_light::MeshStoreData restarted;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::ConfigLoadStatus::Loaded),
                        static_cast<int>(store.load(restarted)));
  amaran_light::SequenceAllocator second;
  TEST_ASSERT_TRUE(second.begin(store, restarted));
  TEST_ASSERT_TRUE(second.next(sequence));
  TEST_ASSERT_EQUAL_UINT32(amaran_light::kSequenceBlockSize, sequence);
}

void test_mesh_v1_store_migrates_zhiyun_routing_selectors() {
  V1MeshBackend backend;
  amaran_light::MeshStore store(backend);
  amaran_light::MeshStoreData data;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::ConfigLoadStatus::Loaded),
      static_cast<int>(store.load(data)));
  TEST_ASSERT_EQUAL_UINT8(2, data.nodeCount);
  const amaran_light::MeshNodeRecord* first =
      amaran_light::findNode(data, 41);
  const amaran_light::MeshNodeRecord* second =
      amaran_light::findNode(data, 42);
  TEST_ASSERT_NOT_NULL(first);
  TEST_ASSERT_NOT_NULL(second);
  TEST_ASSERT_EQUAL_UINT8(0, first->routingSelector);
  TEST_ASSERT_EQUAL_UINT8(1, second->routingSelector);
  TEST_ASSERT_EQUAL_HEX16(0, first->controlGroupAddress);
  TEST_ASSERT_EQUAL_UINT32(256, data.network.sequenceHighWater);
}

void test_zhiyun_x100_frames_and_confirmed_state_replies() {
  const zhiyun_x100::FrameBytes brightnessRead =
      zhiyun_x100::buildReadRequest(6, zhiyun_x100::kCommandBrightness);
  const uint8_t expectedRead[] = {
      0x24,0x3c,0x0d,0x00,0x00,0x01,0x06,0x00,0x01,0x10,
      0x00,0x80,0x00,0x00,0x00,0x00,0x00,0x36,0xf0};
  TEST_ASSERT_EQUAL_UINT32(sizeof(expectedRead), brightnessRead.length);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expectedRead, brightnessRead.bytes,
                                sizeof(expectedRead));

  const zhiyun_x100::FrameBytes powerOff =
      zhiyun_x100::buildPowerWrite(0x0172, false);
  const uint8_t expectedPowerOff[] = {
      0x24,0x3c,0x0a,0x00,0x00,0x01,0x72,0x01,0x08,0x10,
      0x00,0x80,0x01,0x00,0x9b,0x6d};
  TEST_ASSERT_EQUAL_UINT32(sizeof(expectedPowerOff), powerOff.length);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expectedPowerOff, powerOff.bytes,
                                sizeof(expectedPowerOff));

  const uint8_t brightnessReply[] = {
      0x24,0x3c,0x0d,0x00,0x01,0x00,0x06,0x00,0x01,0x10,
      0x00,0x80,0x00,0x00,0x00,0x40,0x40,0x28,0xf3};
  zhiyun_x100::FrameScanner scanner;
  unsigned callbacks = 0;
  scanner.feed(brightnessReply, 7,
               [&](const zhiyun_x100::ParsedFrame&) { ++callbacks; });
  scanner.feed(brightnessReply + 7, sizeof(brightnessReply) - 7,
               [&](const zhiyun_x100::ParsedFrame& frame) {
                 ++callbacks;
                 float brightness = -1.0f;
                 TEST_ASSERT_TRUE(zhiyun_x100::parseBrightness(frame,
                                                               brightness));
                 TEST_ASSERT_FLOAT_WITHIN(0.001f, 3.0f, brightness);
                 TEST_ASSERT_EQUAL_UINT16(6, frame.sequence);
               });
  TEST_ASSERT_EQUAL_UINT32(1, callbacks);
  TEST_ASSERT_TRUE(zhiyun_x100::validCctCommand(2700, 0, 0));
  TEST_ASSERT_TRUE(zhiyun_x100::validCctCommand(6500, 100, 0));
  TEST_ASSERT_FALSE(zhiyun_x100::validCctCommand(5600, 50, 1));
  TEST_ASSERT_EQUAL_UINT16(4500, zhiyun_x100::normalizeCct(4549));
  TEST_ASSERT_EQUAL_UINT16(4600, zhiyun_x100::normalizeCct(4550));
  const zhiyun_x100::FrameBytes capturedCct =
      zhiyun_x100::buildCctWrite(0x000c, 5450);
  const uint8_t expectedCct[] = {
      0x24,0x3c,0x0b,0x00,0x00,0x01,0x0c,0x00,0x02,0x10,
      0x00,0x80,0x01,0x4a,0x15,0xa9,0xea};
  TEST_ASSERT_EQUAL_UINT32(sizeof(expectedCct), capturedCct.length);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expectedCct, capturedCct.bytes,
                                sizeof(expectedCct));
  TEST_ASSERT_EQUAL_UINT32(0,
      zhiyun_x100::buildCctWrite(1, 2699).length);

  const zhiyun_x100::FrameBytes x60Cct =
      zhiyun_x100::buildCctWrite(0x000b, 5100, 1);
  const uint8_t expectedX60Cct[] = {
      0x24,0x3c,0x0b,0x00,0x00,0x01,0x0b,0x00,0x02,0x10,
      0x01,0x80,0x01,0xec,0x13,0x4d,0x26};
  TEST_ASSERT_EQUAL_UINT32(sizeof(expectedX60Cct), x60Cct.length);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expectedX60Cct, x60Cct.bytes,
                                sizeof(expectedX60Cct));
  const zhiyun_x100::FrameBytes x60Hue =
      zhiyun_x100::buildHueWrite(0x013b, 240.0f);
  const uint8_t expectedX60Hue[] = {
      0x24,0x3c,0x0d,0x00,0x00,0x01,0x3b,0x01,0x04,0x10,
      0x01,0x80,0x01,0x00,0x00,0x70,0x43,0xb6,0x5e};
  TEST_ASSERT_EQUAL_UINT32(sizeof(expectedX60Hue), x60Hue.length);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expectedX60Hue, x60Hue.bytes,
                                sizeof(expectedX60Hue));
  const uint8_t x60SaturationReply[] = {
      0x24,0x3c,0x0d,0x00,0x01,0x00,0x39,0x01,0x05,0x10,
      0x01,0x80,0x01,0x00,0x00,0xc8,0x42,0xb5,0xd4};
  bool parsedSaturation = false;
  scanner.feed(x60SaturationReply, sizeof(x60SaturationReply),
               [&](const zhiyun_x100::ParsedFrame& frame) {
                 float saturation = -1.0f;
                 parsedSaturation = zhiyun_x100::parseSaturation(
                     frame, saturation, 1, true);
                 TEST_ASSERT_FLOAT_WITHIN(0.001f, 100.0f, saturation);
               });
  TEST_ASSERT_TRUE(parsedSaturation);
  uint16_t hue = 99;
  uint8_t saturation = 99;
  zhiyun_x100::rgbToHsv(0x0000ff, hue, saturation);
  TEST_ASSERT_EQUAL_UINT16(240, hue);
  TEST_ASSERT_EQUAL_UINT8(100, saturation);
  struct CapturedSwatch {
    uint32_t rgb;
    uint16_t hue;
  };
  const CapturedSwatch capturedSwatches[] = {
      {0xff0000, 0},   {0x0000ff, 240}, {0xff00ff, 300},
      {0x00ffff, 180}, {0xff8000, 30},  {0x00ff00, 120},
  };
  for (const CapturedSwatch& swatch : capturedSwatches) {
    zhiyun_x100::rgbToHsv(swatch.rgb, hue, saturation);
    TEST_ASSERT_EQUAL_UINT16(swatch.hue, hue);
    TEST_ASSERT_EQUAL_UINT8(100, saturation);
  }
}

void test_zhiyun_x100_identity_and_advertisement_match() {
  const uint8_t identityReply[] = {
      0x24,0x3c,0x22,0x00,0x01,0x00,0x02,0x00,0x03,0x20,
      0x30,0x39,0x30,0x32,0x30,0x37,0x65,0x30,0x63,0x33,
      0x31,0x32,0x30,0x32,0x35,0x30,0x00,0x70,0x6c,0x31,
      0x30,0x35,0x00,0x00,0x00,0x00,0x00,0x00,
      0xf3,0xc6};
  zhiyun_x100::FrameScanner scanner;
  bool identified = false;
  scanner.feed(identityReply, sizeof(identityReply),
               [&](const zhiyun_x100::ParsedFrame& frame) {
                 identified = zhiyun_x100::identityIsX100(frame);
               });
  TEST_ASSERT_TRUE(identified);
  const uint8_t x60IdentityReply[] = {
      0x24,0x3c,0x22,0x00,0x01,0x00,0x02,0x00,0x03,0x20,
      0x30,0x39,0x63,0x37,0x30,0x63,0x34,0x30,0x65,0x35,
      0x31,0x32,0x30,0x30,0x37,0x31,0x00,0x70,0x6c,0x78,
      0x31,0x30,0x34,0x00,0x00,0x00,0x00,0x00,0xce,0xd6};
  bool identifiedX60 = false;
  scanner.feed(x60IdentityReply, sizeof(x60IdentityReply),
               [&](const zhiyun_x100::ParsedFrame& frame) {
                 identifiedX60 =
                     zhiyun_x100::identityContains(frame, "plx104");
               });
  TEST_ASSERT_TRUE(identifiedX60);
  const studio::ble::Advertisement x100 =
      bleAdvertisement("44:55:66:77:88:99", "PL105_4BF3", 0x1828);
  TEST_ASSERT_TRUE(zhiyun_x100::matchesAdvertisement(x100));
  const studio::ble::Advertisement unprovisioned =
      bleAdvertisement("44:55:66:77:88:99", "PL105_4BF3", 0x1827);
  TEST_ASSERT_FALSE(zhiyun_x100::matchesAdvertisement(unprovisioned));
  TEST_ASSERT_TRUE(
      zhiyun_x100::matchesUnprovisionedAdvertisement(unprovisioned));
  const studio::ble::Advertisement x60 =
      bleAdvertisement("55:66:77:88:99:aa", "X104_C957", 0x1828);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(zhiyun_x100::MolusModel::X60Rgb),
      static_cast<int>(zhiyun_x100::advertisementModel(x60)));
  TEST_ASSERT_TRUE(zhiyun_x100::matchesMolusAdvertisement(x60, true));
  studio::ble::Advertisement manufacturerOnly =
      bleAdvertisement("44:55:66:77:88:99", "Other", 0x1827);
  const uint8_t manufacturer[] = {
      8, 0xff, 0x05, 0x09, 'p', 'l', '1', '0', '5'};
  std::memcpy(manufacturerOnly.payload + manufacturerOnly.payloadLength,
              manufacturer, sizeof(manufacturer));
  manufacturerOnly.payloadLength += sizeof(manufacturer);
  TEST_ASSERT_TRUE(zhiyun_x100::matchesUnprovisionedAdvertisement(
      manufacturerOnly));
  manufacturerOnly.payload[manufacturerOnly.payloadLength - 1] = '6';
  TEST_ASSERT_FALSE(zhiyun_x100::matchesUnprovisionedAdvertisement(
      manufacturerOnly));
}

void test_mesh_no_oob_policy_accepts_x100_static_oob_capability() {
  const uint8_t x100Capabilities[] = {
      0x01, 0x01, 0x00, 0x01, 0x00, 0x01,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  TEST_ASSERT_TRUE(studio::mesh::supportsNoOobProvisioning(
      x100Capabilities, sizeof(x100Capabilities)));
  uint8_t unsupported[sizeof(x100Capabilities)];
  std::memcpy(unsupported, x100Capabilities, sizeof(unsupported));
  unsupported[4] = 1;
  TEST_ASSERT_FALSE(studio::mesh::supportsNoOobProvisioning(
      unsupported, sizeof(unsupported)));
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
  RUN_TEST(test_canon_trigger_pairing_name_and_trigger_bytes);
  RUN_TEST(test_canon_trigger_state_tracks_trigger_outcome);
  RUN_TEST(test_canon_state_requires_camera_notifications);
  RUN_TEST(test_tascam_cobs_commands_match_capture);
  RUN_TEST(test_tascam_scanner_and_confirmed_state);
  RUN_TEST(test_driver_catalog_exposes_shark_and_canon);
  RUN_TEST(test_registry_crud_and_single_shark_limit);
  RUN_TEST(test_transactional_add_commits_only_after_pairing_and_readiness);
  RUN_TEST(test_transactional_add_cancel_and_failed_save_do_not_register_device);
  RUN_TEST(test_config_round_trip_preserves_dormant_records_and_detects_corruption);
  RUN_TEST(test_home_assistant_config_is_separate_checksummed_and_local_only);
  RUN_TEST(test_panel_settings_default_round_trip_corruption_and_rollback);
  RUN_TEST(test_v1_device_blob_migrates_without_changing_ble_identity);
  RUN_TEST(test_home_assistant_profiles_protocol_capacity_and_scene_validation);
  RUN_TEST(test_mixed_scene_activates_physical_transport_before_home_assistant);
  RUN_TEST(test_manager_migrates_legacy_without_boot_activation);
  RUN_TEST(test_manager_routes_commands_and_blocks_disabled_device);
  RUN_TEST(test_manager_routes_to_canon_driver);
  RUN_TEST(test_manager_routes_explicit_tascam_record_commands);
  RUN_TEST(test_removed_registry_stays_empty_after_restart);
  RUN_TEST(test_admin_mutations_roll_back_when_persistence_fails);
  RUN_TEST(test_manager_holds_concurrent_active_links);
  RUN_TEST(test_manager_retains_ready_sessions_and_evicts_safe_lru);
  RUN_TEST(test_manager_counts_shared_mesh_as_one_ble_slot);
  RUN_TEST(test_manager_cancels_unready_release_and_reuses_ready_session);
  RUN_TEST(test_manager_parks_ownerless_drop_but_keeps_intentional_offline);
  RUN_TEST(test_scene_store_round_trip_and_corruption);
  RUN_TEST(test_scene_v1_migration_zeroes_action_arguments);
  RUN_TEST(test_press_record_start_and_authored_stop);
  RUN_TEST(test_partial_start_failure_can_stop_and_restart);
  RUN_TEST(test_prepare_ready_then_start_from_held_links);
  RUN_TEST(test_ble_central_lazy_lifetime_and_slot_exhaustion);
  RUN_TEST(test_ble_central_timing_and_readiness_reset_on_release);
  RUN_TEST(test_ble_central_shared_scan_claims_and_independent_release);
  RUN_TEST(test_ble_central_concurrent_links_retry_watchdog_and_security);
  RUN_TEST(test_ble_central_uses_bounded_scan_bursts);
  RUN_TEST(test_ble_central_parser_bonds_and_queue_overflow);
  RUN_TEST(test_ble_device_advertisement_matchers);
  RUN_TEST(test_amaran_crypto_and_network_vectors);
  RUN_TEST(test_amaran_access_payloads_and_validation);
  RUN_TEST(test_amaran_store_and_sequence_reservation_survive_restart);
  RUN_TEST(test_mesh_v1_store_migrates_zhiyun_routing_selectors);
  RUN_TEST(test_zhiyun_x100_frames_and_confirmed_state_replies);
  RUN_TEST(test_zhiyun_x100_identity_and_advertisement_match);
  RUN_TEST(test_mesh_no_oob_policy_accepts_x100_static_oob_capability);
  return UNITY_END();
}
