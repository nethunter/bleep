#pragma once

#include <cstdint>

// Remembers the access point (channel and BSSID) of the last successful
// station association so later joins in the same boot can skip the full
// channel scan. Purely an optimization: a failed fast join invalidates the
// cache and the next attempt scans normally.
namespace wifi_station_cache {

// Start a station join for `ssid`; uses the cached channel/BSSID when the
// cache matches the SSID. Returns true when a fast (cached) attempt began.
bool begin(const char* ssid, const char* password);
// Record the currently associated access point for `ssid`.
void rememberCurrent(const char* ssid);
// Drop the cache, for example after a fast attempt timed out.
void invalidate();
bool valid();

}  // namespace wifi_station_cache
