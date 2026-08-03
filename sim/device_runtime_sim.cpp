#include "core/device_manager.h"

#include <cstring>
#include <vector>

#include "core/config_store.h"
#include "core/device_driver.h"
#include "shark_state.h"

namespace studio {
namespace {

class MemoryConfigBackend : public IConfigBackend {
 public:
  size_t read(uint8_t* destination, size_t capacity) override {
    if (blob_.empty() || destination == nullptr || capacity == 0) {
      return 0;
    }
    const size_t n = blob_.size() < capacity ? blob_.size() : capacity;
    std::memcpy(destination, blob_.data(), n);
    return n;
  }

  bool write(const uint8_t* data, size_t length) override {
    if (data == nullptr && length > 0) {
      return false;
    }
    blob_.assign(data, data + length);
    return true;
  }

 private:
  std::vector<uint8_t> blob_;
};

class SeededLegacyBackend : public ILegacySharkBackend {
 public:
  bool readLegacyShark(LegacySharkConfig& config) override {
    config.paired = true;
    std::strncpy(config.address, "AA:BB:CC:DD:EE:FF", sizeof(config.address) - 1);
    config.address[sizeof(config.address) - 1] = '\0';
    config.addressType = 0;
    std::strncpy(config.advertisedName, "Shark Nano II", sizeof(config.advertisedName) - 1);
    config.advertisedName[sizeof(config.advertisedName) - 1] = '\0';
    return true;
  }
};

class SimSharkDriver : public DeviceDriver {
 public:
  DriverId driverId() const override { return DriverId::SharkNanoII; }

  void activate(const DeviceRecord& record) override {
    active_ = true;
    std::strncpy(state_.deviceName, record.bleName[0] != '\0' ? record.bleName : record.displayName,
                 sizeof(state_.deviceName) - 1);
    state_.deviceName[sizeof(state_.deviceName) - 1] = '\0';
    state_.hasSavedDevice = record.paired;
    if (state_.link == shark::SharkState::Link::Disconnected) {
      state_.link = shark::SharkState::Link::Scanning;
    }
  }

  void deactivate() override {
    active_ = false;
    state_.link = shark::SharkState::Link::Disconnected;
  }

  void loop() override {}

  CommandStatus dispatch(const DeviceCommand& command) override {
    switch (command.type) {
      case CommandType::Connect:
        state_.link = shark::SharkState::Link::Scanning;
        return CommandStatus::Succeeded;
      case CommandType::ForgetPairing:
        state_.hasSavedDevice = false;
        state_.link = shark::SharkState::Link::Scanning;
        return CommandStatus::Succeeded;
      default:
        return CommandStatus::Succeeded;
    }
  }

  DeviceRuntimeState runtimeState() const override {
    DeviceRuntimeState runtime;
    switch (state_.link) {
      case shark::SharkState::Link::Scanning:
        runtime.link = LinkState::Scanning;
        break;
      case shark::SharkState::Link::Connecting:
        runtime.link = LinkState::Connecting;
        break;
      case shark::SharkState::Link::Connected:
        runtime.link = LinkState::Connected;
        break;
      case shark::SharkState::Link::Disconnected:
        runtime.link = LinkState::Disconnected;
        break;
    }
    runtime.quality = StateQuality::Confirmed;
    return runtime;
  }

  const void* specializedState() const override { return &state_; }

  bool consumePairingUpdate(DeviceRecord&) override { return false; }

  shark::SharkState& state() { return state_; }

 private:
  bool active_ = false;
  shark::SharkState state_;
};

MemoryConfigBackend gBackend;
SeededLegacyBackend gLegacy;
SimSharkDriver gSharkDriver;
DeviceManager gManager(gBackend, gLegacy, gSharkDriver);

}  // namespace

DeviceManager& devices() { return gManager; }

shark::SharkState& simSharkState() { return gSharkDriver.state(); }

void simSetConnectedDemoState() {
  shark::SharkState& state = gSharkDriver.state();
  state.link = shark::SharkState::Link::Connected;
  state.hasSavedDevice = true;
  state.battery = 76;
  state.presenceKnown = true;
  state.timingKnown = true;
  state.trackingKnown = true;
  state.tracking = false;
  state.loopOn = true;
  state.reverse = false;
  state.runStateCode = shark::kRunStop;
  state.runProgressKnown = true;
  state.runPercent = 42.0f;
  std::strncpy(state.runText, "idle", sizeof(state.runText) - 1);
  state.runText[sizeof(state.runText) - 1] = '\0';
  std::strncpy(state.deviceName, "Shark Nano II", sizeof(state.deviceName) - 1);
  state.deviceName[sizeof(state.deviceName) - 1] = '\0';
  for (int i = 0; i < shark::kKeypointCount; ++i) {
    state.present[i] = (i == 0 || i == 2 || i == 5);
    state.speed[i] = state.present[i] ? (30 + i * 5) : -1;
    state.hold[i] = state.present[i] ? (2 + i) : -1;
  }
}

void simSetScanningState() {
  shark::SharkState& state = gSharkDriver.state();
  state.link = shark::SharkState::Link::Scanning;
  state.hasSavedDevice = true;
  state.battery = -1;
  std::strncpy(state.deviceName, "Shark Nano II", sizeof(state.deviceName) - 1);
  state.deviceName[sizeof(state.deviceName) - 1] = '\0';
}

}  // namespace studio
