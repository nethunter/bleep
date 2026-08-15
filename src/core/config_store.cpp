#include "core/config_store.h"

#include "core/blob_codec.h"

#include <cstring>
#include <memory>
#include <new>

namespace studio {

namespace {

constexpr uint8_t kMagic[] = {'S', 'T', 'D', 'V'};
constexpr size_t kHeaderSize = 12;
constexpr size_t kEncodedRecordSize =
    4 + 2 + 1 + 1 + kDeviceNameCapacity + kBleAddressCapacity +
    kBleNameCapacity + 1 + kHomeAssistantEntityIdCapacity;
constexpr size_t kChecksumSize = 4;

static_assert(kHeaderSize +
                      CONFIG_MAX_DEVICE_INSTANCES * kEncodedRecordSize +
                      kChecksumSize <=
                  ConfigStore::kMaxBlobSize,
              "ConfigStore blob is too small for configured device capacity");

}  // namespace

ConfigLoadStatus ConfigStore::load(DeviceRegistry& registry) {
  std::unique_ptr<uint8_t[]> blob(
      new (std::nothrow) uint8_t[kMaxBlobSize]);
  if (!blob) return ConfigLoadStatus::Corrupt;
  const size_t length = backend_.read(blob.get(), kMaxBlobSize);
  if (length == 0) {
    return ConfigLoadStatus::Missing;
  }
  if (length < kHeaderSize + kChecksumSize ||
      std::memcmp(blob.get(), kMagic, sizeof(kMagic)) != 0) {
    return ConfigLoadStatus::Corrupt;
  }

  BlobReader reader(blob.get() + sizeof(kMagic), length - sizeof(kMagic));
  uint16_t version = 0;
  uint8_t initializedValue = 0;
  uint8_t count = 0;
  InstanceId nextInstanceId = 0;
  if (!reader.u16(version) || !reader.u8(initializedValue) ||
      !reader.u8(count) || !reader.u32(nextInstanceId)) {
    return ConfigLoadStatus::Corrupt;
  }
  const size_t expectedLength =
      kHeaderSize + static_cast<size_t>(count) * kEncodedRecordSize + kChecksumSize;
  if (version != kSchemaVersion || initializedValue > 1 ||
      count > CONFIG_MAX_DEVICE_INSTANCES ||
      length != expectedLength) {
    return ConfigLoadStatus::Corrupt;
  }

  BlobReader checksumReader(blob.get() + length - kChecksumSize, kChecksumSize);
  uint32_t storedChecksum = 0;
  if (!checksumReader.u32(storedChecksum) ||
      storedChecksum != fnv1a(blob.get(), length - kChecksumSize)) {
    return ConfigLoadStatus::Corrupt;
  }

  std::unique_ptr<DeviceRecord[]> records(
      new (std::nothrow) DeviceRecord[CONFIG_MAX_DEVICE_INSTANCES]());
  if (!records) return ConfigLoadStatus::Corrupt;
  for (size_t i = 0; i < count; ++i) {
    DeviceRecord& record = records[i];
    uint16_t driverId = 0;
    uint8_t flags = 0;
    if (!reader.u32(record.instanceId) || !reader.u16(driverId) ||
        !reader.u8(flags) || !reader.u8(record.bleAddressType) ||
        !reader.text(record.displayName) || !reader.text(record.bleAddress) ||
        !reader.text(record.bleName)) {
      return ConfigLoadStatus::Corrupt;
    }
    record.driverId = static_cast<DriverId>(driverId);
    record.enabled = (flags & 0x01) != 0;
    record.paired = (flags & 0x02) != 0;
    uint8_t domain = 0;
    if (!reader.u8(domain) || !reader.text(record.homeAssistantEntityId)) {
      return ConfigLoadStatus::Corrupt;
    }
    record.homeAssistantDomain = static_cast<HomeAssistantDomain>(domain);
  }

  if (reader.position() != length - sizeof(kMagic) - kChecksumSize) {
    return ConfigLoadStatus::Corrupt;
  }

  if (!registry.restore(records.get(), count, nextInstanceId,
                        initializedValue != 0)) {
    return ConfigLoadStatus::Corrupt;
  }
  return ConfigLoadStatus::Loaded;
}

bool ConfigStore::save(const DeviceRegistry& registry) {
  const size_t length =
      kHeaderSize + registry.count() * kEncodedRecordSize + kChecksumSize;
  if (length > kMaxBlobSize || registry.count() > 255) {
    return false;
  }

  std::unique_ptr<uint8_t[]> blob(
      new (std::nothrow) uint8_t[length]());
  if (!blob) return false;
  BlobWriter writer(blob.get(), length);
  writer.bytes(kMagic, sizeof(kMagic));
  writer.u16(kSchemaVersion);
  writer.u8(registry.initialized() ? 1 : 0);
  writer.u8(static_cast<uint8_t>(registry.count()));
  writer.u32(registry.nextInstanceId());

  for (size_t i = 0; i < registry.count(); ++i) {
    const DeviceRecord* record = registry.at(i);
    writer.u32(record->instanceId);
    writer.u16(static_cast<uint16_t>(record->driverId));
    writer.u8(static_cast<uint8_t>((record->enabled ? 0x01 : 0x00) |
                                   (record->paired ? 0x02 : 0x00)));
    writer.u8(record->bleAddressType);
    writer.bytes(record->displayName, sizeof(record->displayName));
    writer.bytes(record->bleAddress, sizeof(record->bleAddress));
    writer.bytes(record->bleName, sizeof(record->bleName));
    writer.u8(static_cast<uint8_t>(record->homeAssistantDomain));
    writer.bytes(record->homeAssistantEntityId,
                 sizeof(record->homeAssistantEntityId));
  }

  writer.u32(fnv1a(blob.get(), length - kChecksumSize));
  return writer.valid() && writer.size() == length &&
         backend_.write(blob.get(), length);
}

}  // namespace studio
