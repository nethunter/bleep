#include "core/device_manager.h"

#include <cstring>
#include <vector>

#include "core/config_store.h"
#include "core/device_driver.h"
#include "devices/canon_ble/state.h"
#include "devices/shark_nano_ii/state.h"
#include "devices/tascam_x8/state.h"

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
      case CommandType::SetRunState:
        state_.runStateCode = static_cast<uint8_t>(command.value0);
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

class SimCanonDriver : public DeviceDriver {
 public:
  DriverId driverId() const override { return DriverId::CanonBle; }
  void activate(const DeviceRecord& record) override {
    state_.hasSavedDevice = record.paired;
    state_.link = canon_ble::CanonBleState::Link::Scanning;
    std::strncpy(state_.deviceName, record.displayName,
                 sizeof(state_.deviceName) - 1);
  }
  void deactivate() override {
    state_.link = canon_ble::CanonBleState::Link::Disconnected;
    canon_ble::resetTransientState(state_);
  }
  void loop() override {}
  CommandStatus dispatch(const DeviceCommand& command) override {
    if (command.type == CommandType::RecordStart &&
        state_.link == canon_ble::CanonBleState::Link::Connected) {
      canon_ble::markCommandQueued(state_, true);
      const uint8_t recording[] = {0x01, 0x01, 0x02};
      canon_ble::reduceRecordNotification(state_, recording,
                                          sizeof(recording));
      return CommandStatus::Succeeded;
    }
    if (command.type == CommandType::RecordStop &&
        state_.link == canon_ble::CanonBleState::Link::Connected) {
      canon_ble::markCommandQueued(state_, false);
      const uint8_t stopped[] = {0x01, 0x01, 0x01};
      canon_ble::reduceRecordNotification(state_, stopped, sizeof(stopped));
      return CommandStatus::Succeeded;
    }
    return CommandStatus::Succeeded;
  }
  DeviceRuntimeState runtimeState() const override {
    DeviceRuntimeState runtime;
    switch (state_.link) {
      case canon_ble::CanonBleState::Link::Scanning:
        runtime.link = LinkState::Scanning;
        break;
      case canon_ble::CanonBleState::Link::Connecting:
        runtime.link = LinkState::Connecting;
        break;
      case canon_ble::CanonBleState::Link::Connected:
        runtime.link = LinkState::Connected;
        break;
      case canon_ble::CanonBleState::Link::Disconnected:
        runtime.link = LinkState::Disconnected;
        break;
    }
    runtime.quality = state_.recordingConfirmed ? StateQuality::Confirmed
                                                : StateQuality::Unknown;
    return runtime;
  }
  const void* specializedState() const override { return &state_; }
  bool consumePairingUpdate(DeviceRecord&) override { return false; }
  canon_ble::CanonBleState& state() { return state_; }

 private:
  canon_ble::CanonBleState state_;
};

