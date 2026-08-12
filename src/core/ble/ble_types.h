#pragma once

#include <cstddef>
#include <cstdint>

#include "driver_config.h"

namespace studio::ble {

using LinkHandle = uint8_t;
constexpr LinkHandle kInvalidLinkHandle = 0;

constexpr size_t kBleAddressCapacity = 20;
constexpr size_t kBleNameCapacity = 40;

enum class SecurityPolicy : uint8_t {
  None,
  BondNoMitm,
  BondSecure,
};

enum class LinkPhase : uint8_t {
  Idle,
  Scanning,
  WaitingRetry,
  WaitingConnect,
  Connecting,
  Connected,
  Disconnecting,
};

enum class EventType : uint8_t {
  Advertisement,
  ScanEnded,
  Connected,
  ConnectFailed,
  Disconnected,
  SecurityComplete,
};

struct Address {
  char value[kBleAddressCapacity] = "";
  uint8_t type = 0;
};

struct Advertisement {
  Address address;
  int8_t rssi = 0;
  uint8_t payload[CONFIG_BLE_ADV_PAYLOAD_SIZE] = {};
  uint8_t payloadLength = 0;
};

struct Event {
  EventType type = EventType::ScanEnded;
  LinkHandle link = kInvalidLinkHandle;
  Advertisement advertisement;
  int reason = 0;
  bool succeeded = false;
};

struct ConnectionParameters {
  uint16_t minInterval = 0;
  uint16_t maxInterval = 0;
  uint16_t latency = 0;
  uint16_t supervisionTimeout = 0;

  constexpr ConnectionParameters() = default;
  constexpr ConnectionParameters(uint16_t minimum, uint16_t maximum,
                                 uint16_t connectionLatency,
                                 uint16_t timeout)
      : minInterval(minimum),
        maxInterval(maximum),
        latency(connectionLatency),
        supervisionTimeout(timeout) {}

  constexpr bool configured() const {
    return minInterval != 0 && maxInterval != 0 &&
           supervisionTimeout != 0;
  }
};

struct ConnectPolicy {
  SecurityPolicy security = SecurityPolicy::None;
  uint16_t connectTimeoutMs = 4000;
  uint16_t connectWatchdogMs = 6000;
  // A saved peer commonly needs one or two retries while waking its radio.
  // Keep the third attempt direct before paying the discovery latency again.
  uint8_t directAttemptsBeforeScan = 3;
  bool alwaysDirect = false;
  ConnectionParameters setupParameters = {6, 12, 0, 200};
  // Once protocol setup is complete, favor a calmer 30-50 ms interval. This
  // remains responsive for controller commands while reducing radio wakeups.
  ConnectionParameters readyParameters = {24, 40, 0, 400};
  const char* diagnosticTag = "ble";
};

bool addressEqual(const Address& left, const Address& right);
uint8_t identityAddressType(uint8_t addressType);
bool advertisementName(const Advertisement& advertisement, char* output,
                       size_t capacity);
bool advertisementNameEquals(const Advertisement& advertisement,
                             const char* expected);
bool advertisementNameContains(const Advertisement& advertisement,
                               const char* token);
bool advertisesService(const Advertisement& advertisement, const char* uuid);
uint16_t manufacturerCompanyId(const Advertisement& advertisement);
bool meshProxyNetworkId(const Advertisement& advertisement,
                        uint8_t output[8]);

}  // namespace studio::ble
