#include <unity.h>
#include <ArduinoJson.h>

#include <cmath>
#include <cstdio>
#include <cstring>

#include "core/active_instance_pool.h"
#include "core/ble/ble_central.h"
#include "support/fake_ble_backend.h"
#include "core/ble/onboarding_candidates.h"
#include "core/ble/onboarding_auto_select.h"
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
#include "devices/gopro/ble_match.h"
#include "devices/gopro/state.h"
#include "devices/insta360/protocol.h"
#include "devices/insta360/state.h"
#include "devices/dji_osmo/protocol.h"
#include "devices/home_assistant/protocol.h"
#include "devices/aputure_light/ble_match.h"
#include "devices/aputure_light/crypto.h"
#include "devices/aputure_light/protocol.h"
#include "devices/aputure_light/state.h"
#include "core/mesh/mesh_store.h"
#include "devices/zhiyun_light/ble_match.h"
#include "devices/zhiyun_light/protocol.h"
#include "devices/zhiyun_light/state.h"
#include "portal_scene_parser.h"
#include "core/mesh/provisioning_policy.h"
#include "core/mesh/mesh_initializer.h"
#include "core/mesh/proxy_transport.h"

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

  void seed(const uint8_t* data, size_t length) {
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(sizeof(data_), length);
    std::memcpy(data_, data, length);
    length_ = length;
  }

  const uint8_t* data() const { return data_; }
  size_t length() const { return length_; }

  bool failWrites = false;

 private:
  uint8_t data_[studio::ConfigStore::kMaxBlobSize] = {};
  size_t length_ = 0;
};

class FakeDriver : public studio::DeviceDriver {
 public:
  explicit FakeDriver(
      studio::DriverId id = studio::DriverId::SharkNanoII)
      : id_(id) {}
  studio::DriverId driverId() const override { return id_; }
  studio::InstanceProfile instanceProfile(
      const studio::DeviceRecord&,
      const studio::InstanceProfile& catalogProfile) const override {
    studio::InstanceProfile profile = catalogProfile;
    profile.capabilities &= ~capabilityClearMask;
    return profile;
  }
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
    lastValue0 = command.value0;
    lastValue1 = command.value1;
    lastValue2 = command.value2;
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
  bool cancelOnboarding(const studio::DeviceRecord&) override {
    ++cancelOnboardingCount;
    onboardingScanning = false;
    return cancelOnboardingSucceeds;
  }
  size_t onboardingCandidateCount(studio::InstanceId) const override {
    return onboardingCandidateAvailable ? 1 : 0;
  }
  bool onboardingCandidate(studio::InstanceId, size_t index,
                           studio::OnboardingCandidate& candidate) const override {
    if (!onboardingCandidateAvailable || index != 0) return false;
    candidate = studio::OnboardingCandidate{};
    candidate.token = 73;
    std::strcpy(candidate.name, "Test Light");
    std::strcpy(candidate.address, "11:22:33:44:55:66");
    candidate.rssi = -48;
    return true;
  }
  bool selectOnboardingCandidate(studio::InstanceId, uint32_t token) override {
    if (token != 73 || !onboardingCandidateAvailable) return false;
    if (onboardingSelectionFails) {
      onboardingScanning = true;
      return false;
    }
    onboardingScanning = false;
    onboardingSelected = true;
    return true;
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
  studio::InstanceId
      activeInstances[studio::DeviceManager::kMaxActiveInstances] = {};
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
  bool cancelOnboardingSucceeds = true;
  bool onboardingCandidateAvailable = false;
  bool onboardingSelectionFails = false;
  bool onboardingScanning = true;
  bool onboardingSelected = false;
  bool pairingUpdatePending = false;
  bool pairingValue = true;
  studio::CommandType lastCommand = studio::CommandType::Refresh;
  uint32_t capabilityClearMask = 0;
  int32_t lastValue0 = 0;
  int32_t lastValue1 = 0;
  int32_t lastValue2 = 0;

 private:
  studio::DriverId id_;
};