class SimTascamDriver : public DeviceDriver {
 public:
  DriverId driverId() const override { return DriverId::TascamX8; }
  void activate(const DeviceRecord& record) override {
    state_.hasSavedDevice = record.paired;
    state_.link = tascam_x8::TascamX8State::Link::Scanning;
    std::strncpy(state_.deviceName, record.displayName,
                 sizeof(state_.deviceName) - 1);
    state_.deviceName[sizeof(state_.deviceName) - 1] = '\0';
  }
  void deactivate() override {
    state_.link = tascam_x8::TascamX8State::Link::Disconnected;
    tascam_x8::resetTransientState(state_);
  }
  void loop() override {}
  CommandStatus dispatch(const DeviceCommand& command) override {
    uint8_t payload[18] = {'D', 'R'};
    if (command.type == CommandType::RecordStart &&
        state_.link == tascam_x8::TascamX8State::Link::Connected) {
      tascam_x8::markCommandQueued(state_, true);
      payload[2] = 0x20;
      payload[3] = 0x20;
      payload[4] = 0x24;
      payload[5] = 0x01;
      tascam_x8::reduceFrame(state_, {payload, sizeof(payload)});
      return CommandStatus::Succeeded;
    }
    if (command.type == CommandType::RecordStop &&
        state_.link == tascam_x8::TascamX8State::Link::Connected) {
      tascam_x8::markCommandQueued(state_, false);
      payload[2] = 0x10;
      payload[3] = 0x20;
      payload[4] = 0x08;
      tascam_x8::reduceFrame(state_, {payload, sizeof(payload)});
      return CommandStatus::Succeeded;
    }
    return CommandStatus::Succeeded;
  }
  DeviceRuntimeState runtimeState() const override {
    DeviceRuntimeState runtime;
    switch (state_.link) {
      case tascam_x8::TascamX8State::Link::Scanning:
        runtime.link = LinkState::Scanning;
        break;
      case tascam_x8::TascamX8State::Link::Connecting:
        runtime.link = LinkState::Connecting;
        break;
      case tascam_x8::TascamX8State::Link::Connected:
        runtime.link = LinkState::Connected;
        break;
      case tascam_x8::TascamX8State::Link::Disconnected:
        runtime.link = LinkState::Disconnected;
        break;
    }
    runtime.quality = state_.recordingConfirmed ? StateQuality::Confirmed
                                                : StateQuality::Unknown;
    return runtime;
  }
  const void* specializedState() const override { return &state_; }
  bool consumePairingUpdate(DeviceRecord&) override { return false; }
  tascam_x8::TascamX8State& state() { return state_; }

 private:
  tascam_x8::TascamX8State state_;
};

MemoryConfigBackend gBackend;
SeededLegacyBackend gLegacy;
SimSharkDriver gSharkDriver;
SimCanonDriver gCanonDriver;
SimTascamDriver gTascamDriver;
DeviceDriver* gDrivers[] = {&gSharkDriver, &gCanonDriver, &gTascamDriver};
DeviceManager gManager(gBackend, gLegacy, gDrivers, 3);

}  // namespace

DeviceManager& devices() { return gManager; }

shark::SharkState& simSharkState() { return gSharkDriver.state(); }
canon_ble::CanonBleState& simCanonState() { return gCanonDriver.state(); }
tascam_x8::TascamX8State& simTascamState() {
  return gTascamDriver.state();
}

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

void simSetCanonConnectedState(bool recording, bool confirmed) {
  canon_ble::CanonBleState& state = gCanonDriver.state();
  state.link = canon_ble::CanonBleState::Link::Connected;
  state.phase = canon_ble::CanonBleState::Phase::Ready;
  state.hasSavedDevice = true;
  state.commandPending = false;
  state.lastCommandFailed = false;
  state.recordingConfirmed = confirmed;
  state.recording =
      confirmed ? (recording ? canon_ble::CanonBleState::Recording::Recording
                             : canon_ble::CanonBleState::Recording::Stopped)
                : canon_ble::CanonBleState::Recording::Unknown;
  std::strncpy(state.deviceName, "EOS R6 Mark III",
               sizeof(state.deviceName) - 1);
  state.deviceName[sizeof(state.deviceName) - 1] = '\0';
}

void simSetTascamConnectedState(bool recording) {
  tascam_x8::TascamX8State& state = gTascamDriver.state();
  state.link = tascam_x8::TascamX8State::Link::Connected;
  state.hasSavedDevice = true;
  state.commandPending = false;
  state.lastCommandFailed = false;
  state.recordingConfirmed = true;
  state.recording = recording
                        ? tascam_x8::TascamX8State::Recording::Recording
                        : tascam_x8::TascamX8State::Recording::Stopped;
  std::strncpy(state.deviceName, "Portacapture X8",
               sizeof(state.deviceName) - 1);
  state.deviceName[sizeof(state.deviceName) - 1] = '\0';
}

}  // namespace studio
