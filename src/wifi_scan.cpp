#include "wifi_scan.h"

#if !defined(UI_SIMULATOR) && !defined(WIFI_CONFIGURATION_NATIVE)

#include <Arduino.h>
#include <WiFi.h>
#include <esp_event.h>
#include <esp_wifi.h>

namespace wifi_scan {
namespace {

constexpr uint32_t kMaxMsPerChannel = 300;
constexpr uint32_t kScanTimeoutMs = 15000;
constexpr uint32_t kArduinoResultSettleMs = 500;

esp_event_handler_instance_t handler = nullptr;
volatile bool scanDone = false;
volatile uint32_t scanStatus = 0;
uint32_t scanStartedAt = 0;
uint32_t scanDoneObservedAt = 0;

void onScanDone(void*, esp_event_base_t, int32_t, void* data) {
  const auto* event = static_cast<const wifi_event_sta_scan_done_t*>(data);
  scanStatus = event != nullptr ? event->status : 1;
  scanDone = true;
}

void unregisterHandler() {
  if (handler == nullptr) return;
  esp_event_handler_instance_unregister(WIFI_EVENT, WIFI_EVENT_SCAN_DONE, handler);
  handler = nullptr;
}

}  // namespace

bool start() {
  cancel();
  scanDone = false;
  scanStatus = 0;
  scanDoneObservedAt = 0;
  if (esp_event_handler_instance_register(
          WIFI_EVENT, WIFI_EVENT_SCAN_DONE, onScanDone, nullptr, &handler) != ESP_OK) {
    handler = nullptr;
    return false;
  }

  // Arduino-ESP32 2.0.17 does not initialize home_chan_dwell_time in its
  // WiFiScan wrapper. ESP-IDF accepts that garbage value but may block the
  // scan, in which case WIFI_EVENT_SCAN_DONE is deliberately never emitted.
  wifi_scan_config_t config = {};
  config.show_hidden = true;
  config.scan_type = WIFI_SCAN_TYPE_ACTIVE;
  config.scan_time.active.min = 100;
  config.scan_time.active.max = kMaxMsPerChannel;
  if (esp_wifi_scan_start(&config, false) != ESP_OK) {
    unregisterHandler();
    return false;
  }
  scanStartedAt = millis();
  return true;
}

int complete() {
  const uint32_t now = millis();
  if (!scanDone) {
    if (now - scanStartedAt < kScanTimeoutMs) return WIFI_SCAN_RUNNING;
    esp_wifi_scan_stop();
    unregisterHandler();
    return WIFI_SCAN_FAILED;
  }
  if (scanStatus != 0) {
    unregisterHandler();
    return WIFI_SCAN_FAILED;
  }

  // Arduino's registered event bridge consumes the ESP-IDF records and
  // populates its bounded result accessors. It runs on the event task, so give
  // that bridge a short main-loop-safe settling window after the raw event.
  const int found = WiFi.scanComplete();
  if (found >= 0) {
    unregisterHandler();
    return found;
  }
  if (scanDoneObservedAt == 0) scanDoneObservedAt = now;
  if (now - scanDoneObservedAt < kArduinoResultSettleMs) return WIFI_SCAN_RUNNING;
  unregisterHandler();
  return WIFI_SCAN_FAILED;
}

void cancel() {
  if (handler != nullptr) esp_wifi_scan_stop();
  unregisterHandler();
  WiFi.scanDelete();
  scanDone = false;
  scanStatus = 0;
  scanDoneObservedAt = 0;
}

}  // namespace wifi_scan

#endif
