#include "core/config_store.h"

#include <cstring>

namespace studio {

namespace {

constexpr uint8_t kMagic[] = {'S', 'T', 'D', 'V'};
constexpr size_t kHeaderSize = 12;
constexpr size_t kEncodedRecordSize =
    4 + 2 + 1 + 1 + kDeviceNameCapacity + kBleAddressCapacity + kBleNameCapacity;
constexpr size_t kChecksumSize = 4;

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
  uint8_t blob[kMaxBlobSize];
  const size_t length = backend_.read(blob, sizeof(blob));
  if (length == 0) {
    return ConfigLoadStatus::Missing;
  }
  if (length < kHeaderSize + kChecksumSize ||
      std::memcmp(blob, kMagic, sizeof(kMagic)) != 0) {
    return ConfigLoadStatus::Corrupt;
  }

  const uint8_t* cursor = blob + sizeof(kMagic);
  const uint16_t version = getU16(cursor);
  const bool initialized = *cursor++ != 0;
  const uint8_t count = *cursor++;
  const InstanceId nextInstanceId = getU32(cursor);
  const size_t expectedLength =
      kHeaderSize + static_cast<size_t>(count) * kEncodedRecordSize + kChecksumSize;
  if (version != kSchemaVersion || count > CONFIG_MAX_DEVICE_INSTANCES ||
      length != expectedLength) {
    return ConfigLoadStatus::Corrupt;
  }

  const uint8_t* storedChecksumCursor = blob + length - kChecksumSize;
  const uint32_t storedChecksum = getU32(storedChecksumCursor);
  if (storedChecksum != checksum(blob, length - kChecksumSize)) {
    return ConfigLoadStatus::Corrupt;
  }

  DeviceRecord records[CONFIG_MAX_DEVICE_INSTANCES] = {};
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
  }

  if (!registry.restore(records, count, nextInstanceId, initialized)) {
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

  uint8_t blob[kMaxBlobSize] = {};
  uint8_t* cursor = blob;
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
  }

  putU32(cursor, checksum(blob, length - kChecksumSize));
  return static_cast<size_t>(cursor - blob) == length && backend_.write(blob, length);
}

}  // namespace studio

