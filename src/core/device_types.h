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
};

enum class DeviceType : uint8_t {
  Unknown,
  Motion,
  Light,
  Camera,
  Recorder,
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
};

struct DeviceCommand {
  uint32_t requestId = 0;
  InstanceId instanceId = kInvalidInstanceId;
  CommandType type = CommandType::Refresh;
  int value0 = 0;
  int value1 = 0;
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
};

constexpr size_t kInstanceIdTextCapacity = 16;
constexpr size_t kDriverIdTextCapacity = 24;
constexpr size_t kDeviceNameCapacity = 32;
constexpr size_t kBleAddressCapacity = 20;
constexpr size_t kBleNameCapacity = 40;

struct DeviceRecord {
  InstanceId instanceId = kInvalidInstanceId;
  DriverId driverId = DriverId::Unknown;
  bool enabled = true;
  bool paired = false;
  char displayName[kDeviceNameCapacity] = "";
  char bleAddress[kBleAddressCapacity] = "";
  uint8_t bleAddressType = 0;
  char bleName[kBleNameCapacity] = "";
};

struct DriverDescriptor {
  DriverId id = DriverId::Unknown;
  const char* stableId = "";
  const char* brand = "";
  const char* model = "";
  DeviceType type = DeviceType::Unknown;
  uint32_t capabilities = 0;
  uint8_t maxInstances = 0;

  constexpr DriverDescriptor() = default;
  constexpr DriverDescriptor(DriverId driverId, const char* stableDriverId,
                             const char* driverBrand, const char* driverModel,
                             DeviceType deviceType, uint32_t capabilityMask,
                             uint8_t instanceLimit)
      : id(driverId),
        stableId(stableDriverId),
        brand(driverBrand),
        model(driverModel),
        type(deviceType),
        capabilities(capabilityMask),
        maxInstances(instanceLimit) {}
};

}  // namespace studio
