#include "core/config_store.h"

#include <cstring>
#include <memory>
#include <new>

namespace studio {

namespace {

constexpr uint8_t kMagic[] = {'S', 'T', 'D', 'V'};
constexpr size_t kHeaderSize = 12;
constexpr size_t kEncodedV1RecordSize =
    4 + 2 + 1 + 1 + kDeviceNameCapacity + kBleAddressCapacity + kBleNameCapacity;
constexpr size_t kEncodedRecordSize =
    kEncodedV1RecordSize + 1 + kHomeAssistantEntityIdCapacity;
constexpr size_t kChecksumSize = 4;

static_assert(kHeaderSize +
                      CONFIG_MAX_DEVICE_INSTANCES * kEncodedRecordSize +
                      kChecksumSize <=
                  ConfigStore::kMaxBlobSize,
              "ConfigStore blob is too small for configured device capacity");

uint32_t checksum(const uint8_t* data, size_t length) {
  uint32_t value = 2166136261u;
  for (size_t i = 0; i < length; ++i) {
    value ^= data[i];
    value *= 16777619u;
  }
  return value;
}

void putU16(uint8_t*& out, uint16_t value) {
  *out++ = static_cast<uint8_t>(value & 0xFF);
  *out++ = static_cast<uint8_t>((value >> 8) & 0xFF);
}

void putU32(uint8_t*& out, uint32_t value) {
  for (int shift = 0; shift < 32; shift += 8) {
    *out++ = static_cast<uint8_t>((value >> shift) & 0xFF);
  }
}

uint16_t getU16(const uint8_t*& in) {
  const uint16_t value = static_cast<uint16_t>(in[0]) |
                         (static_cast<uint16_t>(in[1]) << 8);
  in += 2;
  return value;
}

uint32_t getU32(const uint8_t*& in) {
  uint32_t value = 0;
  for (int shift = 0; shift < 32; shift += 8) {
    value |= static_cast<uint32_t>(*in++) << shift;
  }
  return value;
}

template <size_t N>
void decodeText(char (&destination)[N], const uint8_t*& in) {
  std::memcpy(destination, in, N);
  destination[N - 1] = '\0';
  in += N;
}

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

  const uint8_t* cursor = blob.get() + sizeof(kMagic);
  const uint16_t version = getU16(cursor);
  const bool initialized = *cursor++ != 0;
  const uint8_t count = *cursor++;
  const InstanceId nextInstanceId = getU32(cursor);
  const size_t encodedRecordSize =
      version == 1 ? kEncodedV1RecordSize : kEncodedRecordSize;
  const size_t expectedLength =
      kHeaderSize + static_cast<size_t>(count) * encodedRecordSize + kChecksumSize;
  if ((version != 1 && version != kSchemaVersion) ||
      count > CONFIG_MAX_DEVICE_INSTANCES ||
      length != expectedLength) {
    return ConfigLoadStatus::Corrupt;
  }

  const uint8_t* storedChecksumCursor = blob.get() + length - kChecksumSize;
  const uint32_t storedChecksum = getU32(storedChecksumCursor);
  if (storedChecksum != checksum(blob.get(), length - kChecksumSize)) {
    return ConfigLoadStatus::Corrupt;
  }

  std::unique_ptr<DeviceRecord[]> records(
      new (std::nothrow) DeviceRecord[CONFIG_MAX_DEVICE_INSTANCES]());
  if (!records) return ConfigLoadStatus::Corrupt;
  for (size_t i = 0; i < count; ++i) {
    DeviceRecord& record = records[i];
    record.instanceId = getU32(cursor);
    record.driverId = static_cast<DriverId>(getU16(cursor));
    const uint8_t flags = *cursor++;
    record.enabled = (flags & 0x01) != 0;
    record.paired = (flags & 0x02) != 0;
    record.bleAddressType = *cursor++;
    decodeText(record.displayName, cursor);
    decodeText(record.bleAddress, cursor);
    decodeText(record.bleName, cursor);
    if (version >= 2) {
      record.homeAssistantDomain = static_cast<HomeAssistantDomain>(*cursor++);
      decodeText(record.homeAssistantEntityId, cursor);
    }
  }

  if (!registry.restore(records.get(), count, nextInstanceId, initialized)) {
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
  uint8_t* cursor = blob.get();
  std::memcpy(cursor, kMagic, sizeof(kMagic));
  cursor += sizeof(kMagic);
  putU16(cursor, kSchemaVersion);
  *cursor++ = registry.initialized() ? 1 : 0;
  *cursor++ = static_cast<uint8_t>(registry.count());
  putU32(cursor, registry.nextInstanceId());

  for (size_t i = 0; i < registry.count(); ++i) {
    const DeviceRecord* record = registry.at(i);
    putU32(cursor, record->instanceId);
    putU16(cursor, static_cast<uint16_t>(record->driverId));
    *cursor++ = static_cast<uint8_t>((record->enabled ? 0x01 : 0x00) |
                                     (record->paired ? 0x02 : 0x00));
    *cursor++ = record->bleAddressType;
    std::memcpy(cursor, record->displayName, sizeof(record->displayName));
    cursor += sizeof(record->displayName);
    std::memcpy(cursor, record->bleAddress, sizeof(record->bleAddress));
    cursor += sizeof(record->bleAddress);
    std::memcpy(cursor, record->bleName, sizeof(record->bleName));
    cursor += sizeof(record->bleName);
    *cursor++ = static_cast<uint8_t>(record->homeAssistantDomain);
    std::memcpy(cursor, record->homeAssistantEntityId,
                sizeof(record->homeAssistantEntityId));
    cursor += sizeof(record->homeAssistantEntityId);
  }

  putU32(cursor, checksum(blob.get(), length - kChecksumSize));
  return static_cast<size_t>(cursor - blob.get()) == length &&
         backend_.write(blob.get(), length);
}

}  // namespace studio
