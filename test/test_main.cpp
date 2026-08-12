#include <unity.h>
#include <ArduinoJson.h>

#include <cmath>
#include <cstdio>
#include <cstring>

#include "core/ble/ble_central.h"
#include "core/ble/fake_ble_backend.h"
#include "core/config_store.h"
#include "core/command_traits.h"
#include "core/device_driver.h"
#include "core/device_manager.h"
#include "core/driver_catalog.h"
#include "core/home_assistant_config.h"
#include "core/panel_settings.h"
#include "core/panel_identity.h"
#include "core/scene_service.h"
#include "core/scene_store.h"
#include "devices/canon_ble/ble_match.h"
#include "devices/canon_ble/protocol.h"
#include "devices/canon_ble/state.h"
#include "devices/canon_camera_name.h"
#include "devices/canon_trigger/ble_match.h"
#include "devices/canon_trigger/protocol.h"
#include "devices/canon_trigger/state.h"
#include "devices/shark_nano_ii/ble_match.h"
#include "devices/shark_nano_ii/protocol.h"
#include "devices/shark_nano_ii/state.h"
#include "devices/tascam_x8/ble_match.h"
#include "devices/tascam_x8/protocol.h"
#include "devices/tascam_x8/state.h"
#include "devices/gopro/protocol.h"
#include "devices/gopro/state.h"
#include "devices/insta360/protocol.h"
#include "devices/insta360/state.h"
#include "devices/dji_osmo/protocol.h"
#include "devices/home_assistant/protocol.h"
#include "devices/aputure_light/crypto.h"
#include "devices/aputure_light/protocol.h"
#include "devices/aputure_light/state.h"
#include "devices/aputure_light/store.h"
#include "devices/zhiyun_x100/ble_match.h"
#include "devices/zhiyun_x100/protocol.h"
#include "devices/zhiyun_x100/state.h"
#include "portal_scene_parser.h"
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

class V2SceneBackend : public studio::IConfigBackend {
 public:
  V2SceneBackend() {
    uint8_t* out = data_;
    std::memcpy(out, "SCN1", 4); out += 4;
    put32(out, 0x01010002);  // version 2, initialized, one scene
    put32(out, 2);
    put32(out, 1);
    *out++ = 1;
    std::memset(out, 0, studio::kDeviceNameCapacity);
    std::memcpy(out, "Legacy v2", 9); out += studio::kDeviceNameCapacity;
    *out++ = 1; *out++ = 1;
    for (size_t list = 0; list < 2; ++list) {
      for (size_t step = 0; step < CONFIG_MAX_SCENE_STEPS; ++step) {
        const bool populated = step == 0;
        *out++ = static_cast<uint8_t>(populated
            ? studio::SceneStepType::Action : studio::SceneStepType::Wait);
        put32(out, populated ? (list == 0 ? 7 : 9) : 0);
        *out++ = static_cast<uint8_t>(populated
            ? (list == 0 ? studio::CommandType::RecordStart
                         : studio::CommandType::TurnOff)
            : studio::CommandType::Refresh);
        put32(out, 0);
        put32(out, populated ? 11 : 0);
        put32(out, populated ? 22 : 0);
        put32(out, populated ? 33 : 0);
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
  static uint32_t checksum(const uint8_t* data, size_t length) {
    uint32_t value = 2166136261u;
    for (size_t i = 0; i < length; ++i) {
      value ^= data[i]; value *= 16777619u;
    }
    return value;
  }

  uint8_t data_[1024] = {};
  size_t length_ = 0;
};

class V3SceneBackend : public studio::IConfigBackend {
 public:
  V3SceneBackend() {
    uint8_t* out = data_;
    std::memcpy(out, "SCN1", 4); out += 4;
    put16(out, 3);
    *out++ = 1;
    put32(out, 1);
    put32(out, 2);
    put32(out, 1);
    *out++ = 1;
    std::memset(out, 0, studio::kDeviceNameCapacity);
    std::memcpy(out, "Legacy v3", 9); out += studio::kDeviceNameCapacity;
    *out++ = 1; *out++ = 1;
    encodeAction(out, 7, studio::CommandType::RecordStart, 11, 22, 33);
    encodeAction(out, 9, studio::CommandType::TurnOff, 44, 55, 66);
    put32(out, checksum(data_, static_cast<size_t>(out - data_)));
    length_ = static_cast<size_t>(out - data_);
  }

  size_t read(uint8_t* destination, size_t capacity) override {
    if (capacity < length_) return 0;
    std::memcpy(destination, data_, length_); return length_;
  }
  bool write(const uint8_t*, size_t) override { return false; }

 private:
  static void put16(uint8_t*& out, uint16_t value) {
    *out++ = static_cast<uint8_t>(value);
    *out++ = static_cast<uint8_t>(value >> 8);
  }
  static void put32(uint8_t*& out, uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8) *out++ = value >> shift;
  }
  static void encodeAction(uint8_t*& out, studio::InstanceId target,
                           studio::CommandType command, int32_t value0,
                           int32_t value1, int32_t value2) {
    *out++ = static_cast<uint8_t>(studio::SceneStepType::Action);
    put32(out, target);
    *out++ = static_cast<uint8_t>(command);
    put32(out, 0);
    put32(out, static_cast<uint32_t>(value0));
    put32(out, static_cast<uint32_t>(value1));
    put32(out, static_cast<uint32_t>(value2));
  }
  static uint32_t checksum(const uint8_t* data, size_t length) {
    uint32_t value = 2166136261u;
    for (size_t i = 0; i < length; ++i) {
      value ^= data[i]; value *= 16777619u;
    }
    return value;
  }

  uint8_t data_[256] = {};
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
    if (command.type == pendingCommand) commandPending = true;
    return studio::CommandStatus::Succeeded;
  }
  void cancelPendingCommand(studio::InstanceId) override {
    ++cancelPendingCount;
    commandPending = false;
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
  studio::CommandType pendingCommand = studio::CommandType::Refresh;
  int cancelPendingCount = 0;
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

void test_canon_camera_names_and_identity_address_types() {
  char displayName[studio::kDeviceNameCapacity] = "";
  TEST_ASSERT_TRUE(canon_camera::canonicalDisplayName(
      "EOSR6m2_D4D530", displayName, sizeof(displayName)));
  TEST_ASSERT_EQUAL_STRING("Canon EOS R6 Mark II", displayName);
  TEST_ASSERT_TRUE(canon_camera::canonicalDisplayName(
      "EOSR6m3_ABC123", displayName, sizeof(displayName)));
  TEST_ASSERT_EQUAL_STRING("Canon EOS R6 Mark III", displayName);
  TEST_ASSERT_FALSE(canon_camera::canonicalDisplayName(
      "Canon (Smart)", displayName, sizeof(displayName)));
  TEST_ASSERT_EQUAL_UINT8(0, studio::ble::identityAddressType(2));
  TEST_ASSERT_EQUAL_UINT8(1, studio::ble::identityAddressType(3));
  TEST_ASSERT_EQUAL_UINT8(1, studio::ble::identityAddressType(1));
}

void test_canon_trigger_state_tracks_trigger_outcome() {
  canon_trigger::CanonTriggerState state;
  state.claimedPeerVisible = true;
  canon_trigger::resetTransientState(state);
  TEST_ASSERT_FALSE(state.claimedPeerVisible);
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
  TEST_ASSERT_EQUAL_UINT32(12, studio::DriverCatalog::count());
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
  const studio::DriverDescriptor* aputure =
      studio::DriverCatalog::find(studio::DriverId::AputureLight);
  TEST_ASSERT_NOT_NULL(aputure);
  TEST_ASSERT_EQUAL_STRING("aputure.light", aputure->stableId);
  TEST_ASSERT_EQUAL_STRING("Aputure", aputure->brand);
  TEST_ASSERT_EQUAL_STRING("Aputure Light", aputure->model);
  TEST_ASSERT_TRUE(aputure->discoverable);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::DeviceType::Light),
                        static_cast<int>(aputure->type));
  TEST_ASSERT_BITS_HIGH(
      studio::capabilityBit(studio::Capability::SetLightCct) |
          studio::capabilityBit(studio::Capability::SetLightRgb),
      aputure->capabilities);
  TEST_ASSERT_BITS_HIGH(
      studio::capabilityBit(studio::Capability::TurnOn) |
          studio::capabilityBit(studio::Capability::TurnOff),
      aputure->capabilities);
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
  const studio::DriverDescriptor* goPro =
      studio::DriverCatalog::find(studio::DriverId::GoPro);
  TEST_ASSERT_NOT_NULL(goPro);
  TEST_ASSERT_EQUAL_STRING("GoPro", goPro->model);
  TEST_ASSERT_EQUAL_UINT8(4, goPro->maxInstances);
  TEST_ASSERT_BITS_HIGH(
      studio::capabilityBit(studio::Capability::RecordStart) |
          studio::capabilityBit(studio::Capability::RecordStop),
      goPro->capabilities);
  TEST_ASSERT_BITS_LOW(
      studio::capabilityBit(studio::Capability::RecordingState),
      goPro->capabilities);
  const studio::DriverDescriptor* insta360Driver =
      studio::DriverCatalog::find(studio::DriverId::Insta360);
  TEST_ASSERT_NOT_NULL(insta360Driver);
  TEST_ASSERT_EQUAL_STRING("Insta360", insta360Driver->model);
  TEST_ASSERT_BITS_HIGH(
      studio::capabilityBit(studio::Capability::RecordStart) |
          studio::capabilityBit(studio::Capability::RecordStop) |
          studio::capabilityBit(studio::Capability::RecordingState),
      insta360Driver->capabilities);
  TEST_ASSERT_BITS_LOW(
      studio::capabilityBit(studio::Capability::RecordTrigger),
      insta360Driver->capabilities);
  TEST_ASSERT_EQUAL_STRING(
      "DJI Osmo", studio::DriverCatalog::find(studio::DriverId::DjiOsmo)->model);
  TEST_ASSERT_EQUAL_STRING(
      "Sony Camera", studio::DriverCatalog::find(studio::DriverId::SonyCamera)->model);
  TEST_ASSERT_EQUAL_STRING(
      "Phone Camera", studio::DriverCatalog::find(studio::DriverId::PhoneCamera)->model);
  TEST_ASSERT_EQUAL_STRING(
      "phone.camera.hid",
      studio::DriverCatalog::find(studio::DriverId::PhoneCamera)->stableId);
  TEST_ASSERT_BITS_HIGH(
      studio::capabilityBit(studio::Capability::RecordTrigger),
      studio::DriverCatalog::find(studio::DriverId::PhoneCamera)->capabilities);
  TEST_ASSERT_NULL(studio::DriverCatalog::find(static_cast<studio::DriverId>(99)));
}

void test_manager_keeps_every_compiled_camera_driver_reachable() {
  MemoryBackend backend;
  LegacyBackend legacy;
  FakeDriver shark(studio::DriverId::SharkNanoII);
  FakeDriver canonSmart(studio::DriverId::CanonBle);
  FakeDriver tascam(studio::DriverId::TascamX8);
  FakeDriver canonTrigger(studio::DriverId::CanonTrigger);
  FakeDriver homeAssistant(studio::DriverId::HomeAssistant);
  FakeDriver aputure(studio::DriverId::AputureLight);
  FakeDriver zhiyun(studio::DriverId::ZhiyunLight);
  FakeDriver goPro(studio::DriverId::GoPro);
  FakeDriver insta360(studio::DriverId::Insta360);
  FakeDriver dji(studio::DriverId::DjiOsmo);
  FakeDriver sony(studio::DriverId::SonyCamera);
  FakeDriver phone(studio::DriverId::PhoneCamera);
  studio::DeviceDriver* drivers[] = {
      &shark, &canonSmart, &tascam, &canonTrigger, &homeAssistant,
      &aputure, &zhiyun, &goPro, &insta360, &dji,
      &sony, &phone};
  studio::DeviceManager manager(backend, legacy, drivers,
                                sizeof(drivers) / sizeof(drivers[0]));
  TEST_ASSERT_TRUE(manager.begin());

  studio::InstanceId instanceId = studio::kInvalidInstanceId;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(manager.beginAdd(studio::DriverId::PhoneCamera,
                                        "Phone Camera", instanceId)));
  TEST_ASSERT_TRUE(
      manager.acquire(instanceId, studio::ConnectionOwner::Foreground));
  TEST_ASSERT_TRUE(
      manager.ownedBy(instanceId, studio::ConnectionOwner::Foreground));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(manager.cancelPendingAdd(instanceId)));
}

void test_insta360_explicit_recording_is_available_to_scenes() {
  MemoryBackend deviceBackend;
  MemoryBackend sceneBackend;
  LegacyBackend legacy;
  FakeDriver insta360(studio::DriverId::Insta360);
  studio::DeviceDriver* drivers[] = {&insta360};
  studio::DeviceManager devices(deviceBackend, legacy, drivers, 1);
  TEST_ASSERT_TRUE(devices.begin());

  studio::InstanceId cameraId = studio::kInvalidInstanceId;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(devices.add(studio::DriverId::Insta360, "Insta360",
                                   cameraId)));
  studio::SceneService scenes(sceneBackend, devices);
  TEST_ASSERT_TRUE(scenes.begin());
  studio::SceneId sceneId = studio::kInvalidSceneId;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::SceneRegistryStatus::Ok),
      static_cast<int>(scenes.add("Insta360 camera", sceneId)));
  studio::SceneRecord record = *scenes.find(sceneId);
  record.startCount = 1;
  record.startSteps[0] =
      studio::makeActionStep(cameraId, studio::CommandType::RecordStart);
  record.stopCount = 1;
  record.stopSteps[0] =
      studio::makeActionStep(cameraId, studio::CommandType::RecordStop);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::SceneRegistryStatus::Ok),
      static_cast<int>(scenes.replace(record)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::SceneValidationStatus::Ok),
                        static_cast<int>(scenes.validate(sceneId)));

  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::SceneRunStatus::Ok),
                        static_cast<int>(scenes.start(sceneId)));
  for (uint32_t now = 1; now < 20; ++now) {
    devices.loop();
    scenes.loop(now);
  }
  TEST_ASSERT_EQUAL_INT(1, insta360.dispatchCount);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::CommandType::RecordStart),
                        static_cast<int>(insta360.lastCommand));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::SceneRunStatus::Ok),
                        static_cast<int>(scenes.stop()));
  for (uint32_t now = 20; now < 40; ++now) {
    devices.loop();
    scenes.loop(now);
  }
  TEST_ASSERT_EQUAL_INT(2, insta360.dispatchCount);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::CommandType::RecordStop),
                        static_cast<int>(insta360.lastCommand));
}

