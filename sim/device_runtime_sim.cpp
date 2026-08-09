#include "core/device_manager.h"
#include "core/panel_settings.h"
#include "core/scene_service.h"

#include <cstring>
#include <vector>

#include "core/config_store.h"
#include "core/device_driver.h"
#include "devices/canon_ble/state.h"
#include "devices/canon_trigger/state.h"
#include "devices/shark_nano_ii/state.h"
#include "devices/tascam_x8/state.h"
#include "devices/gopro/state.h"
#include "devices/gopro/protocol.h"
#include "devices/insta360/state.h"
#include "devices/dji_osmo/state.h"
#include "devices/action_camera_research/driver.h"
#include "devices/home_assistant/driver.h"
#include "devices/aputure_light/driver.h"
#include "devices/zhiyun_x100/state.h"

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

  bool activate(const DeviceRecord& record) override {
    activeInstance_ = record.instanceId;
    active_ = true;
    std::strncpy(state_.deviceName, record.bleName[0] != '\0' ? record.bleName : record.displayName,
                 sizeof(state_.deviceName) - 1);
    state_.deviceName[sizeof(state_.deviceName) - 1] = '\0';
    state_.hasSavedDevice = record.paired;
    if (state_.link == shark::SharkState::Link::Disconnected) {
      state_.link = shark::SharkState::Link::Scanning;
    }
    return true;
  }

  void deactivate(InstanceId instanceId) override {
    if (instanceId != activeInstance_) return;
    active_ = false;
    activeInstance_ = kInvalidInstanceId;
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

  DeviceRuntimeState runtimeState(InstanceId instanceId) const override {
    DeviceRuntimeState runtime;
    if (instanceId != activeInstance_) return runtime;
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
    runtime.protocolReady = runtime.link == LinkState::Connected;
    runtime.quality = StateQuality::Confirmed;
    return runtime;
  }

  const void* specializedState(InstanceId instanceId) const override {
    return instanceId == activeInstance_ ? &state_ : nullptr;
  }
  bool consumePairingUpdate(InstanceId, DeviceRecord&) override { return false; }

  shark::SharkState& state() { return state_; }

 private:
  bool active_ = false;
  InstanceId activeInstance_ = kInvalidInstanceId;
  shark::SharkState state_;
};

class SimCanonDriver : public DeviceDriver {
 public:
  DriverId driverId() const override { return DriverId::CanonBle; }
  bool activate(const DeviceRecord& record) override {
    activeInstance_ = record.instanceId;
    state_.hasSavedDevice = record.paired;
    state_.link = canon_ble::CanonBleState::Link::Scanning;
    std::strncpy(state_.deviceName, record.displayName,
                 sizeof(state_.deviceName) - 1);
    return true;
  }
  bool resume(const DeviceRecord& record) override {
    if (activeInstance_ != record.instanceId) return false;
    if (state_.phase == canon_ble::CanonBleState::Phase::PoweredOff) {
      canon_ble::resetTransientState(state_);
      state_.link = canon_ble::CanonBleState::Link::Connected;
      state_.phase = canon_ble::CanonBleState::Phase::Ready;
      const uint8_t stopped[] = {0x01, 0x01, 0x01};
      canon_ble::reduceRecordNotification(state_, stopped, sizeof(stopped));
    }
    return true;
  }
  void deactivate(InstanceId instanceId) override {
    if (instanceId != activeInstance_) return;
    activeInstance_ = kInvalidInstanceId;
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
    if (command.type == CommandType::CameraPowerOff &&
        state_.link == canon_ble::CanonBleState::Link::Connected &&
        state_.phase == canon_ble::CanonBleState::Phase::Ready) {
      canon_ble::resetTransientState(state_);
      state_.link = canon_ble::CanonBleState::Link::Disconnected;
      state_.phase = canon_ble::CanonBleState::Phase::PoweredOff;
      return CommandStatus::Succeeded;
    }
    if (command.type == CommandType::CameraPowerOn &&
        state_.link == canon_ble::CanonBleState::Link::Disconnected &&
        state_.phase == canon_ble::CanonBleState::Phase::PoweredOff) {
      canon_ble::resetTransientState(state_);
      state_.link = canon_ble::CanonBleState::Link::Connected;
      state_.phase = canon_ble::CanonBleState::Phase::Ready;
      const uint8_t stopped[] = {0x01, 0x01, 0x01};
      canon_ble::reduceRecordNotification(state_, stopped, sizeof(stopped));
      return CommandStatus::Succeeded;
    }
    return CommandStatus::Succeeded;
  }
  DeviceRuntimeState runtimeState(InstanceId instanceId) const override {
    DeviceRuntimeState runtime;
    if (instanceId != activeInstance_) return runtime;
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
    runtime.protocolReady =
        runtime.link == LinkState::Connected &&
        state_.phase == canon_ble::CanonBleState::Phase::Ready;
    runtime.quality = state_.recordingConfirmed ? StateQuality::Confirmed
                                                : StateQuality::Unknown;
    runtime.commandPending = state_.commandPending;
    runtime.recordingConfirmed = state_.recordingConfirmed;
    runtime.recording =
        state_.recording == canon_ble::CanonBleState::Recording::Recording;
    return runtime;
  }
  const void* specializedState(InstanceId instanceId) const override {
    return instanceId == activeInstance_ ? &state_ : nullptr;
  }
  bool consumePairingUpdate(InstanceId, DeviceRecord&) override { return false; }
  canon_ble::CanonBleState& state() { return state_; }

