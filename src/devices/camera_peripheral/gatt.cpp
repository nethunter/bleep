#include "devices/camera_peripheral/gatt.h"

#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>

#include <cstring>
#include <new>

#include "core/ble/peripheral_dispatcher.h"
#include "devices/insta360/protocol.h"
#include "driver_config.h"

namespace studio::camera_peripheral {
namespace {

constexpr uint8_t kConsumerReportMap[] = {
    0x05, 0x0c, 0x09, 0x01, 0xa1, 0x01, 0x85, 0x01,
    0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x01,
    0x09, 0xe9, 0x81, 0x06, 0x75, 0x07, 0x95, 0x01,
    0x81, 0x03, 0xc0,
};

NimBLEHIDDevice* gHid = nullptr;
NimBLECharacteristic* gPhoneInput = nullptr;

bool ensureInsta360(NimBLEServer* server, GattServices& services) {
#if CONFIG_DRIVER_INSTA360
  NimBLEService* service =
      server->getServiceByUUID(NimBLEUUID(insta360::kServiceUuid));
  if (service == nullptr) {
    service = server->createService(insta360::kServiceUuid);
  }
  if (service == nullptr) return false;
  // The X5 capture and the working CoreBluetooth harness both declare these
  // in CE82, CE81, CE83 order. Preserve it because the camera performs a full
  // profile walk before enabling CE82 and sending its unsolicited CE81 state.
  services.instaNotify =
      service->getCharacteristic(insta360::kGattCharacteristicOrder[0]);
  if (services.instaNotify == nullptr) {
    services.instaNotify = service->createCharacteristic(
        insta360::kGattCharacteristicOrder[0], NIMBLE_PROPERTY::NOTIFY);
  }
  services.instaWrite =
      service->getCharacteristic(insta360::kGattCharacteristicOrder[1]);
  if (services.instaWrite == nullptr) {
    services.instaWrite = service->createCharacteristic(
        insta360::kGattCharacteristicOrder[1],
        NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  }
  NimBLECharacteristic* info =
      service->getCharacteristic(insta360::kGattCharacteristicOrder[2]);
  if (info == nullptr) {
    info = service->createCharacteristic(insta360::kGattCharacteristicOrder[2],
                                         NIMBLE_PROPERTY::READ);
  }
  return services.instaWrite != nullptr && services.instaNotify != nullptr &&
         info != nullptr;
#else
  (void)server;
  (void)services;
  return true;
#endif
}

bool ensureCameraHid(NimBLEServer* server, GattServices& services) {
#if CONFIG_DRIVER_PHONE_CAMERA || CONFIG_DRIVER_INSTA360
  if (gHid == nullptr) {
    gHid = new (std::nothrow) NimBLEHIDDevice(server);
    if (gHid == nullptr) return false;
    gHid->setManufacturer("Ble(e)p");
    gHid->setPnp(0x02, 0x1209, 0x0001, 0x0001);
    gHid->setHidInfo(0x00, 0x01);
    gPhoneInput = gHid->getInputReport(1);
    uint8_t reportMap[sizeof(kConsumerReportMap)];
    std::memcpy(reportMap, kConsumerReportMap, sizeof(reportMap));
    gHid->setReportMap(reportMap, sizeof(reportMap));
    gHid->setBatteryLevel(100);
  }
#if CONFIG_DRIVER_PHONE_CAMERA
  services.phoneInput = gPhoneInput;
  return services.phoneInput != nullptr;
#else
  (void)services;
  return true;
#endif
#else
  (void)server;
  (void)services;
  return true;
#endif
}

}  // namespace

bool ensureGattServices(NimBLEServer* server, GattServices& services) {
  if (server == nullptr) return false;
  const bool missingInsta =
#if CONFIG_DRIVER_INSTA360
      server->getServiceByUUID(NimBLEUUID(insta360::kServiceUuid)) == nullptr;
#else
      false;
#endif
  const bool missingHid =
#if CONFIG_DRIVER_PHONE_CAMERA || CONFIG_DRIVER_INSTA360
      gHid == nullptr;
#else
      false;
#endif
  if ((missingInsta || missingHid) &&
      !ble::preparePeripheralGattMutation(server)) {
    return false;
  }
  if (!ensureInsta360(server, services) ||
      !ensureCameraHid(server, services)) {
    return false;
  }
  return server->start();
}

}  // namespace studio::camera_peripheral
