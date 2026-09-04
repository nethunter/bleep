#include "wifi_station_cache.h"

#include <WiFi.h>

#include <cstring>

namespace wifi_station_cache {
namespace {

bool cacheValid = false;
int32_t cachedChannel = 0;
uint8_t cachedBssid[6] = {};
char cachedSsid[33] = "";

}  // namespace

bool begin(const char* ssid, const char* password) {
  if (ssid == nullptr) return false;
  if (cacheValid && std::strcmp(cachedSsid, ssid) == 0) {
    WiFi.begin(ssid, password, cachedChannel, cachedBssid);
    return true;
  }
  WiFi.begin(ssid, password);
  return false;
}

void rememberCurrent(const char* ssid) {
  if (ssid == nullptr || WiFi.status() != WL_CONNECTED) return;
  const uint8_t* bssid = WiFi.BSSID();
  const int32_t channel = WiFi.channel();
  if (bssid == nullptr || channel <= 0) return;
  std::memcpy(cachedBssid, bssid, sizeof(cachedBssid));
  cachedChannel = channel;
  std::strncpy(cachedSsid, ssid, sizeof(cachedSsid) - 1);
  cachedSsid[sizeof(cachedSsid) - 1] = '\0';
  cacheValid = true;
}

void invalidate() { cacheValid = false; }

bool valid() { return cacheValid; }

}  // namespace wifi_station_cache