 private:
  InstanceId activeInstance_ = kInvalidInstanceId;
  canon_ble::CanonBleState state_;
};

class SimCanonTriggerDriver : public DeviceDriver {
 public:
  DriverId driverId() const override { return DriverId::CanonTrigger; }
  bool activate(const DeviceRecord& record) override {
    activeInstance_ = record.instanceId;
    state_.hasSavedDevice = record.paired;
    state_.link = canon_trigger::CanonTriggerState::Link::Scanning;
    std::strncpy(state_.deviceName, record.displayName,
                 sizeof(state_.deviceName) - 1);
    state_.deviceName[sizeof(state_.deviceName) - 1] = '\0';
    return true;
  }
  void deactivate(InstanceId instanceId) override {
    if (instanceId != activeInstance_) return;
    activeInstance_ = kInvalidInstanceId;
    state_.link = canon_trigger::CanonTriggerState::Link::Disconnected;
    canon_trigger::resetTransientState(state_);
  }
  void loop() override {}
  CommandStatus dispatch(const DeviceCommand& command) override {
    if (command.type == CommandType::RecordTrigger &&
        state_.link == canon_trigger::CanonTriggerState::Link::Connected) {
      canon_trigger::markTriggerQueued(state_);
      canon_trigger::markTriggerComplete(state_, true);
      return CommandStatus::Succeeded;
    }
    return CommandStatus::Succeeded;
  }
  DeviceRuntimeState runtimeState(InstanceId instanceId) const override {
    DeviceRuntimeState runtime;
    if (instanceId != activeInstance_) return runtime;
    switch (state_.link) {
      case canon_trigger::CanonTriggerState::Link::Scanning:
        runtime.link = LinkState::Scanning;
        break;
      case canon_trigger::CanonTriggerState::Link::Connecting:
        runtime.link = LinkState::Connecting;
        break;
      case canon_trigger::CanonTriggerState::Link::Connected:
        runtime.link = LinkState::Connected;
        break;
      case canon_trigger::CanonTriggerState::Link::Disconnected:
        runtime.link = LinkState::Disconnected;
        break;
    }
    runtime.protocolReady = runtime.link == LinkState::Connected;
    runtime.quality = StateQuality::Unknown;
    runtime.commandPending = state_.triggerPending;
    return runtime;
  }
  const void* specializedState(InstanceId instanceId) const override {
    return instanceId == activeInstance_ ? &state_ : nullptr;
  }
  bool consumePairingUpdate(InstanceId instanceId,
                            DeviceRecord& record) override {
    if (instanceId != activeInstance_ || record.paired ||
        state_.link != canon_trigger::CanonTriggerState::Link::Connected) {
      return false;
    }
    record.paired = true;
    std::strncpy(record.bleAddress, "22:33:44:55:66:77",
                 sizeof(record.bleAddress) - 1);
    std::strncpy(record.bleName, "EOS Remote",
                 sizeof(record.bleName) - 1);
    return true;
  }
  canon_trigger::CanonTriggerState& state() { return state_; }

 private:
  InstanceId activeInstance_ = kInvalidInstanceId;
  canon_trigger::CanonTriggerState state_;
};

