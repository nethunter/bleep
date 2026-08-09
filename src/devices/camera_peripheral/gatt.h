#pragma once

class NimBLECharacteristic;
class NimBLEServer;

namespace studio::camera_peripheral {

struct GattServices {
  NimBLECharacteristic* instaWrite = nullptr;
  NimBLECharacteristic* instaNotify = nullptr;
  NimBLECharacteristic* phoneInput = nullptr;
};

// Registers every compiled peripheral-camera service before the first peer can
// connect. NimBLE cannot safely extend the GATT table after a retained camera
// or phone connection exists.
bool ensureGattServices(NimBLEServer* server, GattServices& services);

}  // namespace studio::camera_peripheral
