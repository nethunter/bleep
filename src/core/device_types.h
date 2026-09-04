#pragma once

#include <cstddef>
#include <cstdint>

#include "driver_config.h"

namespace studio {

using InstanceId = uint32_t;
constexpr InstanceId kInvalidInstanceId = 0;

enum class DriverId : uint16_t {
  Unknown = 0,
  SharkNanoII = 1,
  CanonBle = 2,       // Canon (Smart) smartphone-mode BLE
  TascamX8 = 3,
  CanonTrigger = 4,   // Canon (Trigger) BR-E1-compatible BLE
  HomeAssistant = 5,
  AputureLight = 6,
  ZhiyunLight = 9,
  GoPro = 10,
  Insta360 = 11,
  DjiOsmo = 12,
  SonyCamera = 13,
  PhoneCamera = 14,
  Insta360Mini = 15,
  // Internal transport identity. Never persisted as a device driver.
  PanelOwnedMesh = 0xfffe,
};

enum class DeviceType : uint8_t {
  Unknown,
  Motion,
  Light,
  Camera,
  Recorder,
  Switch,
  Action,
};

enum class HomeAssistantDomain : uint8_t {
  None = 0,
  Light,
  Switch,
  InputBoolean,
  Button,
  Scene,
  Script,
};

enum class LinkState : uint8_t {
  Disconnected,
  Scanning,
  Connecting,
  Connected,
};

enum class StateQuality : uint8_t {
  Unknown,
  Optimistic,
  Confirmed,
};

enum class Capability : uint32_t {
  None = 0,
  Link = 1u << 0,
  Battery = 1u << 1,
  Keypoints = 1u << 2,
  Timing = 1u << 3,
  ManualMotion = 1u << 4,
  RunControl = 1u << 5,
  RunProgress = 1u << 6,
  Loop = 1u << 7,
  Direction = 1u << 8,
  RecordTrigger = 1u << 9,
  RecordStart = 1u << 10,
  RecordStop = 1u << 11,
  RecordingState = 1u << 12,
  TurnOn = 1u << 13,
  TurnOff = 1u << 14,
  Press = 1u << 15,
  Activate = 1u << 16,
  SetLightCct = 1u << 17,
  SetLightRgb = 1u << 18,
  SetLightTint = 1u << 19,
};

constexpr uint32_t capabilityBit(Capability capability) {
  return static_cast<uint32_t>(capability);
}

enum class CommandType : uint8_t {
  Connect,
  Disconnect,
  ForgetPairing,
  Refresh,
  KeypointSet,
  KeypointGo,
  KeypointDelete,
  SetSpeed,
  SetHold,
  SetRunState,
  SetLoop,
  SetDirection,
  SetManualTracking,
  SetMotionVector,
  StopMotion,
  RecordTrigger,
  RecordStart,
  RecordStop,
  CameraPowerOn,
  CameraPowerOff,
  TurnOn,
  TurnOff,
  Press,
  Activate,
  SetLightCct,
  SetLightRgb,
  SetLightCctAndOn,
  SetLightRgbAndOn,
};

struct DeviceCommand {
  uint32_t requestId = 0;
  InstanceId instanceId = kInvalidInstanceId;
  CommandType type = CommandType::Refresh;
  int value0 = 0;
  int value1 = 0;
  int value2 = 0;
};

enum class CommandStatus : uint8_t {
  Queued,
  Succeeded,
  InvalidInstance,
  Disabled,
  Unsupported,
  InvalidArgument,
  Unavailable,
  QueueFull,
  Busy,
  ConfirmationRequired,
};

struct CommandResult {
  uint32_t requestId = 0;
  InstanceId instanceId = kInvalidInstanceId;
  CommandStatus status = CommandStatus::Unavailable;
};

struct DeviceRuntimeState {
  LinkState link = LinkState::Disconnected;
  bool protocolReady = false;
  StateQuality quality = StateQuality::Unknown;
  bool commandPending = false;
  bool commandFailed = false;
  bool recordingConfirmed = false;
  bool recording = false;
  // The peer accepts the radio link but refuses this panel (for example a
  // camera whose smartphone registration now belongs to another remote).
  // Retrying cannot succeed until the operator re-pairs.
  bool pairingRejected = false;
};

struct LightControlState {
  bool available = false;
  bool supportsPower = false;
  bool supportsCct = false;
  bool supportsRgb = false;
  bool supportsTint = false;
  bool on = false;
  bool stateKnown = false;
  bool commandPending = false;
  bool commandFailed = false;
  StateQuality quality = StateQuality::Unknown;
  uint16_t minKelvin = 0;
  uint16_t maxKelvin = 0;
  uint16_t kelvin = 5600;
  int16_t tintPermille = 0;
  uint8_t brightness = 50;
  uint8_t cctBrightness = 50;
  uint8_t rgbBrightness = 50;
  uint32_t rgb = 0xff0000;
  bool rgbMode = false;
  char status[48] = "Unavailable";
};

enum class ConnectionOwner : uint8_t {
  Foreground = 1u << 0,
  Sequence = 1u << 1,
};

constexpr size_t kInstanceIdTextCapacity = 16;
constexpr size_t kDriverIdTextCapacity = 24;
constexpr size_t kDeviceNameCapacity = 32;
constexpr size_t kBleAddressCapacity = 20;
constexpr size_t kBleNameCapacity = 40;
constexpr size_t kHomeAssistantEntityIdCapacity = 72;

struct OnboardingCandidate {
  uint32_t token = 0;
  char name[kBleNameCapacity] = "";
  char address[kBleAddressCapacity] = "";
  uint8_t addressType = 0;
  int8_t rssi = 0;
};

struct DeviceRecord {
  InstanceId instanceId = kInvalidInstanceId;
  DriverId driverId = DriverId::Unknown;
  bool enabled = true;
  bool paired = false;
  char displayName[kDeviceNameCapacity] = "";
  char bleAddress[kBleAddressCapacity] = "";
  uint8_t bleAddressType = 0;
  char bleName[kBleNameCapacity] = "";
  HomeAssistantDomain homeAssistantDomain = HomeAssistantDomain::None;
  char homeAssistantEntityId[kHomeAssistantEntityIdCapacity] = "";
};

struct BleSlotKey {
  DriverId family = DriverId::Unknown;
  uint32_t group = 0;