class SimTascamDriver : public DeviceDriver {
 public:
  DriverId driverId() const override { return DriverId::TascamX8; }
  bool activate(const DeviceRecord& record) override {
    activeInstance_ = record.instanceId;
    state_.hasSavedDevice = record.paired;
    state_.link = tascam_x8::TascamX8State::Link::Scanning;
    std::strncpy(state_.deviceName, record.displayName,
                 sizeof(state_.deviceName) - 1);
    state_.deviceName[sizeof(state_.deviceName) - 1] = '\0';
    return true;
  }
  void deactivate(InstanceId instanceId) override {
    if (instanceId != activeInstance_) return;
    activeInstance_ = kInvalidInstanceId;
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
  DeviceRuntimeState runtimeState(InstanceId instanceId) const override {
    DeviceRuntimeState runtime;
    if (instanceId != activeInstance_) return runtime;
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
    runtime.protocolReady = runtime.link == LinkState::Connected;
    runtime.quality = state_.recordingConfirmed ? StateQuality::Confirmed
                                                : StateQuality::Unknown;
    runtime.commandPending = state_.commandPending;
    runtime.recordingConfirmed = state_.recordingConfirmed;
    runtime.recording =
        state_.recording == tascam_x8::TascamX8State::Recording::Recording;
    return runtime;
  }
  const void* specializedState(InstanceId instanceId) const override {
    return instanceId == activeInstance_ ? &state_ : nullptr;
  }
  bool consumePairingUpdate(InstanceId, DeviceRecord&) override { return false; }
  tascam_x8::TascamX8State& state() { return state_; }

 private:
  InstanceId activeInstance_ = kInvalidInstanceId;
  tascam_x8::TascamX8State state_;
};

class SimZhiyunX100Driver : public DeviceDriver {
 public:
  DriverId driverId() const override { return DriverId::ZhiyunLight; }
  bool activate(const DeviceRecord& record) override {
    activeInstance_ = record.instanceId;
    state_.link = zhiyun_x100::X100State::Link::Connected;
    state_.phase = zhiyun_x100::X100State::Phase::Ready;
    state_.hasSavedDevice = true;
    state_.confirmed = true;
    state_.on = true;
    state_.brightness = 13.0f;
    state_.kelvin = 5600;
    std::strncpy(state_.deviceName, "PL105_SIM",
                 sizeof(state_.deviceName) - 1);
    return true;
  }
  void deactivate(InstanceId instanceId) override {
    if (instanceId != activeInstance_) return;
    activeInstance_ = kInvalidInstanceId;
    state_.link = zhiyun_x100::X100State::Link::Disconnected;
  }
  void loop() override {}
  CommandStatus dispatch(const DeviceCommand& command) override {
    if (command.instanceId != activeInstance_) return CommandStatus::Unavailable;
    if (command.type == CommandType::TurnOn ||
        command.type == CommandType::TurnOff) {
      state_.on = command.type == CommandType::TurnOn;
      state_.confirmed = true;
      state_.lastCommandFailed = false;
      return CommandStatus::Succeeded;
    }
    if ((command.type == CommandType::SetLightCct ||
         command.type == CommandType::SetLightCctAndOn) && command.value2 == 0) {
      state_.kelvin = static_cast<uint16_t>(command.value0);
      state_.brightness = static_cast<float>(command.value1);
      if (command.type == CommandType::SetLightCctAndOn) state_.on = true;
      state_.confirmed = true;
      state_.lastCommandFailed = false;
      return CommandStatus::Succeeded;
    }
    return CommandStatus::Unsupported;
  }
  DeviceRuntimeState runtimeState(InstanceId instanceId) const override {
    DeviceRuntimeState runtime;
    if (instanceId != activeInstance_) return runtime;
    runtime.link = state_.link == zhiyun_x100::X100State::Link::Connected
                       ? LinkState::Connected
                       : LinkState::Disconnected;
    runtime.protocolReady = state_.phase == zhiyun_x100::X100State::Phase::Ready;
    runtime.quality = state_.confirmed ? StateQuality::Confirmed
                                       : StateQuality::Unknown;
    runtime.commandPending = state_.commandPending;
    runtime.commandFailed = state_.lastCommandFailed;
    return runtime;
  }
  const void* specializedState(InstanceId instanceId) const override {
    return instanceId == activeInstance_ ? &state_ : nullptr;
  }
  bool lightControlState(InstanceId instanceId,
                         LightControlState& out) const override {
    if (instanceId != activeInstance_) return false;
    out.available = state_.phase == zhiyun_x100::X100State::Phase::Ready;
    out.supportsPower = out.supportsCct = true;
    out.supportsRgb = zhiyun_x100::supportsRgb(state_.model);
    out.on = state_.on;
    out.stateKnown = state_.confirmed;
    out.quality = state_.confirmed ? StateQuality::Confirmed : StateQuality::Unknown;
    out.minKelvin = zhiyun_x100::kMinKelvin;
    out.maxKelvin = zhiyun_x100::kMaxKelvin;
    out.kelvin = state_.kelvin;
    out.brightness = static_cast<uint8_t>(state_.brightness + 0.5f);
    out.rgb = state_.rgb;
    out.rgbMode = out.supportsRgb &&
                  state_.mode == zhiyun_x100::X100State::Mode::Rgb;
    std::strncpy(out.status, state_.confirmed ? "Ready / confirmed"
                                              : "State unknown",
                 sizeof(out.status) - 1);
    return true;
  }
  bool consumePairingUpdate(InstanceId, DeviceRecord&) override { return false; }
  zhiyun_x100::X100State& state() { return state_; }

