#include "shark_client.h"

#include <Arduino.h>
#include <NimBLEDevice.h>

#include <cstring>
#include <string>

#include "freertos/FreeRTOS.h"
#include "freertos/stream_buffer.h"

namespace shark {

SharkClient gShark;

namespace {

const NimBLEUUID kServiceUuid("fff0");
const NimBLEUUID kWriteUuid("fff2");
const NimBLEUUID kNotifyUuid("fff1");

bool nameLooksLikeShark(const std::string& name) {
  return name.find("Nano") != std::string::npos || name.find("Shark") != std::string::npos;
}

class ScanCallbacks : public NimBLEScanCallbacks {
 public:
  void onResult(const NimBLEAdvertisedDevice* device) override {
    if (device == nullptr) {
      return;
    }
    bool match = device->isAdvertisingService(kServiceUuid);
    if (!match) {
      match = nameLooksLikeShark(device->getName());
    }
    if (match) {
      gShark.onScanMatch(device);
    }
  }
};

class ClientCallbacks : public NimBLEClientCallbacks {
 public:
  void onDisconnect(NimBLEClient*, int) override { gShark.onLinkDisconnected(); }
};

ScanCallbacks gScanCallbacks;
ClientCallbacks gClientCallbacks;

void notifyTrampoline(NimBLERemoteCharacteristic*, uint8_t* data, size_t length, bool) {
  gShark.onNotifyBytes(data, length);
}

}  // namespace

void SharkClient::begin() {
  notifyStream_ = xStreamBufferCreate(1024, 1);

  NimBLEDevice::init("");
  NimBLEDevice::setPower(9);
  NimBLEDevice::setMTU(247);

  loadSavedDevice();

  // If a device is remembered, go straight to reconnecting; otherwise begin a
  // discovery scan so pairing just works.
  if (haveTarget_) {
    beginConnect();
  } else {
    beginScan();
  }
}

void SharkClient::loop() {
  if (disconnectedFlag_) {
    disconnectedFlag_ = false;
    handleDisconnect();
  }

  drainNotifications();

  if (scanHit_) {
    // Snapshot the matched device and stop scanning before connecting.
    NimBLEDevice::getScan()->stop();
    scanActive_ = false;
    strncpy(targetAddr_, scanHitAddr_, sizeof(targetAddr_) - 1);
    targetAddr_[sizeof(targetAddr_) - 1] = '\0';
    targetAddrType_ = scanHitType_;
    strncpy(targetName_, scanHitName_, sizeof(targetName_) - 1);
    targetName_[sizeof(targetName_) - 1] = '\0';
    haveTarget_ = true;
    scanHit_ = false;
    beginConnect();
  }

  const uint32_t now = millis();

  if (trackingPending_ && now > trackingPendingExpiryMs_) {
    trackingPending_ = false;
  }

  switch (state_.link) {
    case Link::Disconnected:
      if (now >= retryAtMs_) {
        // Try a couple of fast direct connects to the saved device, then fall
        // back to non-blocking scanning so the UI stays responsive when the
        // slider is powered off or its address has changed.
        if (haveTarget_ && connectFails_ < 2) {
          beginConnect();
        } else {
          beginScan();
        }
      }
      break;
    case Link::Scanning:
    case Link::Connecting:
    case Link::Connected:
      break;
  }
}

void SharkClient::startScan() {
  // Manual (re)pairing: drop any current link and scan fresh.
  teardownConnection();
  resetDeviceState();
  beginScan();
}

void SharkClient::disconnectLink() {
  teardownConnection();
  resetDeviceState();
  state_.link = Link::Disconnected;
  // Stay disconnected for a moment, then auto-reconnect if a device is known.
  scheduleRetry(retryAtMs_, 1500);
}

void SharkClient::forgetDevice() {
  prefs_.begin("shark", false);
  prefs_.clear();
  prefs_.end();
  haveTarget_ = false;
  targetAddr_[0] = '\0';
  targetName_[0] = '\0';
  state_.hasSavedDevice = false;
  startScan();
}

void SharkClient::beginScan() {
  if (scanActive_) {
    state_.link = Link::Scanning;
    return;
  }
  NimBLEScan* scan = NimBLEDevice::getScan();
  scan->setScanCallbacks(&gScanCallbacks, false);
  scan->setActiveScan(true);
  scan->setInterval(100);
  scan->setWindow(80);
  scan->clearResults();
  scanHit_ = false;
  scan->start(0, false, true);  // 0 == scan until stopped
  scanActive_ = true;
  state_.link = Link::Scanning;
}

void SharkClient::beginConnect() {
  if (client_ == nullptr) {
    client_ = NimBLEDevice::createClient();
    client_->setClientCallbacks(&gClientCallbacks, false);
    client_->setConnectTimeout(3000);
  }
  state_.link = Link::Connecting;

  NimBLEAddress address(std::string(targetAddr_), targetAddrType_);
  const bool ok = client_->connect(address, true, false, true);
  if (ok) {
    completeConnect();
    return;
  }

  // Connection failed: back off and let loop() decide whether to retry the
  // direct connect or fall back to scanning.
  connectFails_++;
  state_.link = Link::Disconnected;
  uint32_t delay = 1500u * (connectFails_ < 4 ? connectFails_ : 4);
  scheduleRetry(retryAtMs_, delay);
}

void SharkClient::completeConnect() {
  NimBLERemoteService* service = client_->getService(kServiceUuid);
  if (service == nullptr) {
    client_->disconnect();
    connectFails_++;
    state_.link = Link::Disconnected;
    scheduleRetry(retryAtMs_, 2000);
    return;
  }

  writeChar_ = service->getCharacteristic(kWriteUuid);
  NimBLERemoteCharacteristic* notifyChar = service->getCharacteristic(kNotifyUuid);
  if (writeChar_ == nullptr || notifyChar == nullptr) {
    writeChar_ = nullptr;
    client_->disconnect();
    connectFails_++;
    state_.link = Link::Disconnected;
    scheduleRetry(retryAtMs_, 2000);
    return;
  }

  scanner_.reset();
  notifyChar->subscribe(true, notifyTrampoline, true);

  resetDeviceState();
  strncpy(state_.deviceName, targetName_, sizeof(state_.deviceName) - 1);
  state_.deviceName[sizeof(state_.deviceName) - 1] = '\0';
  state_.link = Link::Connected;
  connectFails_ = 0;

  saveDevice();
  state_.hasSavedDevice = true;

  sendHandshake();
}

void SharkClient::teardownConnection() {
  if (client_ != nullptr && client_->isConnected()) {
    client_->disconnect();
  }
  writeChar_ = nullptr;
}

void SharkClient::handleDisconnect() {
  writeChar_ = nullptr;
  resetDeviceState();
  state_.link = Link::Disconnected;
  scheduleRetry(retryAtMs_, 1500);
}

void SharkClient::sendHandshake() {
  // Replay the connection handshake documented in protocol.md so the slider
  // begins pushing state, then request the initial snapshots.
  const uint8_t initData[1] = {0x02};
  sendFrame(encodeFrame(0x06, 0x18, initData, 1));
  sendFrame(buildControlPing(0x15, nextTx()));
  refreshAll();
}

void SharkClient::refreshAll() {
  if (!connected()) {
    return;
  }
  sendFrame(buildControlPing(0x00, nextTx()));  // full status (battery)
  sendFrame(buildControlPing(0x03, nextTx()));  // point-state (presence)
  sendFrame(buildTimingQuery(nextTx()));         // timing table (speed/hold)
}

bool SharkClient::sendFrame(const FrameBytes& frame, bool response) {
  if (writeChar_ == nullptr || !connected() || frame.len == 0) {
    return false;
  }
  return writeChar_->writeValue(frame.bytes, frame.len, response);
}

void SharkClient::drainNotifications() {
  if (notifyStream_ == nullptr) {
    return;
  }
  StreamBufferHandle_t stream = static_cast<StreamBufferHandle_t>(notifyStream_);
  uint8_t buf[256];
  size_t got = 0;
  while ((got = xStreamBufferReceive(stream, buf, sizeof(buf), 0)) > 0) {
    scanner_.feed(buf, got, [this](const ParsedFrame& frame) { applyFrame(frame); });
  }
}

void SharkClient::applyFrame(const ParsedFrame& frame) {
  if (frame.code == 0x08 && frame.dataLen == kTimingDataLen) {
    applyTimingTable(frame);
  }

  if (frame.family == 0x06 && frame.code == 0x00 && frame.kind == 0x0029 && frame.dataLen > 16) {
    const int battery = frame.data[2];
    if (battery >= 0 && battery <= 100) {
      state_.battery = battery;
    }
  }

  if (frame.family == 0x03 && frame.code == 0x02 && frame.kind == 0x0003 && frame.dataLen >= 3) {
    if (trackingPending_ && millis() <= trackingPendingExpiryMs_ &&
        frame.data[0] == trackingPendingTx_) {
      trackingPending_ = false;
      state_.tracking = trackingPendingValue_;
      state_.trackingKnown = true;
    }
  }

  if (frame.family == 0x06 && frame.code == 0x03 && frame.dataLen >= 2) {
    const size_t flags = frame.dataLen - 1;
    const uint8_t* p = frame.data + 1;
    for (int i = 0; i < kKeypointCount; ++i) {
      state_.present[i] = (static_cast<size_t>(i) < flags) ? (p[i] != 0) : false;
    }
    state_.presenceKnown = true;
  }

  if (isRunProgress(frame)) {
    RunProgress rp;
    if (parseRunProgress(frame, rp)) {
      state_.runProgressKnown = true;

      // The button reflects operator intent; an idle slider keeps reporting
      // "stopped", so a plain "stopped" must NOT clobber a freshly commanded
      // Standby/Start (it would snap the button back). Only accept "stopped" as
      // the end of a run we believed was actually running. "running" and an
      // explicit "standby" report are always trusted.
      const bool running = (rp.stateCode == kRunStart || rp.stateCode == 0x06);
      if (running) {
        state_.runStateCode = kRunStart;
      } else if (rp.stateCode == kRunStandby) {
        state_.runStateCode = kRunStandby;
      } else if (rp.stateCode == kRunStop) {
        if (state_.runStateCode == kRunStart || state_.runStateCode == 0x06) {
          state_.runStateCode = kRunStop;
        }
      }

      // Only show progress while the slider is actually moving. When the route
      // finishes (or is stopped/standby) the device may keep its last progress
      // value or stop notifying; treating non-running states as 0% keeps the
      // bar from freezing near the end and matches a fresh, ready-to-run UI.
      if (running) {
        // The device reports per-segment progress; estimate progress across the
        // whole route from how many travel segments the configured keypoints
        // make (present count - 1). `rp.segment` is assumed to be the current
        // segment index; tune if hardware indexes differently.
        int presentCount = 0;
        for (int i = 0; i < kKeypointCount; ++i) {
          if (state_.present[i]) {
            ++presentCount;
          }
        }
        const int totalSegments = presentCount - 1;
        if (totalSegments > 0) {
          float whole = (rp.segment + rp.progressPercent / 100.0f) / totalSegments * 100.0f;
          if (whole < 0.0f) {
            whole = 0.0f;
          } else if (whole > 100.0f) {
            whole = 100.0f;
          }
          state_.runPercent = whole;
        } else {
          state_.runPercent = rp.progressPercent;
        }
      } else {
        state_.runPercent = 0.0f;
      }

      strncpy(state_.runText, runStateLabel(rp.stateCode), sizeof(state_.runText) - 1);
      state_.runText[sizeof(state_.runText) - 1] = '\0';
    }
  }
}

void SharkClient::applyTimingTable(const ParsedFrame& frame) {
  memcpy(timingTable_, frame.data, kTimingDataLen);
  haveTable_ = true;
  state_.timingKnown = true;
  for (int i = 1; i < kKeypointCount; ++i) {
    const int base = 1 + (i - 1) * 4;
    state_.speed[i] = frame.data[base + kTimingSpeedOffset];
    state_.hold[i] = frame.data[base + kTimingHoldOffset];
  }

  if (timingPending_ && timingPendingSlot_ > 0 && timingPendingSlot_ < kKeypointCount) {
    FrameBytes out;
    if (patchTimingTable(timingTable_, kTimingDataLen, timingPendingSlot_, timingPendingSpeed_,
                         timingPendingHold_, nextTx(), out)) {
      sendFrame(out);
    }
  }
  timingPending_ = false;
  timingPendingSlot_ = -1;
  timingPendingSpeed_ = -1;
  timingPendingHold_ = -1;
}

void SharkClient::keypointSet(int slot) {
  if (!connected() || slot < 0 || slot >= kKeypointCount) {
    return;
  }
  sendFrame(buildKeypointAction(slot, kMarkerSet, nextTx()));
  sendFrame(buildControlPing(0x05, nextTx()));
  sendFrame(buildControlPing(0x03, nextTx()));
  state_.present[slot] = true;
  state_.presenceKnown = true;
}

void SharkClient::keypointGo(int slot) {
  if (!connected() || slot < 0 || slot >= kKeypointCount) {
    return;
  }
  sendFrame(buildKeypointAction(slot, kMarkerGo, nextTx()));
}

void SharkClient::keypointDelete(int slot) {
  if (!connected() || slot < 0 || slot >= kKeypointCount) {
    return;
  }
  sendFrame(buildKeypointDelete(slot, state_.present, nextTx()));
  sendFrame(buildControlPing(0x05, nextTx()));
  sendFrame(buildControlPing(0x03, nextTx()));
  // Optimistic cascade: clears the target and any later configured slots.
  for (int i = slot; i < kKeypointCount; ++i) {
    if (i == slot || state_.present[i]) {
      state_.present[i] = false;
    }
  }
}

void SharkClient::editTiming(int slot, int speed, int holdSeconds) {
  if (!connected() || slot <= 0 || slot >= kKeypointCount) {
    return;  // A (slot 0) has no inbound travel segment.
  }
  if (haveTable_) {
    FrameBytes out;
    if (patchTimingTable(timingTable_, kTimingDataLen, slot, speed, holdSeconds, nextTx(), out)) {
      sendFrame(out);
      if (speed >= 0) {
        state_.speed[slot] = speed;
      }
      if (holdSeconds >= 0) {
        state_.hold[slot] = holdSeconds;
      }
    }
    return;
  }

  // No table yet: stash the edit and request the table; applied on arrival.
  timingPending_ = true;
  timingPendingSlot_ = slot;
  if (speed >= 0) {
    timingPendingSpeed_ = speed;
  }
  if (holdSeconds >= 0) {
    timingPendingHold_ = holdSeconds;
  }
  requestTiming();
}

void SharkClient::setSpeed(int slot, int percent) {
  if (percent < 0) {
    percent = 0;
  } else if (percent > 100) {
    percent = 100;
  }
  editTiming(slot, percent, -1);
}

void SharkClient::setHold(int slot, int seconds) {
  if (seconds < 0) {
    seconds = 0;
  } else if (seconds > 255) {
    seconds = 255;
  }
  editTiming(slot, -1, seconds);
}

void SharkClient::requestTiming() {
  if (!connected()) {
    return;
  }
  sendFrame(buildTimingQuery(nextTx()));
}

void SharkClient::setRunState(uint8_t runState) {
  if (!connected()) {
    return;
  }
  sendFrame(buildRunState(runState, nextTx()));
  const char* text = "idle";
  if (runState == kRunStart) {
    text = "running";
  } else if (runState == kRunStandby) {
    text = "standby";
  } else if (runState == kRunStop) {
    text = "stopped";
  }
  state_.runStateCode = runState;
  // A fresh command clears any stale/frozen progress; live notifications
  // repopulate it once a new run actually starts moving.
  state_.runProgressKnown = false;
  state_.runPercent = 0.0f;
  strncpy(state_.runText, text, sizeof(state_.runText) - 1);
  state_.runText[sizeof(state_.runText) - 1] = '\0';
}

void SharkClient::setLoop(bool on) {
  if (!connected()) {
    return;
  }
  sendFrame(buildLoop(on, nextTx()));
  state_.loopOn = on;
}

void SharkClient::setDirection(bool reverse) {
  if (!connected()) {
    return;
  }
  sendFrame(buildDirection(reverse, nextTx()));
  state_.reverse = reverse;
}

void SharkClient::setManualTracking(bool enabled) {
  if (!connected()) {
    return;
  }
  const uint8_t tx = nextTx();
  sendFrame(buildManualTracking(enabled, tx));
  trackingPending_ = true;
  trackingPendingTx_ = tx;
  trackingPendingValue_ = enabled;
  trackingPendingExpiryMs_ = millis() + 5000;
  state_.tracking = enabled;
  state_.trackingKnown = true;
}

void SharkClient::onScanMatch(const NimBLEAdvertisedDevice* device) {
  if (scanHit_) {
    return;  // already captured a candidate; ignore until loop() consumes it
  }
  const std::string addr = device->getAddress().toString();
  // When a device is remembered, only auto-reconnect to that exact address so
  // we don't latch onto a different nearby slider. Pairing (no saved device)
  // accepts the first matching Shark Nano.
  if (haveTarget_ && targetAddr_[0] != '\0' && addr != targetAddr_) {
    return;
  }
  strncpy(scanHitAddr_, addr.c_str(), sizeof(scanHitAddr_) - 1);
  scanHitAddr_[sizeof(scanHitAddr_) - 1] = '\0';
  scanHitType_ = device->getAddressType();
  const std::string name = device->getName();
  strncpy(scanHitName_, name.empty() ? "Shark Nano II" : name.c_str(), sizeof(scanHitName_) - 1);
  scanHitName_[sizeof(scanHitName_) - 1] = '\0';
  scanHit_ = true;
}

void SharkClient::onLinkDisconnected() { disconnectedFlag_ = true; }

void SharkClient::onNotifyBytes(const uint8_t* data, size_t len) {
  if (notifyStream_ == nullptr || data == nullptr || len == 0) {
    return;
  }
  StreamBufferHandle_t stream = static_cast<StreamBufferHandle_t>(notifyStream_);
  xStreamBufferSend(stream, data, len, 0);
}

void SharkClient::resetDeviceState() {
  state_.battery = -1;
  state_.presenceKnown = false;
  for (int i = 0; i < kKeypointCount; ++i) {
    state_.present[i] = false;
    state_.speed[i] = -1;
    state_.hold[i] = -1;
  }
  state_.timingKnown = false;
  haveTable_ = false;
  state_.trackingKnown = false;
  state_.tracking = false;
  state_.runProgressKnown = false;
  state_.runPercent = 0.0f;
  state_.runStateCode = kRunStop;
  strncpy(state_.runText, "idle", sizeof(state_.runText) - 1);
  state_.runText[sizeof(state_.runText) - 1] = '\0';
  timingPending_ = false;
  trackingPending_ = false;
}

void SharkClient::loadSavedDevice() {
  prefs_.begin("shark", true);
  String addr = prefs_.getString("addr", "");
  uint8_t type = prefs_.getUChar("atype", 0);
  String name = prefs_.getString("name", "");
  prefs_.end();

  if (addr.length() > 0) {
    strncpy(targetAddr_, addr.c_str(), sizeof(targetAddr_) - 1);
    targetAddr_[sizeof(targetAddr_) - 1] = '\0';
    targetAddrType_ = type;
    strncpy(targetName_, name.c_str(), sizeof(targetName_) - 1);
    targetName_[sizeof(targetName_) - 1] = '\0';
    haveTarget_ = true;
    state_.hasSavedDevice = true;
    strncpy(state_.deviceName, targetName_, sizeof(state_.deviceName) - 1);
    state_.deviceName[sizeof(state_.deviceName) - 1] = '\0';
  }
}

void SharkClient::saveDevice() {
  prefs_.begin("shark", false);
  prefs_.putString("addr", targetAddr_);
  prefs_.putUChar("atype", targetAddrType_);
  prefs_.putString("name", targetName_);
  prefs_.end();
}

void SharkClient::scheduleRetry(uint32_t& whenMs, uint32_t delayMs) { whenMs = millis() + delayMs; }

}  // namespace shark