void test_gopro_open_ble_packets_and_optimistic_state() {
  const gopro::Packet start = gopro::buildSetShutter(true);
  const uint8_t expectedStart[] = {0x03, 0x01, 0x01, 0x01};
  TEST_ASSERT_EQUAL_UINT32(sizeof(expectedStart), start.len);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expectedStart, start.bytes, start.len);
  const gopro::Packet stop = gopro::buildSetShutter(false);
  const uint8_t expectedStop[] = {0x03, 0x01, 0x01, 0x00};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expectedStop, stop.bytes, stop.len);
  const gopro::Packet pairing = gopro::buildSetPairingState();
  const uint8_t expectedPairing[] = {0x03, 0x17, 0x01, 0x01};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expectedPairing, pairing.bytes, pairing.len);

  const uint8_t ok[] = {0x02, 0x01, 0x00};
  const gopro::Response response = gopro::parseResponse(ok, sizeof(ok));
  TEST_ASSERT_TRUE(response.valid);
  TEST_ASSERT_EQUAL_HEX8(gopro::kSetShutterCommand, response.command);
  TEST_ASSERT_EQUAL_HEX8(gopro::kSuccessStatus, response.status);
  TEST_ASSERT_FALSE(gopro::parseResponse(ok, 2).valid);

  gopro::GoProState state;
  gopro::markCommandQueued(state, true);
  TEST_ASSERT_TRUE(state.commandPending);
  gopro::reduceCommandResponse(state, true, gopro::kSuccessStatus);
  TEST_ASSERT_FALSE(state.commandPending);
  TEST_ASSERT_FALSE(state.lastCommandFailed);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(gopro::GoProState::Recording::Recording),
                        static_cast<int>(state.recording));
  gopro::markCommandQueued(state, false);
  gopro::reduceCommandResponse(state, false, 0x02);
  TEST_ASSERT_TRUE(state.lastCommandFailed);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(gopro::GoProState::Recording::Unknown),
                        static_cast<int>(state.recording));
}

void test_dji_osmo_protocol_matches_official_connection_vector() {
  const uint8_t address[] = {0x38, 0x34, 0x56, 0x78, 0x9a, 0xbc};
  const dji_osmo::Packet packet = dji_osmo::buildConnectionRequest(
      1, 0x12345678, address, 0, 0x1ffe);
  const uint8_t expected[] = {
      0xaa,0x33,0x00,0x02,0x00,0x00,0x00,0x00,0x01,0x00,0x98,0x2f,0x00,0x19,
      0x78,0x56,0x34,0x12,0x06,0x38,0x34,0x56,0x78,0x9a,0xbc,0x00,0x00,0x00,
      0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
      0xfe,0x1f,0x00,0x00,0x00,0x00,0x79,0x6c,0x66,0x7c};
  TEST_ASSERT_EQUAL_UINT32(sizeof(expected), packet.len);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, packet.bytes, packet.len);
  const auto frame = dji_osmo::parseFrame(packet.bytes, packet.len);
  TEST_ASSERT_TRUE(frame.valid);
  TEST_ASSERT_EQUAL_UINT8(dji_osmo::kCmdConnection, frame.commandId);

  const auto approval = dji_osmo::buildConnectionRequest(
      9, 0x0000ff44, address, 2, 0);
  const auto parsedApproval =
      dji_osmo::parseFrame(approval.bytes, approval.len);
  bool approved = false;
  TEST_ASSERT_TRUE(dji_osmo::parseConnectionApproval(parsedApproval, approved));
  TEST_ASSERT_TRUE(approved);

  const auto firstPair = dji_osmo::buildConnectionRequest(
      2, 0x12345678, address, 1, 42);
  const auto parsedFirstPair =
      dji_osmo::parseFrame(firstPair.bytes, firstPair.len);
  TEST_ASSERT_EQUAL_UINT8(1, parsedFirstPair.payload[26]);
  TEST_ASSERT_EQUAL_UINT8(42, parsedFirstPair.payload[27]);
  TEST_ASSERT_EQUAL_UINT8(0, parsedFirstPair.payload[28]);

  const auto secondCamera = dji_osmo::buildConnectionResponse(
      3, 0x12345678, 2);
  const auto parsedSecondCamera =
      dji_osmo::parseFrame(secondCamera.bytes, secondCamera.len);
  TEST_ASSERT_TRUE(parsedSecondCamera.valid);
  TEST_ASSERT_EQUAL_UINT8(2, parsedSecondCamera.payload[5]);
  TEST_ASSERT_EQUAL_UINT8(0, parsedSecondCamera.payload[6]);
  TEST_ASSERT_EQUAL_UINT8(0, parsedSecondCamera.payload[7]);
  TEST_ASSERT_EQUAL_UINT8(0, parsedSecondCamera.payload[8]);

  bool recording = true;
  TEST_ASSERT_TRUE(dji_osmo::decodeCameraRecordingStatus(0x01, 0x00,
                                                         recording));
  TEST_ASSERT_FALSE(recording);
  TEST_ASSERT_TRUE(dji_osmo::decodeCameraRecordingStatus(0x38, 0x03,
                                                         recording));
  TEST_ASSERT_TRUE(recording);
  TEST_ASSERT_TRUE(dji_osmo::decodeCameraRecordingStatus(0x3f, 0x03,
                                                         recording));
  TEST_ASSERT_FALSE(recording);
  TEST_ASSERT_FALSE(dji_osmo::decodeCameraRecordingStatus(0x01, 0xff,
                                                          recording));

  const auto start = dji_osmo::buildRecordControl(7, true);
  const auto parsedStart = dji_osmo::parseFrame(start.bytes, start.len);
  TEST_ASSERT_TRUE(parsedStart.valid);
  TEST_ASSERT_EQUAL_UINT8(dji_osmo::kCmdRecord, parsedStart.commandId);
  TEST_ASSERT_EQUAL_UINT8(0, parsedStart.payload[4]);
}

void test_insta360_gps_remote_protocol() {
  TEST_ASSERT_TRUE(insta360::matchesCameraName("X5 123456"));
  TEST_ASSERT_TRUE(insta360::matchesCameraName("GO 3 654321"));
  TEST_ASSERT_TRUE(insta360::matchesCameraName("GO Ultra 123456"));
  TEST_ASSERT_TRUE(insta360::isGoUltra("Insta360 GO Ultra"));
  TEST_ASSERT_FALSE(insta360::isGoUltra("GO 3 654321"));
  TEST_ASSERT_EQUAL_STRING(insta360::kNotifyUuid,
                           insta360::kGattCharacteristicOrder[0]);
  TEST_ASSERT_EQUAL_STRING(insta360::kWriteUuid,
                           insta360::kGattCharacteristicOrder[1]);
  TEST_ASSERT_EQUAL_STRING(insta360::kInfoUuid,
                           insta360::kGattCharacteristicOrder[2]);
  TEST_ASSERT_EQUAL_STRING("Insta360 Remote (Bleep)",
                           insta360::kAdvertisedName);
  TEST_ASSERT_EQUAL_UINT16(0x0180, insta360::kAdvertisedAppearance);
  const uint8_t advertisement[] = {
      0x02,0x01,0x06,0x18,0x09,
      'I','n','s','t','a','3','6','0',' ','R','e','m','o','t','e',' ',
      '(','B','l','e','e','p',')'};
  TEST_ASSERT_EQUAL_UINT32(28, sizeof(insta360::kAdvertisementData));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(advertisement,
                                insta360::kAdvertisementData,
                                sizeof(advertisement));
  const uint8_t scanResponse[] = {
      0x03,0x19,0x80,0x01,0x03,0x03,0x80,0xce};
  TEST_ASSERT_EQUAL_UINT32(8, sizeof(insta360::kScanResponseData));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(scanResponse,
                                insta360::kScanResponseData,
                                sizeof(scanResponse));
  uint8_t wakeAdvertisement[insta360::kWakeAdvertisementDataLength] = {};
  uint8_t wakeScanResponse[insta360::kWakeScanResponseDataLength] = {};
  TEST_ASSERT_TRUE(insta360::buildWakeAdvertisementData(
      "X5 1HDKAB", wakeAdvertisement, wakeScanResponse));
  const uint8_t capturedWakeAdvertisement[] = {
      0x02,0x01,0x06,0x1b,0xff,0x4c,0x00,0x02,
      0x15,0x09,'O','R','B','I','T',0x09,
      0xff,0x0f,0x00,'1','H','D','K','A','B',
      0x00,0x00,0x00,0x00,0xe4,0x01};
  const uint8_t capturedWakeScanResponse[] = {0x02,0x0a,0x00};
  TEST_ASSERT_EQUAL_UINT32(sizeof(capturedWakeAdvertisement),
                           insta360::kWakeAdvertisementDataLength);
  TEST_ASSERT_EQUAL_UINT32(sizeof(capturedWakeScanResponse),
                           insta360::kWakeScanResponseDataLength);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(capturedWakeAdvertisement,
                                wakeAdvertisement,
                                sizeof(capturedWakeAdvertisement));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(capturedWakeScanResponse,
                                wakeScanResponse,
                                sizeof(capturedWakeScanResponse));
  TEST_ASSERT_FALSE(insta360::buildWakeAdvertisementData(
      nullptr, wakeAdvertisement, wakeScanResponse));
  TEST_ASSERT_FALSE(insta360::buildWakeAdvertisementData(
      "X5", wakeAdvertisement, wakeScanResponse));
  TEST_ASSERT_FALSE(insta360::buildWakeAdvertisementData(
      "X5 ABC", wakeAdvertisement, wakeScanResponse));
  TEST_ASSERT_FALSE(insta360::buildWakeAdvertisementData(
      "X5 ABCDEFG", wakeAdvertisement, wakeScanResponse));
  TEST_ASSERT_FALSE(insta360::buildWakeAdvertisementData(
      "X5 AB-CD1", wakeAdvertisement, wakeScanResponse));
  TEST_ASSERT_FALSE(insta360::buildWakeAdvertisementData(
      "Camera ABC123", wakeAdvertisement, wakeScanResponse));
  const uint8_t expected[] = {0xfc,0xef,0xfe,0x86,0x00,0x03,0x01,0x02,0x00};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, insta360::kShutterCommand,
                                sizeof(expected));
  const uint8_t powerOff[] = {0xfc,0xef,0xfe,0x86,0x00,0x03,0x01,0x00,0x03};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(powerOff, insta360::kPowerOffCommand,
                                sizeof(powerOff));

  insta360::State provisional;
  insta360::assumeVideoIdle(provisional);
  TEST_ASSERT_TRUE(insta360::canStartRecording(provisional));
  TEST_ASSERT_FALSE(insta360::canStopRecording(provisional));
  TEST_ASSERT_FALSE(provisional.recordingConfirmed);
  provisional.recording = insta360::State::Recording::Recording;
  provisional.recordingConfirmed = true;
  TEST_ASSERT_FALSE(insta360::canStartRecording(provisional));
  TEST_ASSERT_TRUE(insta360::canStopRecording(provisional));

  insta360::CaptureStatus status;
  const uint8_t gpsVideoIdle[] = {
      0xfe,0xef,0xfe,0x10,0x80,0x07,0x01,0x2c,0x46,0x01,'3','5','m'};
  const uint8_t gpsRecording[] = {
      0xfe,0xef,0xfe,0x10,0x80,0x0d,0x01,0x0e,0x46,0x01,'.',
      '0','0',':','0','0',':','0','0'};
  const uint8_t gpsVideoIdleHours[] = {
      0xfe,0xef,0xfe,0x10,0x80,0x07,0x01,0x2c,0x46,0x01,
      '1','h','0','5','m'};
  const uint8_t gpsPhotoIdle[] = {
      0xfe,0xef,0xfe,0x10,0x80,0x09,0x01,0x1e,0x46,0x01,' ',
      '9','9','9','+'};
  const uint8_t gpsPhotoSaving[] = {
      0xfe,0xef,0xfe,0x10,0x80,0x05,0x01,0x34,0x2c,0x02,'5'};
  TEST_ASSERT_TRUE(insta360::decodeCaptureStatus(
      gpsVideoIdle, sizeof(gpsVideoIdle), status));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(insta360::CaptureMode::Video),
                        static_cast<int>(status.mode));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(insta360::CapturePhase::Idle),
                        static_cast<int>(status.phase));
  TEST_ASSERT_TRUE(insta360::decodeCaptureStatus(
      gpsVideoIdleHours, sizeof(gpsVideoIdleHours), status));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(insta360::CaptureMode::Video),
                        static_cast<int>(status.mode));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(insta360::CapturePhase::Idle),
                        static_cast<int>(status.phase));
  uint8_t invalidGpsIdle[sizeof(gpsVideoIdleHours)];
  std::memcpy(invalidGpsIdle, gpsVideoIdleHours, sizeof(invalidGpsIdle));
  invalidGpsIdle[11] = '?';
  TEST_ASSERT_FALSE(insta360::decodeCaptureStatus(
      invalidGpsIdle, sizeof(invalidGpsIdle), status));
  TEST_ASSERT_TRUE(insta360::decodeCaptureStatus(
      gpsRecording, sizeof(gpsRecording), status));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(insta360::CapturePhase::Active),
                        static_cast<int>(status.phase));
  TEST_ASSERT_TRUE(insta360::decodeCaptureStatus(
      gpsPhotoIdle, sizeof(gpsPhotoIdle), status));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(insta360::CaptureMode::Photo),
                        static_cast<int>(status.mode));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(insta360::CapturePhase::Idle),
                        static_cast<int>(status.phase));
  TEST_ASSERT_TRUE(insta360::decodeCaptureStatus(
      gpsPhotoSaving, sizeof(gpsPhotoSaving), status));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(insta360::CapturePhase::Saving),
                        static_cast<int>(status.phase));

  const uint8_t videoStarting[] = {0xfe,0xef,0xfe,0x55,0x00,0x07,0x00,0x01,0,0,0,0,0};
  const uint8_t videoRecording[] = {0xfe,0xef,0xfe,0x55,0x00,0x07,0x00,0x02,0,0,0,0,0};
  const uint8_t videoStopping[] = {0xfe,0xef,0xfe,0x55,0x00,0x07,0x00,0x04,0,0,0,0,0};
  const uint8_t videoStopped[] = {0xfe,0xef,0xfe,0x55,0x00,0x07,0x00,0x00,0,0,0,0,0};
  TEST_ASSERT_TRUE(insta360::decodeCaptureStatus(
      videoStarting, sizeof(videoStarting), status));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(insta360::CaptureMode::Video),
                        static_cast<int>(status.mode));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(insta360::CapturePhase::Starting),
                        static_cast<int>(status.phase));
  TEST_ASSERT_TRUE(insta360::decodeCaptureStatus(
      videoRecording, sizeof(videoRecording), status));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(insta360::CapturePhase::Active),
                        static_cast<int>(status.phase));
  TEST_ASSERT_TRUE(insta360::decodeCaptureStatus(
      videoStopping, sizeof(videoStopping), status));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(insta360::CapturePhase::Stopping),
                        static_cast<int>(status.phase));
  TEST_ASSERT_TRUE(insta360::decodeCaptureStatus(
      videoStopped, sizeof(videoStopped), status));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(insta360::CapturePhase::Idle),
                        static_cast<int>(status.phase));

  const uint8_t photoSaving[] = {0xfe,0xef,0xfe,0x55,0x00,0x07,0x01,0x05,0,0,0,0,0};
  TEST_ASSERT_TRUE(insta360::decodeCaptureStatus(
      photoSaving, sizeof(photoSaving), status));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(insta360::CaptureMode::Photo),
                        static_cast<int>(status.mode));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(insta360::CapturePhase::Saving),
                        static_cast<int>(status.phase));
  TEST_ASSERT_FALSE(insta360::decodeCaptureStatus(
      photoSaving, sizeof(photoSaving) - 1, status));
  uint8_t invalid[sizeof(photoSaving)];
  std::memcpy(invalid, photoSaving, sizeof(invalid));
  invalid[3] = 0x56;
  TEST_ASSERT_FALSE(insta360::decodeCaptureStatus(
      invalid, sizeof(invalid), status));

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

