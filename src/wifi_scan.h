#pragma once

namespace wifi_scan {

// Starts one bounded asynchronous active scan. Results remain available
// through Arduino WiFi's SSID/RSSI/encryption accessors after complete()
// returns a non-negative count.
bool start();
int complete();
void cancel();

}  // namespace wifi_scan