 private:
  InstanceId activeInstance_ = kInvalidInstanceId;
  zhiyun_x100::X100State state_;
};

class SimGoProDriver : public DeviceDriver {
 public:
  DriverId driverId() const override { return DriverId::GoPro; }
  bool activate(const DeviceRecord& record) override {
    activeInstance_ = record.instanceId;
    state_.link = gopro::GoProState::Link::Connected;
    state_.hasSavedDevice = true;
    pairingChanged_ = !record.paired;
    return true;
  }
  void deactivate(InstanceId instanceId) override {
    if (instanceId == activeInstance_) activeInstance_ = kInvalidInstanceId;
  }
  void loop() override {}
  CommandStatus dispatch(const DeviceCommand& command) override {
    if (command.instanceId != activeInstance_) return CommandStatus::Unavailable;
    if (command.type == CommandType::RecordStart ||
        command.type == CommandType::RecordStop) {
      const bool start = command.type == CommandType::RecordStart;
      gopro::markCommandQueued(state_, start);
      gopro::reduceCommandResponse(state_, start, gopro::kSuccessStatus);
      return CommandStatus::Succeeded;
    }
    return CommandStatus::Unsupported;
  }
  DeviceRuntimeState runtimeState(InstanceId instanceId) const override {
    DeviceRuntimeState runtime;
    if (instanceId != activeInstance_) return runtime;
    runtime.link = LinkState::Connected;
    runtime.protocolReady = true;
    runtime.quality = StateQuality::Optimistic;
    return runtime;
  }
  const void* specializedState(InstanceId instanceId) const override {
    return instanceId == activeInstance_ ? &state_ : nullptr;
  }
  bool consumePairingUpdate(InstanceId instanceId, DeviceRecord& record) override {
    if (instanceId != activeInstance_ || !pairingChanged_) return false;
    pairingChanged_ = false;
    record.paired = true;
    std::strncpy(record.bleAddress, "10:20:30:40:50:60",
                 sizeof(record.bleAddress) - 1);
    std::strncpy(record.bleName, "GoPro SIM", sizeof(record.bleName) - 1);
    return true;
  }
 private:
  InstanceId activeInstance_ = kInvalidInstanceId;
  bool pairingChanged_ = false;
  gopro::GoProState state_;
};

class SimInsta360Driver : public DeviceDriver {
 public:
  DriverId driverId() const override { return DriverId::Insta360; }
  bool activate(const DeviceRecord& record) override { id_=record.instanceId; state_.link=insta360::State::Link::Connected; return true; }
  void deactivate(InstanceId id) override { if(id==id_)id_=kInvalidInstanceId; }
  void loop() override {}
  CommandStatus dispatch(const DeviceCommand& c) override { if(c.instanceId!=id_)return CommandStatus::Unavailable;if(c.type!=CommandType::RecordTrigger)return CommandStatus::Unsupported;++state_.triggerCount;return CommandStatus::Succeeded; }
  DeviceRuntimeState runtimeState(InstanceId id) const override { DeviceRuntimeState r;if(id==id_){r.link=LinkState::Connected;r.protocolReady=true;r.quality=StateQuality::Optimistic;}return r; }
  const void* specializedState(InstanceId id) const override { return id==id_?&state_:nullptr; }
  bool consumePairingUpdate(InstanceId,DeviceRecord&) override{return false;}
 private: InstanceId id_=kInvalidInstanceId; insta360::State state_;
};

class SimDjiOsmoDriver : public DeviceDriver {
 public:
  DriverId driverId() const override { return DriverId::DjiOsmo; }
  bool activate(const DeviceRecord& record) override { id_=record.instanceId;state_.link=record.paired?dji_osmo::State::Link::Connected:dji_osmo::State::Link::Connecting;state_.recording=dji_osmo::State::Recording::Stopped;state_.statusConfirmed=record.paired;state_.verificationPending=!record.paired;state_.verificationCode=42;return true; }
  void deactivate(InstanceId id) override { if(id==id_)id_=kInvalidInstanceId; }
  void loop() override {}
  CommandStatus dispatch(const DeviceCommand& c) override { if(c.instanceId!=id_)return CommandStatus::Unavailable;if(c.type==CommandType::RecordStart)state_.recording=dji_osmo::State::Recording::Recording;else if(c.type==CommandType::RecordStop)state_.recording=dji_osmo::State::Recording::Stopped;else return CommandStatus::Unsupported;return CommandStatus::Succeeded; }
  DeviceRuntimeState runtimeState(InstanceId id) const override { DeviceRuntimeState r;if(id==id_){r.link=state_.link==dji_osmo::State::Link::Connected?LinkState::Connected:LinkState::Connecting;r.protocolReady=state_.link==dji_osmo::State::Link::Connected;r.quality=StateQuality::Confirmed;r.recordingConfirmed=state_.statusConfirmed;r.recording=state_.recording==dji_osmo::State::Recording::Recording;}return r; }
  const void* specializedState(InstanceId id) const override { return id==id_?&state_:nullptr; }
  bool consumePairingUpdate(InstanceId,DeviceRecord&) override{return false;}
 private: InstanceId id_=kInvalidInstanceId; dji_osmo::State state_;
};

MemoryConfigBackend gBackend;
MemoryConfigBackend gScenesBackend;
MemoryConfigBackend gPanelSettingsBackend;
SeededLegacyBackend gLegacy;
SimSharkDriver gSharkDriver;
SimCanonTriggerDriver gCanonTriggerDriver;
SimCanonDriver gCanonDriver;
SimTascamDriver gTascamDriver;
HomeAssistantDriver gHomeAssistantDriver;
AputureLightDriver gAputureLight;
SimZhiyunX100Driver gZhiyunX100Driver;
SimGoProDriver gGoProDriver;
SimInsta360Driver gInsta360Driver;
SimDjiOsmoDriver gDjiOsmoDriver;
ActionCameraResearchDriver gSonyCameraDriver(DriverId::SonyCamera);
ActionCameraResearchDriver gPhoneCameraDriver(DriverId::PhoneCamera);
DeviceDriver* gDrivers[] = {&gSharkDriver, &gCanonTriggerDriver, &gCanonDriver,
                            &gTascamDriver, &gHomeAssistantDriver,
                            &gAputureLight,
                            &gZhiyunX100Driver, &gGoProDriver,
                            &gInsta360Driver, &gDjiOsmoDriver,
                            &gSonyCameraDriver, &gPhoneCameraDriver};
static_assert(sizeof(gDrivers) / sizeof(gDrivers[0]) <=
                  DeviceManager::kMaxCompiledDrivers,
              "simulated driver table exceeds DeviceManager capacity");
DeviceManager gManager(gBackend, gLegacy, gDrivers,
                       sizeof(gDrivers) / sizeof(gDrivers[0]));
SceneService gScenes(gScenesBackend, gManager);
PanelSettingsService gPanelSettings(gPanelSettingsBackend);

}  // namespace

