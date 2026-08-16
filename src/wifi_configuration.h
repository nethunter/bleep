#pragma once

#include <cstddef>
#include <cstdint>

#include "core/home_assistant_config.h"

namespace wifi_configuration {

constexpr size_t kMaximumNetworks = 16;

enum class Status : uint8_t {
  Idle,
  WaitingForRelease,
  Scanning,
  Results,
  Connecting,
  Saving,
  Stopping,
  Succeeded,
  Failed,
};

struct Network {
  char ssid[studio::kWifiSsidCapacity] = "";
  int32_t rssi = 0;
  bool secure = false;
};

struct Snapshot {
  Status status = Status::Idle;
  bool configured = false;
  size_t networkCount = 0;
  char savedSsid[studio::kWifiSsidCapacity] = "";
  char selectedSsid[studio::kWifiSsidCapacity] = "";
  char message[64] = "Ready";
};

bool validPassword(bool secure, const char* password);

class WifiConfigurationService {
 public:
  void begin();
  void loop();
  bool requestScan();
  bool connect(size_t networkIndex, const char* password);
  void cancel();
  bool forget();
  bool active() const;
  const Snapshot& snapshot() const { return snapshot_; }
  const Network* network(size_t index) const;

#if defined(UI_SIMULATOR) || defined(WIFI_CONFIGURATION_NATIVE)
  void simSetSaved(const char* ssid);
  void simSetResults();
  void simSetConnecting(const char* ssid);
  void simSetOutcome(bool success, const char* message);
#endif

 private:
  Snapshot snapshot_;
  Network networks_[kMaximumNetworks] = {};
  char pendingPassword_[studio::kWifiPasswordCapacity] = "";
  uint32_t stateStarted_ = 0;
  Status afterStop_ = Status::Idle;
  char afterStopMessage_[64] = "Ready";

  void refreshSaved();
  void setStatus(Status status, const char* message);
  void beginStop(Status finalStatus, const char* message);
};

WifiConfigurationService& service();

}  // namespace wifi_configuration