void test_config_round_trip_at_twenty_four_record_capacity() {
  MemoryBackend backend;
  studio::ConfigStore store(backend);
  studio::DeviceRecord records[CONFIG_MAX_DEVICE_INSTANCES] = {};
  for (size_t i = 0; i < CONFIG_MAX_DEVICE_INSTANCES; ++i) {
    records[i].instanceId = static_cast<studio::InstanceId>(i + 1);
    records[i].driverId = static_cast<studio::DriverId>(100 + i);
    records[i].enabled = (i % 2) == 0;
    records[i].paired = (i % 3) == 0;
    std::strcpy(records[i].displayName, "Dormant device");
  }

  studio::DeviceRegistry source;
  TEST_ASSERT_EQUAL_UINT32(24, source.capacity());
  TEST_ASSERT_TRUE(source.restore(records, CONFIG_MAX_DEVICE_INSTANCES,
                                  CONFIG_MAX_DEVICE_INSTANCES + 1, true));
  TEST_ASSERT_TRUE(store.save(source));

  studio::DeviceRegistry restored;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::ConfigLoadStatus::Loaded),
      static_cast<int>(store.load(restored)));
  TEST_ASSERT_EQUAL_UINT32(CONFIG_MAX_DEVICE_INSTANCES, restored.count());
  TEST_ASSERT_EQUAL_UINT32(CONFIG_MAX_DEVICE_INSTANCES,
                           restored.at(CONFIG_MAX_DEVICE_INSTANCES - 1)
                               ->instanceId);
  TEST_ASSERT_EQUAL_INT(
      100 + CONFIG_MAX_DEVICE_INSTANCES - 1,
      static_cast<int>(
          restored.at(CONFIG_MAX_DEVICE_INSTANCES - 1)->driverId));
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

void test_panel_identity_uses_full_hardware_id_and_short_setup_suffix() {
  char identity[studio::kPanelIdentityCapacity] = "";
  char setupSsid[studio::kPanelSetupSsidCapacity] = "";
  char otherSetupSsid[studio::kPanelSetupSsidCapacity] = "";
  studio::formatPanelIdentity(0x0010A1B2C3D4E5F6ULL, identity);
  studio::formatPanelSetupSsid(0x0010A1B2C3D4E5F6ULL, setupSsid);
  TEST_ASSERT_EQUAL_STRING("BLP-A1B2C3D4E5F6", identity);
  TEST_ASSERT_EQUAL_STRING("Bleep-Setup-4E5F6", setupSsid);
  studio::formatPanelSetupSsid(0x0010A1B2C3D5E6F7ULL, otherSetupSsid);
  TEST_ASSERT_EQUAL_STRING("Bleep-Setup-5E6F7", otherSetupSsid);
  TEST_ASSERT_NOT_EQUAL(0, std::strcmp(setupSsid, otherSetupSsid));

  studio::formatPanelIdentity(0, identity);
  studio::formatPanelSetupSsid(0, setupSsid);
  TEST_ASSERT_EQUAL_STRING("BLP-000000000000", identity);
  TEST_ASSERT_EQUAL_STRING("Bleep-Setup-00000", setupSsid);
}

void test_portal_parses_sequence_action_commands_from_json() {
  const char* payload = R"JSON({"start":[{"kind":"action","target_id":11,"command":"turn_on","value0":0,"value1":0,"value2":0,"label":"Turn on"},{"kind":"wait","wait_ms":200},{"kind":"action","target_id":12,"command":"record_start","value0":0,"value1":0,"value2":0,"label":"Record start"},{"kind":"action","target_id":10,"command":"record_start","value0":0,"value1":0,"value2":0,"label":"Record start"},{"kind":"wait","wait_ms":200},{"kind":"action","target_id":13,"command":"record_start","value0":0,"value1":0,"value2":0,"label":"Record start"}]})JSON";
  JsonDocument request;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(DeserializationError::Ok),
      static_cast<int>(deserializeJson(request, payload).code()));
  studio::SceneStep steps[CONFIG_MAX_SCENE_STEPS] = {};
  uint8_t count = 0;
  const portal::StepParseResult result =
      portal::parseSceneSteps(request["start"], steps, count);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(portal::StepParseStatus::Ok),
                        static_cast<int>(result.status));
  TEST_ASSERT_EQUAL_UINT8(6, count);
  TEST_ASSERT_EQUAL_UINT32(11, steps[0].targetId);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::CommandType::TurnOn),
                        static_cast<int>(steps[0].command));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::SceneStepType::Wait),
                        static_cast<int>(steps[1].type));
  TEST_ASSERT_EQUAL_UINT32(200, steps[1].waitMs);
  TEST_ASSERT_EQUAL_UINT32(12, steps[2].targetId);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::CommandType::RecordStart),
                        static_cast<int>(steps[2].command));
  TEST_ASSERT_EQUAL_UINT32(10, steps[3].targetId);
  TEST_ASSERT_EQUAL_UINT32(13, steps[5].targetId);

  request["start"][2]["command"] = "wrong_params";
  const portal::StepParseResult invalid =
      portal::parseSceneSteps(request["start"], steps, count);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(portal::StepParseStatus::InvalidCommand),
      static_cast<int>(invalid.status));
  TEST_ASSERT_EQUAL_UINT8(2, invalid.index);
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
  TEST_ASSERT_TRUE(home_assistant::matchesEntitySearch(
      "light.key_light", "Key Light", "KEY LIGHT"));
  TEST_ASSERT_TRUE(home_assistant::matchesEntitySearch(
      "light.key_light", "Key Light", "LIGHT.KEY"));
  TEST_ASSERT_TRUE(home_assistant::matchesEntitySearch(
      "light.key_light", "KEY LIGHT", "key light"));
  TEST_ASSERT_TRUE(home_assistant::matchesEntitySearch(
      "light.key_light", nullptr, ""));
  TEST_ASSERT_FALSE(home_assistant::matchesEntitySearch(
      "light.key_light", "Key Light", "camera"));
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
  FakeDriver retainedLightDriver(studio::DriverId::AputureLight);
  int activationSequence = 0;
  physicalDriver.activationSequence = &activationSequence;
  homeAssistantDriver.activationSequence = &activationSequence;
  studio::DeviceDriver* drivers[] = {&physicalDriver, &homeAssistantDriver,
                                     &retainedLightDriver};
  studio::DeviceManager devices(deviceBackend, legacy, drivers, 3);
  TEST_ASSERT_TRUE(devices.begin());

  studio::InstanceId cameraId = studio::kInvalidInstanceId;
  studio::InstanceId helperId = studio::kInvalidInstanceId;
  studio::InstanceId lightId = studio::kInvalidInstanceId;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(devices.add(studio::DriverId::CanonBle, "Camera",
                                   cameraId)));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(devices.addHomeAssistantEntity(
          studio::HomeAssistantDomain::InputBoolean, "input_boolean.live",
          "Live", helperId)));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(devices.add(studio::DriverId::AputureLight, "Old light",
                                   lightId)));

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
  // Because HA is a target, it stays pooled while the physical target starts.
  TEST_ASSERT_TRUE(
      devices.acquire(helperId, studio::ConnectionOwner::Foreground));
  devices.release(helperId, studio::ConnectionOwner::Foreground);
  TEST_ASSERT_TRUE(devices.isActive(helperId));
  // Reproduce switching from a protocol-ready lights scene. It is not a target
  // of the mixed studio scene, but capacity remains and the pool retains it.
  TEST_ASSERT_TRUE(
      devices.acquire(lightId, studio::ConnectionOwner::Sequence));
  devices.loop();
  devices.release(lightId, studio::ConnectionOwner::Sequence);
  TEST_ASSERT_TRUE(devices.isRetained(lightId));
  activationSequence = 0;
  physicalDriver.firstActivationOrder = 0;
  homeAssistantDriver.firstActivationOrder = 0;
  physicalDriver.ready = false;

  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::SceneRunStatus::Ok),
                        static_cast<int>(scenes.prepare(sceneId)));
  TEST_ASSERT_EQUAL_INT(0, homeAssistantDriver.deactivationCount);
  TEST_ASSERT_EQUAL_INT(0, retainedLightDriver.deactivationCount);
  TEST_ASSERT_TRUE(devices.isActive(lightId));
  TEST_ASSERT_EQUAL_INT(0, physicalDriver.deactivationCount);
  TEST_ASSERT_EQUAL_INT(1, physicalDriver.firstActivationOrder);
  TEST_ASSERT_EQUAL_INT(0, homeAssistantDriver.firstActivationOrder);
  scenes.loop(1);
  TEST_ASSERT_EQUAL_INT(0, homeAssistantDriver.firstActivationOrder);
  scenes.loop(1000);
  TEST_ASSERT_EQUAL_INT(0, homeAssistantDriver.firstActivationOrder);
  physicalDriver.ready = true;
  scenes.loop(1001);
  TEST_ASSERT_EQUAL_INT(0, homeAssistantDriver.firstActivationOrder);
  TEST_ASSERT_EQUAL_INT(0, homeAssistantDriver.firstActivationOrder);
  TEST_ASSERT_TRUE(
      devices.ownedBy(helperId, studio::ConnectionOwner::Sequence));
  scenes.cancel();
}

