#include "wifi_configuration.h"

#include "wifi_scan.h"

#include <algorithm>
#include <cctype>
#include <cstring>

#if !defined(UI_SIMULATOR) && !defined(WIFI_CONFIGURATION_NATIVE)
#include <Arduino.h>
#include <WiFi.h>
#include <esp_heap_caps.h>

#include "core/preferences_store.h"

#if ARDUINO_USB_CDC_ON_BOOT
#define WIFI_CONFIGURATION_DEBUG_PORT Serial0
#else
#define WIFI_CONFIGURATION_DEBUG_PORT Serial
#endif
#endif

namespace wifi_configuration {

bool validPassword(bool secure, const char* password) {
  if (!secure) return password == nullptr || password[0] == '\0';
  if (password == nullptr) return false;
  const size_t length = std::strlen(password);
  if (length == 64) {
    for (size_t i = 0; i < length; ++i) {
      if (!std::isxdigit(static_cast<unsigned char>(password[i]))) return false;
    }
    return true;
  }
  if (length < 8 || length > 63) return false;
  for (size_t i = 0; i < length; ++i) {
    const unsigned char value = static_cast<unsigned char>(password[i]);
    if (value < 0x20 || value > 0x7e) return false;
  }
  return true;
}

const Network* WifiConfigurationService::network(size_t index) const {
  return index < snapshot_.networkCount ? &networks_[index] : nullptr;
}

bool WifiConfigurationService::active() const {
  return snapshot_.status == Status::WaitingForRelease ||
         snapshot_.status == Status::Scanning ||
         snapshot_.status == Status::Results ||
         snapshot_.status == Status::Connecting ||
         snapshot_.status == Status::Saving ||
         snapshot_.status == Status::Stopping;
}

void WifiConfigurationService::setStatus(Status status, const char* message) {
  snapshot_.status = status;
  std::strncpy(snapshot_.message, message != nullptr ? message : "",
               sizeof(snapshot_.message) - 1);
  snapshot_.message[sizeof(snapshot_.message) - 1] = '\0';
}

#if defined(UI_SIMULATOR) || defined(WIFI_CONFIGURATION_NATIVE)

void WifiConfigurationService::begin() { refreshSaved(); }
void WifiConfigurationService::loop() {}

void WifiConfigurationService::refreshSaved() {}

bool WifiConfigurationService::requestScan() {
  setStatus(Status::Scanning, "Scanning nearby networks...");
  return true;
}

bool WifiConfigurationService::connect(size_t networkIndex, const char* password) {
  const Network* selected = network(networkIndex);
  if (selected == nullptr || !validPassword(selected->secure, password)) return false;
  std::strncpy(snapshot_.selectedSsid, selected->ssid,
               sizeof(snapshot_.selectedSsid) - 1);
  setStatus(Status::Connecting, "Connecting...");
  return true;
}

void WifiConfigurationService::cancel() { setStatus(Status::Idle, "Ready"); }

bool WifiConfigurationService::forget() {
  snapshot_.configured = false;
  snapshot_.savedSsid[0] = '\0';
  setStatus(Status::Succeeded, "Saved network forgotten");
  return true;
}

void WifiConfigurationService::beginStop(Status, const char*) {}

void WifiConfigurationService::simSetSaved(const char* ssid) {
  snapshot_.configured = ssid != nullptr && ssid[0] != '\0';
  std::strncpy(snapshot_.savedSsid, ssid != nullptr ? ssid : "",
               sizeof(snapshot_.savedSsid) - 1);
}

void WifiConfigurationService::simSetResults() {
  networks_[0] = Network{};
  std::strncpy(networks_[0].ssid, "Studio-WiFi", sizeof(networks_[0].ssid) - 1);
  networks_[0].rssi = -42;
  networks_[0].secure = true;
  networks_[1] = Network{};
  std::strncpy(networks_[1].ssid, "Guest", sizeof(networks_[1].ssid) - 1);
  networks_[1].rssi = -67;
  networks_[1].secure = false;
  snapshot_.networkCount = 2;
  setStatus(Status::Results, "Select a network");
}

void WifiConfigurationService::simSetConnecting(const char* ssid) {
  std::strncpy(snapshot_.selectedSsid, ssid != nullptr ? ssid : "Studio-WiFi",
               sizeof(snapshot_.selectedSsid) - 1);
  setStatus(Status::Connecting, "Connecting...");
}

void WifiConfigurationService::simSetOutcome(bool success, const char* message) {
  setStatus(success ? Status::Succeeded : Status::Failed,
            message != nullptr ? message : success ? "Wi-Fi saved" : "Connection failed");
}

#else

namespace {

constexpr uint32_t kReleaseTimeoutMs = 2000;
constexpr uint32_t kReleaseSettleMs = 300;
constexpr uint32_t kScanTimeoutMs = 15000;
constexpr uint32_t kConnectTimeoutMs = 15000;
constexpr uint32_t kShutdownSettleMs = 250;
constexpr uint32_t kShutdownTimeoutMs = 2000;
constexpr uint32_t kMinimumFreeHeap = 48000;
constexpr uint32_t kMinimumLargestBlock = 36000;

bool radioOff() { return WiFi.getMode() == WIFI_OFF; }

}  // namespace

void WifiConfigurationService::refreshSaved() {
  studio::PreferencesHomeAssistantBackend backend;
  studio::HomeAssistantConfigStore store(backend);
  studio::HomeAssistantConfig config;
  const studio::ConfigLoadStatus loaded = store.load(config);
  snapshot_.configured = loaded != studio::ConfigLoadStatus::Corrupt &&
                         config.wifiConfigured && config.wifiSsid[0] != '\0';
  std::strncpy(snapshot_.savedSsid,
               snapshot_.configured ? config.wifiSsid : "",
               sizeof(snapshot_.savedSsid) - 1);
}

void WifiConfigurationService::begin() {
  snapshot_ = Snapshot{};
  refreshSaved();
}

bool WifiConfigurationService::requestScan() {
  if (active()) return false;
  snapshot_.networkCount = 0;
  snapshot_.selectedSsid[0] = '\0';
  setStatus(Status::WaitingForRelease, "Preparing Wi-Fi...");
  stateStarted_ = millis();
  return true;
}

bool WifiConfigurationService::connect(size_t networkIndex, const char* password) {
  if (snapshot_.status != Status::Results) return false;
  const Network* selected = network(networkIndex);
  if (selected == nullptr || !validPassword(selected->secure, password)) return false;
  std::strncpy(snapshot_.selectedSsid, selected->ssid,
               sizeof(snapshot_.selectedSsid) - 1);
  std::strncpy(pendingPassword_, password != nullptr ? password : "",
               sizeof(pendingPassword_) - 1);
  WiFi.disconnect(false, false);
  WiFi.mode(WIFI_STA);
  WiFi.begin(snapshot_.selectedSsid, pendingPassword_);
  setStatus(Status::Connecting, "Connecting to network...");
  stateStarted_ = millis();
  return true;
}

void WifiConfigurationService::beginStop(Status finalStatus, const char* message) {
  wifi_scan::cancel();
  WiFi.disconnect(false, false);
  afterStop_ = finalStatus;
  std::strncpy(afterStopMessage_, message != nullptr ? message : "",
               sizeof(afterStopMessage_) - 1);
  setStatus(Status::Stopping, "Turning Wi-Fi off...");
  stateStarted_ = millis();
}

void WifiConfigurationService::cancel() {
  if (!active()) return;
  beginStop(Status::Idle, "Ready");
}

bool WifiConfigurationService::forget() {
  if (active()) return false;
  studio::PreferencesHomeAssistantBackend backend;
  studio::HomeAssistantConfigStore store(backend);
  studio::HomeAssistantConfig config;
  const studio::ConfigLoadStatus loaded = store.load(config);
  if (loaded == studio::ConfigLoadStatus::Corrupt) return false;
  config.wifiConfigured = false;
  config.wifiSsid[0] = '\0';
  config.wifiPassword[0] = '\0';
  if (!store.save(config)) return false;
  refreshSaved();
  setStatus(Status::Succeeded, "Saved network forgotten");
  return true;
}

void WifiConfigurationService::loop() {
  const uint32_t now = millis();
  if (snapshot_.status == Status::WaitingForRelease) {
    const uint32_t elapsed = now - stateStarted_;
    if (elapsed < kReleaseSettleMs) return;
    if (!radioOff() && elapsed < kReleaseTimeoutMs) return;
    if (!radioOff()) {
      WiFi.disconnect(true, false);
      WiFi.mode(WIFI_OFF);
    }
    if (ESP.getFreeHeap() < kMinimumFreeHeap ||
        heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) < kMinimumLargestBlock) {
      setStatus(Status::Failed, "Not enough free memory to scan");
      return;
    }
    WiFi.mode(WIFI_STA);
    const bool started = wifi_scan::start();
    WIFI_CONFIGURATION_DEBUG_PORT.printf(
        "wifi config scan started=%u free_heap=%u max_alloc=%u\n",
        static_cast<unsigned>(started),
        static_cast<unsigned>(ESP.getFreeHeap()),
        static_cast<unsigned>(
            heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)));
    if (!started) {
      beginStop(Status::Failed, "Could not start Wi-Fi scan");
      return;
    }
    setStatus(Status::Scanning, "Scanning nearby networks...");
    stateStarted_ = now;
    return;
  }
  if (snapshot_.status == Status::Scanning) {
    const int found = wifi_scan::complete();
    if (found == WIFI_SCAN_RUNNING) {
      if (now - stateStarted_ >= kScanTimeoutMs) {
        beginStop(Status::Failed, "Wi-Fi scan timed out");
      }
      return;
    }
    if (found < 0) {
      WIFI_CONFIGURATION_DEBUG_PORT.printf(
          "wifi config scan result=%d elapsed_ms=%u\n", found,
          static_cast<unsigned>(now - stateStarted_));
      beginStop(Status::Failed, "Wi-Fi scan failed");
      return;
    }
    snapshot_.networkCount = 0;
    for (int i = 0; i < found; ++i) {
      const String candidate = WiFi.SSID(i);
      if (candidate.length() == 0 || candidate.length() >= studio::kWifiSsidCapacity) continue;
      bool duplicate = false;
      for (size_t existing = 0; existing < snapshot_.networkCount; ++existing) {
        duplicate |= candidate == networks_[existing].ssid;
      }
      if (duplicate) continue;
      Network entry;
      std::strncpy(entry.ssid, candidate.c_str(), sizeof(entry.ssid) - 1);
      entry.rssi = WiFi.RSSI(i);
      entry.secure = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
      size_t insert = snapshot_.networkCount;
      while (insert > 0 && networks_[insert - 1].rssi < entry.rssi) {
        if (insert < kMaximumNetworks) networks_[insert] = networks_[insert - 1];
        --insert;
      }
      if (insert < kMaximumNetworks) networks_[insert] = entry;
      if (snapshot_.networkCount < kMaximumNetworks) ++snapshot_.networkCount;
    }
    WiFi.scanDelete();
    WIFI_CONFIGURATION_DEBUG_PORT.printf(
        "wifi config scan result=%d cached=%u elapsed_ms=%u\n", found,
        static_cast<unsigned>(snapshot_.networkCount),
        static_cast<unsigned>(now - stateStarted_));
    setStatus(snapshot_.networkCount > 0 ? Status::Results : Status::Failed,
              snapshot_.networkCount > 0 ? "Select a network" : "No networks found");
    if (snapshot_.networkCount == 0) beginStop(Status::Failed, "No networks found");
    return;
  }
  if (snapshot_.status == Status::Connecting) {
    if (WiFi.status() == WL_CONNECTED && static_cast<uint32_t>(WiFi.localIP()) != 0) {
      setStatus(Status::Saving, "Saving network...");
      studio::PreferencesHomeAssistantBackend backend;
      studio::HomeAssistantConfigStore store(backend);
      studio::HomeAssistantConfig config;
      const studio::ConfigLoadStatus loaded = store.load(config);
      if (loaded == studio::ConfigLoadStatus::Corrupt) {
        beginStop(Status::Failed, "Saved settings are corrupt");
        return;
      }
      config.wifiConfigured = true;
      std::strncpy(config.wifiSsid, snapshot_.selectedSsid,
                   sizeof(config.wifiSsid) - 1);
      std::strncpy(config.wifiPassword, pendingPassword_,
                   sizeof(config.wifiPassword) - 1);
      if (!store.save(config)) {
        beginStop(Status::Failed, "Could not save network");
        return;
      }
      refreshSaved();
      std::memset(pendingPassword_, 0, sizeof(pendingPassword_));
      beginStop(Status::Succeeded, "Wi-Fi connected and saved");
      return;
    }
    if (WiFi.status() == WL_CONNECT_FAILED || now - stateStarted_ >= kConnectTimeoutMs) {
      std::memset(pendingPassword_, 0, sizeof(pendingPassword_));
      beginStop(Status::Failed, "Connection failed; previous network kept");
    }
    return;
  }
  if (snapshot_.status == Status::Stopping &&
      (now - stateStarted_ >= kShutdownSettleMs &&
       (WiFi.status() != WL_CONNECTED || now - stateStarted_ >= kShutdownTimeoutMs))) {
    WiFi.mode(WIFI_OFF);
    setStatus(afterStop_, afterStopMessage_);
  }
}

#endif

WifiConfigurationService& service() {
  static WifiConfigurationService instance;
  return instance;
}

}  // namespace wifi_configuration
