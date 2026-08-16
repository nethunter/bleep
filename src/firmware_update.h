#pragma once

#include <cstddef>
#include <cstdint>

#include "core/panel_settings.h"

namespace firmware_update {

enum class Status : uint8_t {
  Idle,
  Deferred,
  Connecting,
  Checking,
  Available,
  Downloading,
  Verifying,
  RebootPending,
  Failed,
  RecoveryAvailable,
};

struct Snapshot {
  Status status = Status::Idle;
  bool wifiConfigured = false;
  bool updateAvailable = false;
  bool notificationPending = false;
  bool disconnectRequired = false;
  bool recoveryAvailable = true;
  bool recoveryUpdatePending = false;
  uint8_t progressPercent = 0;
  uint64_t releaseSequence = 0;
  uint64_t lastCheckEpoch = 0;
  char version[32] = "";
  char lastResult[32] = "Never checked";
  char message[64] = "Not checked";
};

class FirmwareUpdateService {
 public:
  void begin();
  void loop();
  void noteUserActivity();
  void setRuntimeIdle(bool idle, bool automaticEligible);
  void checkNow(bool allowDisconnect = false);
  bool installAvailable();
  void dismissAvailable();
  bool enterRecovery();
  bool requestFactoryReset();
  Snapshot status() const;

#ifdef UI_SIMULATOR
  void simSetAvailable(const char* version, uint64_t sequence);
  void simSetChecking();
  void simSetFailure(const char* message);
  void simSetWifiConfigured(bool configured);
  void simSetRecoveryAvailable(bool available);
  void simSetRecoveryRefresh(uint8_t progressPercent);
  void simClearRecoveryRefresh();
  bool simRecoveryRequested() const;
  bool simFactoryResetRequested() const;
  void simClearFactoryResetRequested();
#endif
};

FirmwareUpdateService& service();

}  // namespace firmware_update