void test_scene_switch_retains_old_only_links_when_four_resources_fit() {
  MemoryBackend deviceBackend;
  MemoryBackend sceneBackend;
  LegacyBackend legacy;
  FakeDriver cameraDriver(studio::DriverId::CanonBle);
  FakeDriver recorderDriver(studio::DriverId::TascamX8);
  FakeDriver lightDriver(studio::DriverId::ZhiyunLight);
  FakeDriver homeAssistantDriver(studio::DriverId::HomeAssistant);
  studio::DeviceDriver* drivers[] = {&cameraDriver, &recorderDriver,
                                     &lightDriver, &homeAssistantDriver};
  studio::DeviceManager devices(deviceBackend, legacy, drivers, 4);
  TEST_ASSERT_TRUE(devices.begin());

  studio::InstanceId cameraId = studio::kInvalidInstanceId;
  studio::InstanceId recorderId = studio::kInvalidInstanceId;
  studio::InstanceId lightId = studio::kInvalidInstanceId;
  studio::InstanceId helperId = studio::kInvalidInstanceId;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(devices.add(studio::DriverId::CanonBle, "Camera",
                                   cameraId)));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(devices.add(studio::DriverId::TascamX8, "Recorder",
                                   recorderId)));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(devices.add(studio::DriverId::ZhiyunLight, "Light",
                                   lightId)));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(devices.addHomeAssistantEntity(
          studio::HomeAssistantDomain::InputBoolean, "input_boolean.live",
          "Live", helperId)));

  studio::SceneService scenes(sceneBackend, devices);
  TEST_ASSERT_TRUE(scenes.begin());
  studio::SceneId firstId = studio::kInvalidSceneId;
  studio::SceneId secondId = studio::kInvalidSceneId;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::SceneRegistryStatus::Ok),
      static_cast<int>(scenes.add("First", firstId)));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::SceneRegistryStatus::Ok),
      static_cast<int>(scenes.add("Second", secondId)));
  studio::SceneRecord first = *scenes.find(firstId);
  first.startCount = 3;
  first.startSteps[0] =
      studio::makeActionStep(cameraId, studio::CommandType::RecordStart);
  first.startSteps[1] =
      studio::makeActionStep(recorderId, studio::CommandType::RecordStart);
  first.startSteps[2] =
      studio::makeActionStep(helperId, studio::CommandType::TurnOn);
  first.stopCount = 3;
  first.stopSteps[0] =
      studio::makeActionStep(cameraId, studio::CommandType::RecordStop);
  first.stopSteps[1] =
      studio::makeActionStep(recorderId, studio::CommandType::RecordStop);
  first.stopSteps[2] =
      studio::makeActionStep(helperId, studio::CommandType::TurnOff);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::SceneRegistryStatus::Ok),
      static_cast<int>(scenes.replace(first)));

  studio::SceneRecord second = *scenes.find(secondId);
  second.startCount = 2;
  second.startSteps[0] =
      studio::makeActionStep(lightId, studio::CommandType::TurnOn);
  second.startSteps[1] =
      studio::makeActionStep(helperId, studio::CommandType::TurnOn);
  second.stopCount = 2;
  second.stopSteps[0] =
      studio::makeActionStep(lightId, studio::CommandType::TurnOff);
  second.stopSteps[1] =
      studio::makeActionStep(helperId, studio::CommandType::TurnOff);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::SceneRegistryStatus::Ok),
      static_cast<int>(scenes.replace(second)));

  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::SceneRunStatus::Ok),
                        static_cast<int>(scenes.prepare(firstId)));
  scenes.loop(1);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::ScenePhase::Ready),
                        static_cast<int>(scenes.progress().phase));
  TEST_ASSERT_EQUAL_INT(1, cameraDriver.activationCount);
  TEST_ASSERT_EQUAL_INT(1, recorderDriver.activationCount);
  TEST_ASSERT_EQUAL_INT(0, lightDriver.activationCount);
  TEST_ASSERT_EQUAL_INT(1, homeAssistantDriver.activationCount);

  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::SceneRunStatus::Ok),
                        static_cast<int>(scenes.prepare(secondId)));
  TEST_ASSERT_EQUAL_INT(0, cameraDriver.deactivationCount);
  TEST_ASSERT_EQUAL_INT(0, recorderDriver.deactivationCount);
  TEST_ASSERT_EQUAL_INT(0, lightDriver.deactivationCount);
  TEST_ASSERT_EQUAL_INT(0, homeAssistantDriver.deactivationCount);
  TEST_ASSERT_EQUAL_INT(1, cameraDriver.activationCount);
  TEST_ASSERT_EQUAL_INT(1, recorderDriver.activationCount);
  TEST_ASSERT_EQUAL_INT(1, lightDriver.activationCount);
  TEST_ASSERT_EQUAL_INT(1, homeAssistantDriver.activationCount);
  scenes.loop(252);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::ScenePhase::Ready),
                        static_cast<int>(scenes.progress().phase));
  TEST_ASSERT_TRUE(
      devices.ownedBy(helperId, studio::ConnectionOwner::Sequence));
  TEST_ASSERT_EQUAL_UINT32(4, devices.activeCount());
  TEST_ASSERT_TRUE(devices.isActive(cameraId));
  TEST_ASSERT_TRUE(devices.isActive(recorderId));
  TEST_ASSERT_TRUE(devices.isActive(lightId));
  scenes.cancel();
}

void test_mixed_scene_gives_physical_and_home_assistant_separate_timeouts() {
  MemoryBackend deviceBackend;
  MemoryBackend sceneBackend;
  LegacyBackend legacy;
  FakeDriver physicalDriver(studio::DriverId::CanonBle);
  FakeDriver homeAssistantDriver(studio::DriverId::HomeAssistant);
  physicalDriver.ready = false;
  homeAssistantDriver.ready = false;
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
      static_cast<int>(scenes.add("Cold mixed", sceneId)));
  studio::SceneRecord record = *scenes.find(sceneId);
  record.startCount = 2;
  record.startSteps[0] =
      studio::makeActionStep(cameraId, studio::CommandType::RecordStart);
  record.startSteps[1] =
      studio::makeActionStep(helperId, studio::CommandType::TurnOn);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::SceneRegistryStatus::Ok),
      static_cast<int>(scenes.replace(record)));

  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::SceneRunStatus::Ok),
                        static_cast<int>(scenes.prepare(sceneId)));
  scenes.loop(1);
  scenes.loop(CONFIG_SCENE_CONNECT_TIMEOUT_MS + 2);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::ScenePhase::Connecting),
                        static_cast<int>(scenes.progress().phase));

  constexpr uint32_t physicalReadyAt =
      CONFIG_SCENE_PHYSICAL_CONNECT_TIMEOUT_MS - 1000;
  physicalDriver.ready = true;
  scenes.loop(physicalReadyAt);
  scenes.loop(physicalReadyAt + 250);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::ScenePhase::Connecting),
                        static_cast<int>(scenes.progress().phase));
  TEST_ASSERT_TRUE(devices.isActive(helperId));

  scenes.loop(physicalReadyAt + CONFIG_SCENE_CONNECT_TIMEOUT_MS - 1);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::ScenePhase::Connecting),
                        static_cast<int>(scenes.progress().phase));
  scenes.loop(physicalReadyAt + CONFIG_SCENE_CONNECT_TIMEOUT_MS);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::ScenePhase::Failed),
                        static_cast<int>(scenes.progress().phase));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::SceneRunStatus::ConnectTimeout),
                        static_cast<int>(scenes.progress().lastStatus));
  scenes.cancel();
}

void test_prepared_scene_edit_preserves_shared_ha_while_adding_ble_target() {
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
      static_cast<int>(scenes.add("Editable", sceneId)));
  studio::SceneRecord record = *scenes.find(sceneId);
  record.startCount = 1;
  record.startSteps[0] =
      studio::makeActionStep(helperId, studio::CommandType::TurnOn);
  record.stopCount = 1;
  record.stopSteps[0] =
      studio::makeActionStep(helperId, studio::CommandType::TurnOff);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::SceneRegistryStatus::Ok),
      static_cast<int>(scenes.replace(record)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::SceneRunStatus::Ok),
                        static_cast<int>(scenes.prepare(sceneId)));
  scenes.loop(1);
  TEST_ASSERT_TRUE(
      devices.ownedBy(helperId, studio::ConnectionOwner::Sequence));

  activationSequence = 0;
  physicalDriver.firstActivationOrder = 0;
  homeAssistantDriver.firstActivationOrder = 0;
  record.startCount = 2;
  record.startSteps[1] =
      studio::makeActionStep(cameraId, studio::CommandType::RecordStart);
  record.stopCount = 2;
  record.stopSteps[1] =
      studio::makeActionStep(cameraId, studio::CommandType::RecordStop);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::SceneRegistryStatus::Ok),
      static_cast<int>(scenes.replace(record)));

  TEST_ASSERT_EQUAL_INT(0, homeAssistantDriver.deactivationCount);
  TEST_ASSERT_EQUAL_INT(1, physicalDriver.firstActivationOrder);
  TEST_ASSERT_EQUAL_INT(0, homeAssistantDriver.firstActivationOrder);
  TEST_ASSERT_TRUE(
      devices.ownedBy(cameraId, studio::ConnectionOwner::Sequence));
  TEST_ASSERT_TRUE(
      devices.ownedBy(helperId, studio::ConnectionOwner::Sequence));
  scenes.loop(2);
  TEST_ASSERT_EQUAL_INT(0, homeAssistantDriver.firstActivationOrder);
  scenes.loop(252);
  TEST_ASSERT_EQUAL_INT(0, homeAssistantDriver.firstActivationOrder);
  TEST_ASSERT_TRUE(
      devices.ownedBy(helperId, studio::ConnectionOwner::Sequence));
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

void test_manager_starts_empty_without_legacy_shark() {
  MemoryBackend backend;
  LegacyBackend legacy;
  FakeDriver driver;
  studio::DeviceDriver* drivers[] = {&driver};
  studio::DeviceManager manager(backend, legacy, drivers, 1);

  TEST_ASSERT_TRUE(manager.begin());
  TEST_ASSERT_EQUAL_UINT32(0, manager.count());

  studio::DeviceManager restarted(backend, legacy, drivers, 1);
  TEST_ASSERT_TRUE(restarted.begin());
  TEST_ASSERT_EQUAL_UINT32(0, restarted.count());
}

void test_manager_removes_old_unpaired_default_shark_only() {
  MemoryBackend backend;
  studio::ConfigStore store(backend);
  studio::DeviceRegistry registry;
  studio::InstanceId defaultId = studio::kInvalidInstanceId;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(registry.add(studio::DriverId::SharkNanoII,
                                    "Shark Nano II", 1, defaultId)));
  TEST_ASSERT_TRUE(store.save(registry));

  LegacyBackend legacy;
  FakeDriver driver;
  studio::DeviceDriver* drivers[] = {&driver};
  studio::DeviceManager manager(backend, legacy, drivers, 1);
  TEST_ASSERT_TRUE(manager.begin());
  TEST_ASSERT_EQUAL_UINT32(0, manager.count());

  studio::DeviceManager restarted(backend, legacy, drivers, 1);
  TEST_ASSERT_TRUE(restarted.begin());
  TEST_ASSERT_EQUAL_UINT32(0, restarted.count());
}

void test_manager_preserves_renamed_unpaired_shark() {
  MemoryBackend backend;
  studio::ConfigStore store(backend);
  studio::DeviceRegistry registry;
  studio::InstanceId sharkId = studio::kInvalidInstanceId;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(registry.add(studio::DriverId::SharkNanoII,
                                    "Main Slider", 1, sharkId)));
  TEST_ASSERT_TRUE(store.save(registry));

  LegacyBackend legacy;
  FakeDriver driver;
  studio::DeviceDriver* drivers[] = {&driver};
  studio::DeviceManager manager(backend, legacy, drivers, 1);
  TEST_ASSERT_TRUE(manager.begin());
  TEST_ASSERT_EQUAL_UINT32(1, manager.count());
  TEST_ASSERT_EQUAL_STRING("Main Slider", manager.at(0)->displayName);
}

void test_manager_routes_commands_and_blocks_disabled_device() {
  MemoryBackend backend;
  LegacyBackend legacy;
  FakeDriver driver;
  studio::DeviceDriver* drivers[] = {&driver};
  studio::DeviceManager manager(backend, legacy, drivers, 1);
  TEST_ASSERT_TRUE(manager.begin());
  studio::InstanceId instanceId = studio::kInvalidInstanceId;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(manager.add(studio::DriverId::SharkNanoII,
                                   "Main Slider", instanceId)));
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

