#pragma once

#include <cstddef>
#include <cstdint>

#include "devices/canon_ble/protocol.h"

namespace canon_ble {

struct CanonBleState {
  enum class Link : uint8_t {
    Disconnected,
    Scanning,
    Connecting,
    Connected,
  };

  enum class Phase : uint8_t {
    Idle,
    Bonding,
    AwaitingConfirmation,
    Handshaking,
    PostPairSetup,
    OpeningSession,
    Ready,
    PoweringOff,
    PoweredOff,
  };

  enum class Recording : uint8_t {
    Unknown,
    Stopped,
    Starting,
    Recording,
    Stopping,
  };

  Link link = Link::Disconnected;
  Phase phase = Phase::Idle;
  Recording recording = Recording::Unknown;
  bool recordingConfirmed = false;
  bool hasSavedDevice = false;
  bool commandPending = false;
  bool lastCommandFailed = false;
  bool powerOffFailed = false;
  bool pairingRejected = false;
  // True while scanning if the only visible Canon bodies are already claimed by
  // other device records (so a new instance cannot pair them).
  bool claimedPeerVisible = false;
  char deviceName[40] = "";
};

void resetTransientState(CanonBleState& state);
void markCommandQueued(CanonBleState& state, bool start);
void markCommandWriteFailed(CanonBleState& state);
void reduceRecordNotification(CanonBleState& state, const uint8_t* data,
                              size_t len);

}  // namespace canon_ble