DeviceManager& devices() { return gManager; }

SceneService& scenes() { return gScenes; }

PanelSettingsService& panelSettings() { return gPanelSettings; }

shark::SharkState& simSharkState() { return gSharkDriver.state(); }
canon_ble::CanonBleState& simCanonState() { return gCanonDriver.state(); }
canon_trigger::CanonTriggerState& simCanonTriggerState() {
  return gCanonTriggerDriver.state();
}
tascam_x8::TascamX8State& simTascamState() {
  return gTascamDriver.state();
}
zhiyun_x100::X100State& simZhiyunState() {
  return gZhiyunX100Driver.state();
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
  state.powerOffFailed = false;
  state.recordingConfirmed = confirmed;
  state.recording =
      confirmed ? (recording ? canon_ble::CanonBleState::Recording::Recording
                             : canon_ble::CanonBleState::Recording::Stopped)
                : canon_ble::CanonBleState::Recording::Unknown;
  std::strncpy(state.deviceName, "EOS R6 Mark III",
               sizeof(state.deviceName) - 1);
  state.deviceName[sizeof(state.deviceName) - 1] = '\0';
}

void simSetCanonTriggerConnectedState() {
  canon_trigger::CanonTriggerState& state = gCanonTriggerDriver.state();
  state.link = canon_trigger::CanonTriggerState::Link::Connected;
  state.hasSavedDevice = true;
  state.triggerPending = false;
  state.lastTriggerSucceeded = false;
  state.triggerCount = 0;
  std::strncpy(state.deviceName, "EOS R6 Trigger",
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

void simSetSequenceConnectedState() {
  simSetCanonConnectedState(false, true);
  simSetTascamConnectedState(false);
}

}  // namespace studio