void test_manager_keeps_latest_tracked_result_when_result_queue_is_full() {
  MemoryBackend backend;
  LegacyBackend legacy;
  FakeDriver driver;
  studio::DeviceDriver* drivers[] = {&driver};
  studio::DeviceManager manager(backend, legacy, drivers, 1);
  TEST_ASSERT_TRUE(manager.begin());
  studio::InstanceId instanceId = studio::kInvalidInstanceId;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(manager.add(studio::DriverId::SharkNanoII,
                                   "Main Slider", instanceId)));
  TEST_ASSERT_TRUE(
      manager.acquire(instanceId, studio::ConnectionOwner::Foreground));

  studio::DeviceCommand command;
  command.instanceId = instanceId;
  command.type = studio::CommandType::Refresh;
  for (size_t i = 0; i < CONFIG_DEVICE_COMMAND_QUEUE_SIZE; ++i) {
    TEST_ASSERT_TRUE(manager.enqueue(command));
    manager.loop();
  }

  uint32_t trackedRequestId = 0;
  TEST_ASSERT_TRUE(manager.enqueue(command, &trackedRequestId));
  manager.loop();
  studio::CommandResult result;
  TEST_ASSERT_TRUE(manager.takeResult(trackedRequestId, result));
  TEST_ASSERT_EQUAL_UINT32(trackedRequestId, result.requestId);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::CommandStatus::Succeeded),
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
  studio::InstanceId removedId = studio::kInvalidInstanceId;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(first.add(studio::DriverId::SharkNanoII,
                                 "Main Slider", removedId)));
  TEST_ASSERT_EQUAL_UINT32(1, first.count());
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
  studio::InstanceId sharkId = studio::kInvalidInstanceId;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(manager.add(studio::DriverId::SharkNanoII,
                                   "Main Slider", sharkId)));
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
  FakeDriver meshDriver(studio::DriverId::AputureLight);
  FakeDriver zhiyunDriver(studio::DriverId::ZhiyunLight);
  meshDriver.sharedBleGroup = 1;
  meshDriver.sharedBleFamily = studio::DriverId::PanelOwnedMesh;
  zhiyunDriver.sharedBleGroup = 1;
  zhiyunDriver.sharedBleFamily = studio::DriverId::PanelOwnedMesh;
  studio::DeviceDriver* drivers[] = {
      &sharkDriver, &canonDriver, &tascamDriver, &meshDriver, &zhiyunDriver};
  studio::DeviceManager manager(backend, legacy, drivers, 5);
  TEST_ASSERT_TRUE(manager.begin());
  studio::InstanceId sharkId = studio::kInvalidInstanceId;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(manager.add(studio::DriverId::SharkNanoII,
                                   "Main Slider", sharkId)));
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
      static_cast<int>(manager.add(studio::DriverId::AputureLight,
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

void test_generated_stop_mapping_order_and_capacity() {
  studio::SceneRecord record;
  record.startCount = 8;
  record.startSteps[0] =
      studio::makeActionStep(1, studio::CommandType::RecordStart);
  record.startSteps[1] = studio::makeWaitStep(750);
  record.startSteps[2] =
      studio::makeActionStep(2, studio::CommandType::TurnOn);
  record.startSteps[3] =
      studio::makeActionStep(3, studio::CommandType::RecordTrigger, 1, 2, 3);
  record.startSteps[4] =
      studio::makeActionStep(4, studio::CommandType::Press);
  record.startSteps[5] =
      studio::makeActionStep(5, studio::CommandType::Activate);
  record.startSteps[6] =
      studio::makeActionStep(6, studio::CommandType::SetLightCctAndOn, 4300, 50);
  record.startSteps[7] =
      studio::makeActionStep(7, studio::CommandType::SetLightRgbAndOn, 0xff00ff, 75);
  studio::generateStopSteps(record);

  TEST_ASSERT_EQUAL_UINT8(8, record.stopCount);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::CommandType::TurnOff),
                        static_cast<int>(record.stopSteps[0].command));
  TEST_ASSERT_EQUAL_UINT32(7, record.stopSteps[0].targetId);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::CommandType::TurnOff),
                        static_cast<int>(record.stopSteps[1].command));
  TEST_ASSERT_EQUAL_UINT32(6, record.stopSteps[1].targetId);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::CommandType::Activate),
                        static_cast<int>(record.stopSteps[2].command));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::CommandType::Press),
                        static_cast<int>(record.stopSteps[3].command));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::CommandType::RecordTrigger),
                        static_cast<int>(record.stopSteps[4].command));
  TEST_ASSERT_EQUAL_INT32(1, record.stopSteps[4].value0);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::CommandType::TurnOff),
                        static_cast<int>(record.stopSteps[5].command));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::SceneStepType::Wait),
                        static_cast<int>(record.stopSteps[6].type));
  TEST_ASSERT_EQUAL_UINT32(750, record.stopSteps[6].waitMs);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::CommandType::RecordStop),
                        static_cast<int>(record.stopSteps[7].command));

  const uint32_t cctAndOn = studio::requiredCapabilities(
      studio::CommandType::SetLightCctAndOn);
  TEST_ASSERT_EQUAL_HEX32(
      studio::capabilityBit(studio::Capability::SetLightCct) |
          studio::capabilityBit(studio::Capability::TurnOn),
      cctAndOn);

  for (uint8_t i = 0; i < CONFIG_MAX_SCENE_STEPS; ++i) {
    record.startSteps[i] =
        studio::makeActionStep(i + 1, studio::CommandType::RecordTrigger);
  }
  record.startCount = CONFIG_MAX_SCENE_STEPS;
  studio::generateStopSteps(record);
  TEST_ASSERT_EQUAL_UINT8(CONFIG_MAX_SCENE_STEPS, record.stopCount);
  TEST_ASSERT_EQUAL_UINT32(CONFIG_MAX_SCENE_STEPS,
                           record.stopSteps[0].targetId);

  record.startCount = 2;
  record.startSteps[0] =
      studio::makeActionStep(1, studio::CommandType::RecordStop);
  record.startSteps[1] =
      studio::makeActionStep(2, studio::CommandType::TurnOff);
  studio::generateStopSteps(record);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::CommandType::TurnOn),
                        static_cast<int>(record.stopSteps[0].command));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::CommandType::RecordStart),
                        static_cast<int>(record.stopSteps[1].command));
}

void test_scene_stop_customization_and_relinking() {
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

  studio::SceneService scenes(sceneBackend, devices);
  TEST_ASSERT_TRUE(scenes.begin());
  studio::SceneId sceneId = studio::kInvalidSceneId;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::SceneRegistryStatus::Ok),
                        static_cast<int>(scenes.add("Modes", sceneId)));
  studio::SceneRecord record = *scenes.find(sceneId);
  record.startCount = 1;
  record.startSteps[0] =
      studio::makeActionStep(cameraId, studio::CommandType::RecordStart);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::SceneRegistryStatus::Ok),
                        static_cast<int>(scenes.replace(record)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::CommandType::RecordStop),
                        static_cast<int>(scenes.find(sceneId)->stopSteps[0].command));

  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::SceneRegistryStatus::Ok),
                        static_cast<int>(scenes.customizeStop(sceneId)));
  record = *scenes.find(sceneId);
  record.stopSteps[0] =
      studio::makeActionStep(cameraId, studio::CommandType::RecordStart);
  record.startSteps[record.startCount++] = studio::makeWaitStep(650);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::SceneRegistryStatus::Ok),
                        static_cast<int>(scenes.replace(record)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::SceneStopMode::Custom),
                        static_cast<int>(scenes.find(sceneId)->stopMode));
  TEST_ASSERT_EQUAL_UINT8(1, scenes.find(sceneId)->stopCount);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::CommandType::RecordStart),
                        static_cast<int>(scenes.find(sceneId)->stopSteps[0].command));

  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::SceneRegistryStatus::Ok),
                        static_cast<int>(scenes.useGeneratedStop(sceneId)));
  const studio::SceneRecord* relinked = scenes.find(sceneId);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::SceneStopMode::Generated),
                        static_cast<int>(relinked->stopMode));
  TEST_ASSERT_EQUAL_UINT8(2, relinked->stopCount);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::SceneStepType::Wait),
                        static_cast<int>(relinked->stopSteps[0].type));
  TEST_ASSERT_EQUAL_UINT32(650, relinked->stopSteps[0].waitMs);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::CommandType::RecordStop),
                        static_cast<int>(relinked->stopSteps[1].command));
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
      studio::makeActionStep(2, studio::CommandType::SetLightCctAndOn, 5600, 72, -125);
  record->startSteps[1] = studio::makeWaitStep(500);
  record->startSteps[2] =
      studio::makeActionStep(3, studio::CommandType::RecordStart);
  record->stopMode = studio::SceneStopMode::Custom;
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
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::SceneStopMode::Custom),
                        static_cast<int>(restored.at(0)->stopMode));
  TEST_ASSERT_EQUAL_UINT32(500, restored.at(0)->startSteps[1].waitMs);
  TEST_ASSERT_EQUAL_INT32(5600, restored.at(0)->startSteps[0].value0);
  TEST_ASSERT_EQUAL_INT32(72, restored.at(0)->startSteps[0].value1);
  TEST_ASSERT_EQUAL_INT32(-125, restored.at(0)->startSteps[0].value2);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::CommandType::SetLightCctAndOn),
      static_cast<int>(restored.at(0)->startSteps[0].command));

  backend.corruptLastByte();
  studio::SceneRegistry rejected;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::ConfigLoadStatus::Corrupt),
      static_cast<int>(store.load(rejected)));
}

void test_scene_registry_and_store_grow_beyond_legacy_limit() {
  MemoryBackend backend;
  studio::SceneStore store(backend);
  studio::SceneRegistry source;
  for (size_t i = 0; i < 12; ++i) {
    char name[studio::kDeviceNameCapacity];
    std::snprintf(name, sizeof(name), "Sequence %u",
                  static_cast<unsigned>(i + 1));
    studio::SceneId id = studio::kInvalidSceneId;
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(studio::SceneRegistryStatus::Ok),
        static_cast<int>(source.add(name, id)));
  }
  TEST_ASSERT_EQUAL_UINT32(12, source.count());
  TEST_ASSERT_TRUE(store.save(source));

  studio::SceneRegistry restored;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::ConfigLoadStatus::Loaded),
      static_cast<int>(store.load(restored)));
  TEST_ASSERT_EQUAL_UINT32(12, restored.count());
  TEST_ASSERT_EQUAL_STRING("Sequence 12", restored.at(11)->name);
}

void test_scene_v1_migration_zeroes_action_arguments() {
  V1SceneBackend backend;
  studio::SceneStore store(backend);
  studio::SceneRegistry restored;
  bool migrated = false;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::ConfigLoadStatus::Loaded),
                        static_cast<int>(store.load(restored, &migrated)));
  TEST_ASSERT_TRUE(migrated);
  TEST_ASSERT_EQUAL_UINT32(1, restored.count());
  const studio::SceneStep& step = restored.at(0)->startSteps[0];
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::CommandType::TurnOn),
                        static_cast<int>(step.command));
  TEST_ASSERT_EQUAL_INT32(0, step.value0);
  TEST_ASSERT_EQUAL_INT32(0, step.value1);
  TEST_ASSERT_EQUAL_INT32(0, step.value2);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::SceneStopMode::Generated),
                        static_cast<int>(restored.at(0)->stopMode));
  TEST_ASSERT_EQUAL_UINT8(1, restored.at(0)->stopCount);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::CommandType::TurnOff),
                        static_cast<int>(restored.at(0)->stopSteps[0].command));
}

void test_scene_v2_migration_discards_authored_stop() {
  V2SceneBackend backend;
  studio::SceneStore store(backend);
  studio::SceneRegistry restored;
  bool migrated = false;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::ConfigLoadStatus::Loaded),
                        static_cast<int>(store.load(restored, &migrated)));
  TEST_ASSERT_TRUE(migrated);
  TEST_ASSERT_EQUAL_UINT32(1, restored.count());
  const studio::SceneRecord* record = restored.at(0);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::SceneStopMode::Generated),
                        static_cast<int>(record->stopMode));
  TEST_ASSERT_EQUAL_UINT8(1, record->stopCount);
  TEST_ASSERT_EQUAL_UINT32(7, record->stopSteps[0].targetId);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::CommandType::RecordStop),
                        static_cast<int>(record->stopSteps[0].command));
  TEST_ASSERT_EQUAL_INT32(11, record->stopSteps[0].value0);
}

void test_scene_v3_migration_discards_authored_stop() {
  V3SceneBackend backend;
  studio::SceneStore store(backend);
  studio::SceneRegistry restored;
  bool migrated = false;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::ConfigLoadStatus::Loaded),
                        static_cast<int>(store.load(restored, &migrated)));
  TEST_ASSERT_TRUE(migrated);
  TEST_ASSERT_EQUAL_UINT32(1, restored.count());
  const studio::SceneRecord* record = restored.at(0);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::SceneStopMode::Generated),
                        static_cast<int>(record->stopMode));
  TEST_ASSERT_EQUAL_UINT8(1, record->startCount);
  TEST_ASSERT_EQUAL_UINT32(7, record->startSteps[0].targetId);
  TEST_ASSERT_EQUAL_INT32(11, record->startSteps[0].value0);
  TEST_ASSERT_EQUAL_INT32(22, record->startSteps[0].value1);
  TEST_ASSERT_EQUAL_INT32(33, record->startSteps[0].value2);
  TEST_ASSERT_EQUAL_UINT8(1, record->stopCount);
  TEST_ASSERT_EQUAL_UINT32(7, record->stopSteps[0].targetId);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::CommandType::RecordStop),
                        static_cast<int>(record->stopSteps[0].command));
  TEST_ASSERT_EQUAL_INT32(11, record->stopSteps[0].value0);
}

void test_orphaned_scene_steps_can_be_removed_one_at_a_time() {
  MemoryBackend deviceBackend;
  MemoryBackend sceneBackend;
  LegacyBackend legacy;
  FakeDriver driver(studio::DriverId::CanonBle);
  studio::DeviceDriver* drivers[] = {&driver};
  studio::DeviceManager devices(deviceBackend, legacy, drivers, 1);
  TEST_ASSERT_TRUE(devices.begin());

  studio::InstanceId oldCamera = studio::kInvalidInstanceId;
  studio::InstanceId newCamera = studio::kInvalidInstanceId;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(devices.add(studio::DriverId::CanonBle, "Old camera",
                                   oldCamera)));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(devices.add(studio::DriverId::CanonBle, "New camera",
                                   newCamera)));

  studio::SceneService scenes(sceneBackend, devices);
  TEST_ASSERT_TRUE(scenes.begin());
  studio::SceneId sceneId = studio::kInvalidSceneId;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::SceneRegistryStatus::Ok),
      static_cast<int>(scenes.add("Repair", sceneId)));
  studio::SceneRecord record = *scenes.find(sceneId);
  record.startCount = 1;
  record.startSteps[0] =
      studio::makeActionStep(oldCamera, studio::CommandType::RecordStart);
  record.stopMode = studio::SceneStopMode::Custom;
  record.stopCount = 1;
  record.stopSteps[0] =
      studio::makeActionStep(oldCamera, studio::CommandType::RecordStop);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::SceneRegistryStatus::Ok),
      static_cast<int>(scenes.replace(record)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::RegistryStatus::Ok),
                        static_cast<int>(devices.remove(oldCamera)));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::SceneValidationStatus::MissingTarget),
      static_cast<int>(scenes.validate(sceneId)));

  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::SceneRegistryStatus::Ok),
      static_cast<int>(scenes.removeStep(sceneId, true, 0)));
  TEST_ASSERT_EQUAL_UINT8(0, scenes.find(sceneId)->startCount);
  TEST_ASSERT_EQUAL_UINT8(1, scenes.find(sceneId)->stopCount);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::SceneValidationStatus::MissingTarget),
      static_cast<int>(scenes.validate(sceneId)));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::SceneRegistryStatus::Ok),
      static_cast<int>(scenes.removeStep(sceneId, false, 0)));
  TEST_ASSERT_EQUAL_UINT8(0, scenes.find(sceneId)->stopCount);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::SceneValidationStatus::Empty),
      static_cast<int>(scenes.validate(sceneId)));

  record = *scenes.find(sceneId);
  record.startCount = 1;
  record.startSteps[0] =
      studio::makeActionStep(newCamera, studio::CommandType::RecordStart);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::SceneRegistryStatus::Ok),
      static_cast<int>(scenes.replace(record)));
}