  constexpr BleSlotKey() = default;
  constexpr BleSlotKey(DriverId slotFamily, uint32_t slotGroup)
      : family(slotFamily), group(slotGroup) {}

  constexpr bool valid() const {
    return family != DriverId::Unknown && group != 0;
  }
  constexpr bool operator==(const BleSlotKey& other) const {
    return family == other.family && group == other.group;
  }
  constexpr bool operator!=(const BleSlotKey& other) const {
    return !(*this == other);
  }
};

struct InstanceProfile {
  DeviceType type = DeviceType::Unknown;
  uint32_t capabilities = 0;

  constexpr InstanceProfile() = default;
  constexpr InstanceProfile(DeviceType deviceType, uint32_t capabilityMask)
      : type(deviceType), capabilities(capabilityMask) {}
};

struct HomeAssistantEntitySelection {
  InstanceId instanceId = kInvalidInstanceId;
  HomeAssistantDomain domain = HomeAssistantDomain::None;
  char entityId[kHomeAssistantEntityIdCapacity] = "";
  char displayName[kDeviceNameCapacity] = "";
};

struct DriverDescriptor {
  DriverId id = DriverId::Unknown;
  const char* stableId = "";
  const char* brand = "";
  const char* model = "";
  DeviceType type = DeviceType::Unknown;
  uint32_t capabilities = 0;
  uint8_t maxInstances = 0;
  bool discoverable = true;

  constexpr DriverDescriptor() = default;
  constexpr DriverDescriptor(DriverId driverId, const char* stableDriverId,
                             const char* driverBrand, const char* driverModel,
                             DeviceType deviceType, uint32_t capabilityMask,
                             uint8_t instanceLimit, bool showInPicker = true)
      : id(driverId),
        stableId(stableDriverId),
        brand(driverBrand),
        model(driverModel),
        type(deviceType),
        capabilities(capabilityMask),
        maxInstances(instanceLimit),
        discoverable(showInPicker) {}
};

}  // namespace studio
