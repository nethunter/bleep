#include "core/ble/ble_runtime.h"

#include "core/ble/ble_nimble_backend.h"

namespace studio::ble {

BleCentral& bleCentral() {
  static BleNimbleBackend backend;
  static BleCentral central(backend);
  return central;
}

void loopBleRuntime(uint32_t nowMs) {
  bleCentral().loop(nowMs);
}

}  // namespace studio::ble