void test_press_record_start_and_generated_stop() {
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
  const studio::SceneRecord* seeded = scenes.find(sceneId);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::SceneStopMode::Generated),
                        static_cast<int>(seeded->stopMode));
  TEST_ASSERT_EQUAL_UINT8(3, seeded->stopCount);
  TEST_ASSERT_EQUAL_UINT32(tascamId, seeded->stopSteps[0].targetId);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::CommandType::RecordStop),
                        static_cast<int>(seeded->stopSteps[0].command));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::SceneStepType::Wait),
                        static_cast<int>(seeded->stopSteps[1].type));
  TEST_ASSERT_EQUAL_UINT32(500, seeded->stopSteps[1].waitMs);
  TEST_ASSERT_EQUAL_UINT32(canonId, seeded->stopSteps[2].targetId);
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
  for (uint32_t t = 600; t < 1150; ++t) {
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
  for (uint32_t t = 700; t < 1250; ++t) {
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
  for (uint32_t t = 1250; t < 2000; ++t) {
    devices.loop();
    scenes.loop(t);
  }
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::ScenePhase::IdleArmed),
                        static_cast<int>(scenes.progress().phase));
}

void test_stop_cancels_inflight_compound_light_action() {
  MemoryBackend deviceBackend;
  MemoryBackend sceneBackend;
  LegacyBackend legacy;
  FakeDriver lightDriver(studio::DriverId::ZhiyunLight);
  lightDriver.pendingCommand = studio::CommandType::SetLightCctAndOn;
  studio::DeviceDriver* drivers[] = {&lightDriver};
  studio::DeviceManager devices(deviceBackend, legacy, drivers, 1);
  TEST_ASSERT_TRUE(devices.begin());

  studio::InstanceId lightId = studio::kInvalidInstanceId;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(
          devices.add(studio::DriverId::ZhiyunLight, "Light", lightId)));

  studio::SceneService scenes(sceneBackend, devices);
  TEST_ASSERT_TRUE(scenes.begin());
  studio::SceneId sceneId = studio::kInvalidSceneId;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::SceneRegistryStatus::Ok),
      static_cast<int>(scenes.add("Light", sceneId)));
  studio::SceneRecord record = *scenes.find(sceneId);
  record.startCount = 1;
  record.startSteps[0] = studio::makeActionStep(
      lightId, studio::CommandType::SetLightCctAndOn, 5600, 50, 0);
  studio::generateStopSteps(record);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::SceneRegistryStatus::Ok),
      static_cast<int>(scenes.replace(record)));

  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::SceneRunStatus::Ok),
                        static_cast<int>(scenes.start(sceneId)));
  scenes.loop(0);
  devices.loop();
  scenes.loop(1);
  TEST_ASSERT_TRUE(lightDriver.commandPending);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::ScenePhase::RunningStart),
                        static_cast<int>(scenes.progress().phase));

  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::SceneRunStatus::Ok),
                        static_cast<int>(scenes.stop()));
  TEST_ASSERT_EQUAL_INT(1, lightDriver.cancelPendingCount);
  TEST_ASSERT_FALSE(lightDriver.commandPending);

  devices.loop();
  scenes.loop(2);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::CommandType::TurnOff),
                        static_cast<int>(lightDriver.lastCommand));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::ScenePhase::Completed),
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

void test_ble_central_pauses_scan_for_peripheral_gatt_mutation() {
  studio::ble::FakeBleBackend backend;
  studio::ble::BleCentral central(backend);
  BleTestDelegate delegate;
  const studio::ble::LinkHandle link = central.acquire(delegate, {});

  TEST_ASSERT_TRUE(central.requestScan(link));
  TEST_ASSERT_TRUE(backend.scanRunning());
  const uint32_t startsBeforePause = backend.scanStarts();
  central.pauseScanForGattMutation();
  TEST_ASSERT_FALSE(backend.scanRunning());
  TEST_ASSERT_EQUAL_UINT32(1, backend.scanStops());
  TEST_ASSERT_TRUE(central.scanning(link));

  central.loop(1);
  TEST_ASSERT_TRUE(backend.scanRunning());
  TEST_ASSERT_EQUAL_UINT32(startsBeforePause + 1, backend.scanStarts());
  central.release(link);
}

void test_ble_central_suspends_shared_scan_during_connect_and_security() {
  studio::ble::FakeBleBackend backend;
  studio::ble::BleCentral central(backend);
  BleTestDelegate connecting;
  BleTestDelegate scanning;
  studio::ble::ConnectPolicy policy;
  policy.security = studio::ble::SecurityPolicy::BondSecure;
  const studio::ble::LinkHandle connectingLink =
      central.acquire(connecting, policy);
  const studio::ble::LinkHandle scanningLink = central.acquire(scanning, {});
  connecting.central = &central;
  connecting.requestSecurityOnConnect = true;

  TEST_ASSERT_TRUE(central.requestScan(scanningLink));
  TEST_ASSERT_TRUE(backend.scanRunning());
  TEST_ASSERT_TRUE(central.requestConnect(
      connectingLink, bleAddress("11:22:33:44:55:66")));
  TEST_ASSERT_FALSE(backend.scanRunning());
  TEST_ASSERT_TRUE(central.scanning(scanningLink));

  studio::ble::Event connected;
  connected.type = studio::ble::EventType::Connected;
  connected.link = connectingLink;
  TEST_ASSERT_TRUE(backend.emit(connected));
  central.loop(100);
  TEST_ASSERT_TRUE(connecting.securityRequested);
  TEST_ASSERT_FALSE(backend.scanRunning());

  studio::ble::Event secured;
  secured.type = studio::ble::EventType::SecurityComplete;
  secured.link = connectingLink;
  secured.succeeded = true;
  TEST_ASSERT_TRUE(backend.emit(secured));
  central.loop(101);
  TEST_ASSERT_TRUE(backend.scanRunning());
  TEST_ASSERT_TRUE(central.scanning(scanningLink));

  central.release(connectingLink);
  central.release(scanningLink);
}

void test_ble_central_ignores_queued_event_from_evicted_link_generation() {
  studio::ble::FakeBleBackend backend;
  studio::ble::BleCentral central(backend);
  BleTestDelegate first;
  BleTestDelegate replacement;

  const studio::ble::LinkHandle oldLink = central.acquire(first, {});
  studio::ble::Event staleConnected;
  staleConnected.type = studio::ble::EventType::Connected;
  staleConnected.link = oldLink;
  TEST_ASSERT_TRUE(backend.emit(staleConnected));

  central.release(oldLink);
  const studio::ble::LinkHandle newLink = central.acquire(replacement, {});
  TEST_ASSERT_EQUAL_UINT32(oldLink, newLink);
  central.loop(10);
  TEST_ASSERT_EQUAL_UINT32(0, replacement.events);

  studio::ble::Event currentConnected;
  currentConnected.type = studio::ble::EventType::Connected;
  currentConnected.link = newLink;
  TEST_ASSERT_TRUE(backend.emit(currentConnected));
  central.loop(20);
  TEST_ASSERT_EQUAL_UINT32(1, replacement.events);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::ble::EventType::Connected),
                        static_cast<int>(replacement.lastEvent));
  central.release(newLink);
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
  TEST_ASSERT_FALSE(backend.scanRunning());
  TEST_ASSERT_TRUE(central.scanning(secondLink));
  TEST_ASSERT_FALSE(
      central.selectAdvertisement(secondLink, advertisement));

  studio::ble::Event connected;
  connected.type = studio::ble::EventType::Connected;
  connected.link = firstLink;
  TEST_ASSERT_TRUE(backend.emit(connected));
  central.loop(11);
  TEST_ASSERT_TRUE(backend.scanRunning());

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

void test_tascam_reconnect_scans_after_one_direct_failure() {
  studio::ble::FakeBleBackend backend;
  studio::ble::BleCentral central(backend);
  BleTestDelegate delegate;
  studio::ble::ConnectPolicy policy;
  policy.directAttemptsBeforeScan = tascam_x8::kDirectAttemptsBeforeScan;
  const studio::ble::LinkHandle link = central.acquire(delegate, policy);

  TEST_ASSERT_TRUE(
      central.requestConnect(link, bleAddress("11:22:33:44:55:66")));
  TEST_ASSERT_EQUAL_UINT32(1, backend.connectCalls(link));

  studio::ble::Event failed;
  failed.type = studio::ble::EventType::ConnectFailed;
  failed.link = link;
  backend.emit(failed);
  central.loop(1);
  central.loop(1500);
  TEST_ASSERT_FALSE(backend.scanRunning());
  central.loop(1501);
  TEST_ASSERT_TRUE(backend.scanRunning());
  TEST_ASSERT_EQUAL_UINT32(1, backend.connectCalls(link));
  central.release(link);
}

void test_ble_central_protocol_failure_can_stop_retry() {
  studio::ble::FakeBleBackend backend;
  studio::ble::BleCentral central(backend);
  BleTestDelegate delegate;
  const studio::ble::LinkHandle link = central.acquire(delegate, {});
  TEST_ASSERT_TRUE(
      central.requestConnect(link, bleAddress("11:22:33:44:55:66")));
  TEST_ASSERT_EQUAL_UINT32(1, backend.connectCalls(link));

  studio::ble::Event connected;
  connected.type = studio::ble::EventType::Connected;
  connected.link = link;
  backend.emit(connected);
  central.loop(100);
  central.markProtocolFailed(link, false);
  TEST_ASSERT_EQUAL_UINT32(1, backend.disconnectCalls(link));

  studio::ble::Event disconnected;
  disconnected.type = studio::ble::EventType::Disconnected;
  disconnected.link = link;
  backend.emit(disconnected);
  central.loop(101);
  central.loop(10000);
  TEST_ASSERT_EQUAL_UINT32(1, backend.connectCalls(link));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::ble::LinkPhase::Idle),
                        static_cast<int>(central.phase(link)));
  central.release(link);
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
  TEST_ASSERT_TRUE(tascam_x8::matchesSavedAdvertisement(
      tascamAdvertisement, "22:33:44:55:66:77", 0));
  TEST_ASSERT_FALSE(tascam_x8::matchesSavedAdvertisement(
      tascamAdvertisement, "33:44:55:66:77:88", 0));
  TEST_ASSERT_FALSE(tascam_x8::matchesSavedAdvertisement(
      tascamAdvertisement, "22:33:44:55:66:77", 1));
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

void test_aputure_light_crypto_and_network_vectors() {
  const uint8_t aesKey[16] = {0x2b,0x7e,0x15,0x16,0x28,0xae,0xd2,0xa6,
      0xab,0xf7,0x15,0x88,0x09,0xcf,0x4f,0x3c};
  const uint8_t aesInput[16] = {0x6b,0xc1,0xbe,0xe2,0x2e,0x40,0x9f,0x96,
      0xe9,0x3d,0x7e,0x11,0x73,0x93,0x17,0x2a};
  const uint8_t aesExpected[16] = {0x3a,0xd7,0x7b,0xb4,0x0d,0x7a,0x36,0x60,
      0xa8,0x9e,0xca,0xf3,0x24,0x66,0xef,0x97};
  uint8_t block[16];
  aputure_light::aes128EncryptBlock(aesKey, aesInput, block);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(aesExpected, block, 16);

  const uint8_t cmacExpected[16] = {0xbb,0x1d,0x69,0x29,0xe9,0x59,0x37,0x28,
      0x7f,0xa3,0x7d,0x12,0x9b,0x75,0x67,0x46};
  uint8_t cmac[16];
  aputure_light::aesCmac(aesKey, nullptr, 0, cmac);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(cmacExpected, cmac, 16);

  const uint8_t networkKey[16] = {0x00,0x11,0x22,0x33,0x44,0x55,0x66,0x77,
      0x88,0x99,0xaa,0xbb,0xcc,0xdd,0xee,0xff};
  const uint8_t appKey[16] = {0xff,0xee,0xdd,0xcc,0xbb,0xaa,0x99,0x88,
      0x77,0x66,0x55,0x44,0x33,0x22,0x11,0x00};
  aputure_light::AccessPayload blue;
  TEST_ASSERT_TRUE(aputure_light::buildRgbAccess(0x0000ff, 0, blue));
  // Lock the exact captured blue payload independently of the brightness-aware path.
  const uint8_t blueAccess[11] = {0x26,0xd3,0xa0,0,0,0,0xa0,0x0f,0,0,0x84};
  aputure_light::NetworkPdu network;
  TEST_ASSERT_TRUE(aputure_light::encodeAccessMessage(
      networkKey, appKey, blueAccess, sizeof(blueAccess), 1, 1, 0xc000, 0,
      network));
  const uint8_t expectedNetwork[] = {0x1e,0x72,0xa3,0xec,0xac,0x74,0x86,0xe5,
      0xfa,0x7f,0xf4,0x21,0x57,0xd4,0x22,0xd0,0xe9,0x15,0x35,0x5a,0x48,0x70,
      0xd9,0xfc,0xce,0xd7,0xc1,0xe3,0xba};
  TEST_ASSERT_EQUAL_UINT32(sizeof(expectedNetwork), network.length);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expectedNetwork, network.bytes, network.length);

  const uint8_t mcOff[] = {0x26,0xe8,0,0,0,0,0x20,0xa4,0x28,0xfa,0x02};
  TEST_ASSERT_TRUE(aputure_light::encodeAccessMessage(
      networkKey, appKey, mcOff, sizeof(mcOff), 0x1234, 2, 1, 0, network));
  uint8_t proxy[70] = {};
  size_t proxyLength = 0;
  TEST_ASSERT_TRUE(aputure_light::wrapProxyPdu(
      network, proxy, sizeof(proxy), proxyLength));
  aputure_light::DecodedAccessMessage decoded;
  TEST_ASSERT_TRUE(aputure_light::decodeProxyAccessMessage(
      networkKey, appKey, proxy, proxyLength, 0, decoded));
  TEST_ASSERT_EQUAL_UINT32(0x1234, decoded.sequence);
  TEST_ASSERT_EQUAL_UINT16(2, decoded.source);
  TEST_ASSERT_EQUAL_UINT16(1, decoded.destination);
  TEST_ASSERT_EQUAL_UINT32(sizeof(mcOff), decoded.accessLength);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(mcOff, decoded.access, decoded.accessLength);

  proxy[proxyLength - 1] ^= 1;
  TEST_ASSERT_FALSE(aputure_light::decodeProxyAccessMessage(
      networkKey, appKey, proxy, proxyLength, 0, decoded));

  const uint8_t deviceKey[16] = {0x10,0x21,0x32,0x43,0x54,0x65,0x76,0x87,
      0x98,0xa9,0xba,0xcb,0xdc,0xed,0xfe,0x0f};
  const uint8_t appKeyStatus[] = {0x80,0x03,0x00,0x00,0x00};
  TEST_ASSERT_TRUE(aputure_light::encodeDeviceMessage(
      networkKey, deviceKey, appKeyStatus, sizeof(appKeyStatus), 0x2345,
      2, 1, 0, network));
  TEST_ASSERT_TRUE(aputure_light::wrapProxyPdu(
      network, proxy, sizeof(proxy), proxyLength));
  TEST_ASSERT_TRUE(aputure_light::decodeProxyDeviceMessage(
      networkKey, deviceKey, proxy, proxyLength, 0, decoded));
  TEST_ASSERT_EQUAL_UINT32(0x2345, decoded.sequence);
  TEST_ASSERT_EQUAL_UINT16(2, decoded.source);
  TEST_ASSERT_EQUAL_UINT16(1, decoded.destination);
  TEST_ASSERT_EQUAL_UINT32(sizeof(appKeyStatus), decoded.accessLength);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(
      appKeyStatus, decoded.access, decoded.accessLength);
}

void test_aputure_light_access_payloads_and_validation() {
  uint16_t companyId = 0;
  uint16_t modelId = 0;
  TEST_ASSERT_TRUE(aputure_light::inferKnownVendorModel(
      "Aputure MC Pro", "", companyId, modelId));
  TEST_ASSERT_EQUAL_HEX16(0x03f6, companyId);
  TEST_ASSERT_EQUAL_HEX16(0x1000, modelId);
  TEST_ASSERT_EQUAL_STRING(
      "Aputure MC Pro",
      aputure_light::knownVendorModelName(companyId, modelId));
  TEST_ASSERT_TRUE(aputure_light::inferKnownVendorModel(
      "Studio light", "SLCK_BLE", companyId, modelId));
  TEST_ASSERT_EQUAL_HEX16(0x0211, companyId);
  TEST_ASSERT_EQUAL_HEX16(0x0000, modelId);
  TEST_ASSERT_EQUAL_STRING(
      "amaran Ace 25c",
      aputure_light::knownVendorModelName(companyId, modelId));
  TEST_ASSERT_TRUE(aputure_light::inferKnownVendorModel(
      "amaran Pano 60c", "", companyId, modelId));
  TEST_ASSERT_EQUAL_HEX16(0x0211, companyId);
  TEST_ASSERT_EQUAL_HEX16(0x0000, modelId);
  TEST_ASSERT_EQUAL_STRING(
      "amaran Pano 60c", aputure_light::knownProductName("Pavo 60"));
  TEST_ASSERT_EQUAL_STRING(
      "amaran Pano 120c",
      aputure_light::knownProductName("amaran Pano 120c"));
  TEST_ASSERT_FALSE(aputure_light::inferKnownVendorModel(
      "Unknown light", "", companyId, modelId));
  TEST_ASSERT_NULL(aputure_light::knownVendorModelName(0, 0));

  aputure_light::AccessPayload payload;
  TEST_ASSERT_TRUE(aputure_light::buildPowerAccess(true, payload));
  const uint8_t powerOn[] = {0x26,0x8d,0,0,0,0,0,0,0,1,0x8c};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(powerOn, payload.bytes, sizeof(powerOn));
  aputure_light::NetworkPdu unicastPower;
  const uint8_t unicastNetworkKey[16] = {
      0x00,0x11,0x22,0x33,0x44,0x55,0x66,0x77,
      0x88,0x99,0xaa,0xbb,0xcc,0xdd,0xee,0xff};
  const uint8_t unicastAppKey[16] = {
      0xff,0xee,0xdd,0xcc,0xbb,0xaa,0x99,0x88,
      0x77,0x66,0x55,0x44,0x33,0x22,0x11,0x00};
  TEST_ASSERT_TRUE(aputure_light::encodeAccessMessage(
      unicastNetworkKey, unicastAppKey, powerOn, sizeof(powerOn), 1, 1, 2, 0,
      unicastPower));
  const uint8_t expectedUnicastPower[] = {
      0x1e,0x4f,0x47,0x6a,0x22,0xab,0xe7,0x25,0xf8,0x7f,
      0x38,0x12,0x50,0x33,0x85,0x94,0xf7,0x4f,0x6b,0xf5,
      0x06,0xc8,0x8a,0x28,0x15,0xc7,0x6c,0x42,0x29};
  TEST_ASSERT_EQUAL_UINT32(sizeof(expectedUnicastPower), unicastPower.length);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expectedUnicastPower, unicastPower.bytes,
                                unicastPower.length);
  const uint8_t mcOff[] = {0x26,0xe8,0,0,0,0,0x20,0xa4,0x28,0xfa,0x02};
  const uint8_t aceOn[] = {0x26,0xec,1,0,0,0,0x80,0x56,0x1a,0xfa,0x01};
  aputure_light::VendorPowerStatus status;
  TEST_ASSERT_TRUE(
      aputure_light::parseVendorPowerStatus(mcOff, sizeof(mcOff), status));
  TEST_ASSERT_FALSE(status.on);
  TEST_ASSERT_EQUAL_UINT8(250, status.storedIntensity);
  TEST_ASSERT_EQUAL_UINT8(2, status.profile);
  TEST_ASSERT_TRUE(
      aputure_light::parseVendorPowerStatus(aceOn, sizeof(aceOn), status));
  TEST_ASSERT_TRUE(status.on);
  TEST_ASSERT_EQUAL_UINT8(1, status.profile);
  uint8_t corruptStatus[sizeof(aceOn)];
  std::memcpy(corruptStatus, aceOn, sizeof(aceOn));
  corruptStatus[6] ^= 1;
  TEST_ASSERT_FALSE(aputure_light::parseVendorPowerStatus(
      corruptStatus, sizeof(corruptStatus), status));

  aputure_light::ConfigurationStatusExpectation configExpected;
  configExpected.type =
      aputure_light::ConfigurationStatusType::ModelSubscription;
  configExpected.elementAddress = 0x0006;
  configExpected.groupAddress = 0xc005;
  configExpected.companyId = 0x0211;
  configExpected.modelId = 0x0000;
  configExpected.vendorModel = true;
  uint8_t configStatus = 0xff;
  const uint8_t acePrivateSubscription[] = {
      0x80,0x1f,0x00,0x06,0x00,0x05,0xc0,0x11,0x02,0x00,0x00};
  TEST_ASSERT_TRUE(aputure_light::matchConfigurationStatus(
      acePrivateSubscription, sizeof(acePrivateSubscription), configExpected,
      configStatus));
  TEST_ASSERT_EQUAL_UINT8(0, configStatus);
  const uint8_t duplicatedCommonSubscription[] = {
      0x80,0x1f,0x00,0x06,0x00,0x00,0xc0,0x11,0x02,0x00,0x00};
  TEST_ASSERT_FALSE(aputure_light::matchConfigurationStatus(
      duplicatedCommonSubscription, sizeof(duplicatedCommonSubscription),
      configExpected, configStatus));
  uint8_t wrongElementSubscription[sizeof(acePrivateSubscription)];
  std::memcpy(wrongElementSubscription, acePrivateSubscription,
              sizeof(acePrivateSubscription));
  wrongElementSubscription[3] = 0x04;
  TEST_ASSERT_FALSE(aputure_light::matchConfigurationStatus(
      wrongElementSubscription, sizeof(wrongElementSubscription),
      configExpected, configStatus));

  configExpected.type = aputure_light::ConfigurationStatusType::ModelApp;
  const uint8_t aceModelApp[] = {
      0x80,0x3e,0x00,0x06,0x00,0x00,0x00,0x11,0x02,0x00,0x00};
  TEST_ASSERT_TRUE(aputure_light::matchConfigurationStatus(
      aceModelApp, sizeof(aceModelApp), configExpected, configStatus));
  configExpected.type = aputure_light::ConfigurationStatusType::AppKey;
  configExpected.vendorModel = false;
  const uint8_t appKeyStatus[] = {0x80,0x03,0x00,0x00,0x00,0x00};
  TEST_ASSERT_TRUE(aputure_light::matchConfigurationStatus(
      appKeyStatus, sizeof(appKeyStatus), configExpected, configStatus));
  TEST_ASSERT_TRUE(aputure_light::buildCctAccess(5000, 0, 89, payload));
  const uint8_t cct[] = {0x26,0x80,0,0,0,0,0x40,0x41,0x9f,0xde,0x82};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(cct, payload.bytes, sizeof(cct));
  TEST_ASSERT_TRUE(aputure_light::buildRgbAccess(0xff0000, 50, payload));
  const uint8_t rgb[] = {0x26,0xdd,0x40,0x1f,0,0,0,0,0,0xfa,0x84};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(rgb, payload.bytes, sizeof(rgb));
  TEST_ASSERT_TRUE(aputure_light::buildRgbAccess(0xff0000, 5, payload));
  const uint8_t mcRed5[] = {
      0x26,0x87,0x06,0x03,0,0,0,0,0,0xfa,0x84};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(mcRed5, payload.bytes, sizeof(mcRed5));
  TEST_ASSERT_TRUE(aputure_light::buildRgbAccess(0x00ff00, 5, payload));
  const uint8_t aceGreen5[] = {
      0x26,0x4b,0x06,0x03,0,0,0,0x80,0x3e,0,0x84};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(aceGreen5, payload.bytes, sizeof(aceGreen5));
  TEST_ASSERT_TRUE(aputure_light::validCctCommand(2300, 0, -1000));
  TEST_ASSERT_FALSE(aputure_light::validCctCommand(2299, 50, 0));
  TEST_ASSERT_TRUE(aputure_light::validRgbCommand(0xffffff, 100));
  TEST_ASSERT_FALSE(aputure_light::validRgbCommand(0x1000000, 50));

  const uint8_t networkKey[16] = {};
  const uint8_t deviceKey[16] = {1};
  uint8_t appKeyAdd[20] = {};
  const uint32_t sequences[] = {0x123, 0x124};
  aputure_light::NetworkPduBatch segmented;
  TEST_ASSERT_TRUE(aputure_light::encodeSegmentedDeviceMessage(
      networkKey, deviceKey, appKeyAdd, sizeof(appKeyAdd), sequences, 2,
      1, 2, 0, segmented));
  TEST_ASSERT_EQUAL_UINT8(2, segmented.count);
  TEST_ASSERT_TRUE(segmented.pdus[0].length > 0);
  TEST_ASSERT_TRUE(segmented.pdus[1].length > 0);
  TEST_ASSERT_FALSE(aputure_light::encodeDeviceMessage(
      networkKey, deviceKey, appKeyAdd, sizeof(appKeyAdd), sequences[0],
      1, 2, 0, segmented.pdus[0]));
}

void test_aputure_light_store_and_sequence_reservation_survive_restart() {
  MemoryBackend backend;
  aputure_light::MeshStore store(backend);
  aputure_light::MeshStoreData data;
  data.network.initialized = true;
  for (uint8_t i = 0; i < 16; ++i) {
    data.network.networkKey[i] = i;
    data.network.applicationKey[i] = static_cast<uint8_t>(0xff - i);
  }
  aputure_light::MeshNodeRecord node;
  node.instanceId = 42;
  node.model = studio::DriverId::AputureLight;
  node.unicastAddress = 2;
  node.configured = true;
  node.controlGroupAddress = 0xc001;
  node.vendorCompanyId = 0x03f6;
  node.vendorModelId = 0x1000;
  node.configurationVersion = aputure_light::kCurrentConfigurationVersion;
  TEST_ASSERT_TRUE(aputure_light::upsertNode(data, node));
  aputure_light::MeshNodeRecord zhiyun;
  zhiyun.instanceId = 43;
  zhiyun.model = studio::DriverId::ZhiyunLight;
  zhiyun.unicastAddress = 3;
  zhiyun.configured = true;
  zhiyun.routingSelector =
      aputure_light::nextZhiyunRoutingSelector(data);
  TEST_ASSERT_EQUAL_UINT8(0, zhiyun.routingSelector);
  TEST_ASSERT_TRUE(aputure_light::upsertNode(data, zhiyun));
  TEST_ASSERT_EQUAL_UINT8(1,
      aputure_light::nextZhiyunRoutingSelector(data));
  TEST_ASSERT_TRUE(store.save(data));
  aputure_light::MeshStoreData loaded;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::ConfigLoadStatus::Loaded),
                        static_cast<int>(store.load(loaded)));
  const aputure_light::MeshNodeRecord* loadedNode =
      aputure_light::findNode(loaded, 42);
  TEST_ASSERT_NOT_NULL(loadedNode);
  TEST_ASSERT_EQUAL_HEX16(0xc001, loadedNode->controlGroupAddress);
  TEST_ASSERT_EQUAL_HEX16(0x03f6, loadedNode->vendorCompanyId);
  TEST_ASSERT_EQUAL_HEX16(0x1000, loadedNode->vendorModelId);
  TEST_ASSERT_EQUAL_UINT8(aputure_light::kCurrentConfigurationVersion,
                          loadedNode->configurationVersion);
  TEST_ASSERT_EQUAL_HEX16(
      0xc001, aputure_light::defaultControlGroupAddress(loaded, *loadedNode));
  const aputure_light::MeshNodeRecord fallbackGroupNode = {
      44, studio::DriverId::AputureLight, 4};
  TEST_ASSERT_EQUAL_HEX16(
      0xc003,
      aputure_light::defaultControlGroupAddress(loaded, fallbackGroupNode));
  const aputure_light::MeshNodeRecord secondGroupNode = {
      45, studio::DriverId::AputureLight, 5};
  TEST_ASSERT_EQUAL_HEX16(
      0xc004,
      aputure_light::defaultControlGroupAddress(loaded, secondGroupNode));
  TEST_ASSERT_NOT_EQUAL(
      aputure_light::defaultControlGroupAddress(loaded, fallbackGroupNode),
      aputure_light::defaultControlGroupAddress(loaded, secondGroupNode));
  TEST_ASSERT_EQUAL_HEX16(
      0xc001, aputure_light::memberControlGroupAddress(loaded, 42));
  TEST_ASSERT_EQUAL_HEX16(
      0, aputure_light::memberControlGroupAddress(loaded, 999));
  aputure_light::MeshNodeRecord pendingMcPro;
  pendingMcPro.instanceId = 44;
  pendingMcPro.model = studio::DriverId::AputureLight;
  pendingMcPro.unicastAddress = 4;
  TEST_ASSERT_TRUE(aputure_light::upsertNode(loaded, pendingMcPro));
  TEST_ASSERT_TRUE(aputure_light::assignVendorModel(
      loaded, 44, 0x03f6, 0x1000));
  aputure_light::MeshNodeRecord* identifiedMcPro =
      aputure_light::findNode(loaded, 44);
  TEST_ASSERT_NOT_NULL(identifiedMcPro);
  TEST_ASSERT_EQUAL_HEX16(0x03f6, identifiedMcPro->vendorCompanyId);
  TEST_ASSERT_EQUAL_HEX16(0x1000, identifiedMcPro->vendorModelId);
  TEST_ASSERT_EQUAL_HEX16(0xc003, identifiedMcPro->controlGroupAddress);
  TEST_ASSERT_TRUE(aputure_light::assignVendorModel(
      loaded, 44, 0x0211, 0x0000));
  TEST_ASSERT_EQUAL_HEX16(0x0211, identifiedMcPro->vendorCompanyId);
  TEST_ASSERT_EQUAL_HEX16(0x0000, identifiedMcPro->vendorModelId);
  identifiedMcPro->configured = true;
  TEST_ASSERT_FALSE(aputure_light::assignVendorModel(
      loaded, 44, 0x0211, 0x0000));
  std::strcpy(loaded.nodes[0].bleAddress, "aa:bb:cc:dd:ee:01");
  std::strcpy(loaded.nodes[1].bleAddress, "aa:bb:cc:dd:ee:02");
  TEST_ASSERT_TRUE(aputure_light::isKnownMeshProxyAddress(
      loaded, "aa:bb:cc:dd:ee:01"));
  TEST_ASSERT_TRUE(aputure_light::isKnownMeshProxyAddress(
      loaded, "aa:bb:cc:dd:ee:02"));
  TEST_ASSERT_FALSE(aputure_light::isKnownMeshProxyAddress(
      loaded, "aa:bb:cc:dd:ee:03"));
  TEST_ASSERT_FALSE(aputure_light::isKnownMeshProxyAddress(loaded, nullptr));
  const aputure_light::MeshNodeRecord* loadedZhiyun =
      aputure_light::findNode(loaded, 43);
  TEST_ASSERT_NOT_NULL(loadedZhiyun);
  TEST_ASSERT_EQUAL_UINT8(0, loadedZhiyun->routingSelector);
  aputure_light::SequenceAllocator first;
  TEST_ASSERT_TRUE(first.begin(store, loaded));
  uint32_t sequence = 99;
  TEST_ASSERT_TRUE(first.next(sequence));
  TEST_ASSERT_EQUAL_UINT32(0, sequence);
  aputure_light::MeshStoreData restarted;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::ConfigLoadStatus::Loaded),
                        static_cast<int>(store.load(restarted)));
  aputure_light::SequenceAllocator second;
  TEST_ASSERT_TRUE(second.begin(store, restarted));
  TEST_ASSERT_TRUE(second.next(sequence));
  TEST_ASSERT_EQUAL_UINT32(aputure_light::kSequenceBlockSize, sequence);
}

void test_mesh_store_round_trip_at_device_capacity() {
  MemoryBackend backend;
  aputure_light::MeshStore store(backend);
  aputure_light::MeshStoreData data;
  data.network.initialized = true;
  for (size_t i = 0; i < CONFIG_MAX_DEVICE_INSTANCES; ++i) {
    aputure_light::MeshNodeRecord node;
    node.instanceId = static_cast<studio::InstanceId>(i + 1);
    node.model = studio::DriverId::AputureLight;
    node.unicastAddress = static_cast<uint16_t>(i + 2);
    node.configured = true;
    TEST_ASSERT_TRUE(aputure_light::upsertNode(data, node));
  }
  TEST_ASSERT_EQUAL_UINT32(CONFIG_MAX_DEVICE_INSTANCES, data.nodeCount);
  TEST_ASSERT_TRUE(store.save(data));

  aputure_light::MeshStoreData restored;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::ConfigLoadStatus::Loaded),
      static_cast<int>(store.load(restored)));
  TEST_ASSERT_EQUAL_UINT32(CONFIG_MAX_DEVICE_INSTANCES, restored.nodeCount);
  TEST_ASSERT_NOT_NULL(
      aputure_light::findNode(restored, CONFIG_MAX_DEVICE_INSTANCES));
}

void test_mesh_v1_store_migrates_zhiyun_routing_selectors() {
  V1MeshBackend backend;
  aputure_light::MeshStore store(backend);
  aputure_light::MeshStoreData data;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::ConfigLoadStatus::Loaded),
      static_cast<int>(store.load(data)));
  TEST_ASSERT_EQUAL_UINT8(2, data.nodeCount);
  const aputure_light::MeshNodeRecord* first =
      aputure_light::findNode(data, 41);
  const aputure_light::MeshNodeRecord* second =
      aputure_light::findNode(data, 42);
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
  RUN_TEST(test_canon_camera_names_and_identity_address_types);
  RUN_TEST(test_canon_trigger_state_tracks_trigger_outcome);
  RUN_TEST(test_canon_state_requires_camera_notifications);
  RUN_TEST(test_tascam_cobs_commands_match_capture);
  RUN_TEST(test_tascam_scanner_and_confirmed_state);
  RUN_TEST(test_gopro_open_ble_packets_and_optimistic_state);
  RUN_TEST(test_dji_osmo_protocol_matches_official_connection_vector);
  RUN_TEST(test_insta360_gps_remote_protocol);
  RUN_TEST(test_driver_catalog_exposes_shark_and_canon);
  RUN_TEST(test_manager_keeps_every_compiled_camera_driver_reachable);
  RUN_TEST(test_insta360_explicit_recording_is_available_to_scenes);
  RUN_TEST(test_registry_crud_and_single_shark_limit);
  RUN_TEST(test_transactional_add_commits_only_after_pairing_and_readiness);
  RUN_TEST(test_transactional_add_cancel_and_failed_save_do_not_register_device);
  RUN_TEST(test_config_round_trip_preserves_dormant_records_and_detects_corruption);
  RUN_TEST(test_config_round_trip_at_twenty_four_record_capacity);
  RUN_TEST(test_home_assistant_config_is_separate_checksummed_and_local_only);
  RUN_TEST(test_panel_identity_uses_full_hardware_id_and_short_setup_suffix);
  RUN_TEST(test_portal_parses_sequence_action_commands_from_json);
  RUN_TEST(test_panel_settings_default_round_trip_corruption_and_rollback);
  RUN_TEST(test_v1_device_blob_migrates_without_changing_ble_identity);
  RUN_TEST(test_home_assistant_profiles_protocol_capacity_and_scene_validation);
  RUN_TEST(test_mixed_scene_activates_physical_transport_before_home_assistant);
  RUN_TEST(test_scene_switch_retains_old_only_links_when_four_resources_fit);
  RUN_TEST(test_mixed_scene_gives_physical_and_home_assistant_separate_timeouts);
  RUN_TEST(test_prepared_scene_edit_preserves_shared_ha_while_adding_ble_target);
  RUN_TEST(test_manager_migrates_legacy_without_boot_activation);
  RUN_TEST(test_manager_starts_empty_without_legacy_shark);
  RUN_TEST(test_manager_removes_old_unpaired_default_shark_only);
  RUN_TEST(test_manager_preserves_renamed_unpaired_shark);
  RUN_TEST(test_manager_routes_commands_and_blocks_disabled_device);
  RUN_TEST(test_manager_keeps_latest_tracked_result_when_result_queue_is_full);
  RUN_TEST(test_manager_routes_to_canon_driver);
  RUN_TEST(test_manager_routes_explicit_tascam_record_commands);
  RUN_TEST(test_removed_registry_stays_empty_after_restart);
  RUN_TEST(test_admin_mutations_roll_back_when_persistence_fails);
  RUN_TEST(test_manager_holds_concurrent_active_links);
  RUN_TEST(test_manager_retains_ready_sessions_and_evicts_safe_lru);
  RUN_TEST(test_manager_counts_shared_mesh_as_one_ble_slot);
  RUN_TEST(test_manager_cancels_unready_release_and_reuses_ready_session);
  RUN_TEST(test_manager_parks_ownerless_drop_but_keeps_intentional_offline);
  RUN_TEST(test_generated_stop_mapping_order_and_capacity);
  RUN_TEST(test_scene_stop_customization_and_relinking);
  RUN_TEST(test_scene_store_round_trip_and_corruption);
  RUN_TEST(test_scene_registry_and_store_grow_beyond_legacy_limit);
  RUN_TEST(test_scene_v1_migration_zeroes_action_arguments);
  RUN_TEST(test_scene_v2_migration_discards_authored_stop);
  RUN_TEST(test_scene_v3_migration_discards_authored_stop);
  RUN_TEST(test_orphaned_scene_steps_can_be_removed_one_at_a_time);
  RUN_TEST(test_press_record_start_and_generated_stop);
  RUN_TEST(test_partial_start_failure_can_stop_and_restart);
  RUN_TEST(test_stop_cancels_inflight_compound_light_action);
  RUN_TEST(test_prepare_ready_then_start_from_held_links);
  RUN_TEST(test_ble_central_lazy_lifetime_and_slot_exhaustion);
  RUN_TEST(test_ble_central_timing_and_readiness_reset_on_release);
  RUN_TEST(test_ble_central_pauses_scan_for_peripheral_gatt_mutation);
  RUN_TEST(test_ble_central_suspends_shared_scan_during_connect_and_security);
  RUN_TEST(test_ble_central_ignores_queued_event_from_evicted_link_generation);
  RUN_TEST(test_ble_central_shared_scan_claims_and_independent_release);
  RUN_TEST(test_ble_central_concurrent_links_retry_watchdog_and_security);
  RUN_TEST(test_tascam_reconnect_scans_after_one_direct_failure);
  RUN_TEST(test_ble_central_protocol_failure_can_stop_retry);
  RUN_TEST(test_ble_central_uses_bounded_scan_bursts);
  RUN_TEST(test_ble_central_parser_bonds_and_queue_overflow);
  RUN_TEST(test_ble_device_advertisement_matchers);
  RUN_TEST(test_aputure_light_crypto_and_network_vectors);
  RUN_TEST(test_aputure_light_access_payloads_and_validation);
  RUN_TEST(test_aputure_light_store_and_sequence_reservation_survive_restart);
  RUN_TEST(test_mesh_store_round_trip_at_device_capacity);
  RUN_TEST(test_mesh_v1_store_migrates_zhiyun_routing_selectors);
  RUN_TEST(test_zhiyun_x100_frames_and_confirmed_state_replies);
  RUN_TEST(test_zhiyun_x100_identity_and_advertisement_match);
  RUN_TEST(test_mesh_no_oob_policy_accepts_x100_static_oob_capability);
  return UNITY_END();
}
