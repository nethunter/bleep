#include "devices/amaran_light/store.h"

#include <cstring>

namespace amaran_light {
namespace {

constexpr uint8_t kMagic[] = {'A','M','S','H'};
constexpr size_t kHeaderSize = 8;
constexpr size_t kNetworkSize = 1 + 16 + 16 + 4 + 2 + 2 + 2 + 4;
constexpr size_t kNodeV1Size = 4 + 2 + 2 + 1 + 1 + 16 + 16 +
                               studio::kBleAddressCapacity + 1;
constexpr size_t kNodeV2Size = kNodeV1Size + 2 + 2 + 2 + 1;
constexpr size_t kNodeV3Size = kNodeV2Size + 1;
constexpr size_t kChecksumSize = 4;

static_assert(kHeaderSize + kNetworkSize + kMaxMeshNodes * kNodeV3Size +
                      kChecksumSize <=
                  MeshStore::kMaxBlobSize,
              "MeshStore blob is too small for configured node capacity");

uint32_t checksum(const uint8_t* data, size_t length) {
  uint32_t value = 2166136261u;
  for (size_t i = 0; i < length; ++i) {
    value ^= data[i];
    value *= 16777619u;
  }
  return value;
}

void put16(uint8_t*& out, uint16_t value) {
  *out++ = static_cast<uint8_t>(value);
  *out++ = static_cast<uint8_t>(value >> 8);
}
void put32(uint8_t*& out, uint32_t value) {
  for (size_t i = 0; i < 4; ++i) *out++ = static_cast<uint8_t>(value >> (i * 8));
}
uint16_t get16(const uint8_t*& in) {
  const uint16_t value = static_cast<uint16_t>(in[0]) |
                         static_cast<uint16_t>(in[1]) << 8;
  in += 2;
  return value;
}
uint32_t get32(const uint8_t*& in) {
  uint32_t value = 0;
  for (size_t i = 0; i < 4; ++i) value |= static_cast<uint32_t>(*in++) << (i * 8);
  return value;
}

}  // namespace

studio::ConfigLoadStatus MeshStore::load(MeshStoreData& data) {
  data = MeshStoreData{};
  uint8_t blob[kMaxBlobSize];
  const size_t length = backend_.read(blob, sizeof(blob));
  if (length == 0) return studio::ConfigLoadStatus::Missing;
  if (length < kHeaderSize + kNetworkSize + kChecksumSize ||
      std::memcmp(blob, kMagic, sizeof(kMagic)) != 0) {
    return studio::ConfigLoadStatus::Corrupt;
  }
  const uint8_t* cursor = blob + 4;
  const uint16_t version = get16(cursor);
  const uint8_t count = *cursor++;
  ++cursor;
  const size_t nodeSize = version == 1 ? kNodeV1Size
                          : version == 2 ? kNodeV2Size
                                         : kNodeV3Size;
  const size_t expected = kHeaderSize + kNetworkSize +
                          static_cast<size_t>(count) * nodeSize + kChecksumSize;
  if ((version != 1 && version != 2 && version != kSchemaVersion) ||
      count > kMaxMeshNodes || length != expected) {
    return studio::ConfigLoadStatus::Corrupt;
  }
  const uint8_t* checksumCursor = blob + length - 4;
  if (get32(checksumCursor) != checksum(blob, length - 4)) {
    return studio::ConfigLoadStatus::Corrupt;
  }
  data.network.initialized = *cursor++ != 0;
  std::memcpy(data.network.networkKey, cursor, 16); cursor += 16;
  std::memcpy(data.network.applicationKey, cursor, 16); cursor += 16;
  data.network.ivIndex = get32(cursor);
  data.network.provisionerAddress = get16(cursor);
  data.network.groupAddress = get16(cursor);
  data.network.nextUnicastAddress = get16(cursor);
  data.network.sequenceHighWater = get32(cursor);
  data.nodeCount = count;
  for (uint8_t i = 0; i < count; ++i) {
    MeshNodeRecord& node = data.nodes[i];
    node.instanceId = get32(cursor);
    node.model = static_cast<studio::DriverId>(get16(cursor));
    node.unicastAddress = get16(cursor);
    node.elementCount = *cursor++;
    node.configured = *cursor++ != 0;
    std::memcpy(node.deviceKey, cursor, 16); cursor += 16;
    std::memcpy(node.deviceUuid, cursor, 16); cursor += 16;
    std::memcpy(node.bleAddress, cursor, sizeof(node.bleAddress));
    node.bleAddress[sizeof(node.bleAddress) - 1] = '\0';
    cursor += sizeof(node.bleAddress);
    node.bleAddressType = *cursor++;
    if (version >= 2) {
      node.controlGroupAddress = get16(cursor);
      node.vendorCompanyId = get16(cursor);
      node.vendorModelId = get16(cursor);
      node.routingSelector = *cursor++;
    }
    if (version >= 3) node.configurationVersion = *cursor++;
  }
  if (version == 1) {
    uint8_t zhiyunSelector = 0;
    for (uint8_t i = 0; i < count; ++i) {
      MeshNodeRecord& node = data.nodes[i];
      if (node.model == studio::DriverId::ZhiyunLight) {
        node.routingSelector = zhiyunSelector++;
      }
    }
  }
  return studio::ConfigLoadStatus::Loaded;
}

bool MeshStore::save(const MeshStoreData& data) {
  if (data.nodeCount > kMaxMeshNodes) return false;
  const size_t length = kHeaderSize + kNetworkSize +
                        static_cast<size_t>(data.nodeCount) * kNodeV3Size + 4;
  if (length > kMaxBlobSize) return false;
  uint8_t blob[kMaxBlobSize] = {};
  uint8_t* cursor = blob;
  std::memcpy(cursor, kMagic, 4); cursor += 4;
  put16(cursor, kSchemaVersion);
  *cursor++ = data.nodeCount;
  *cursor++ = 0;
  *cursor++ = data.network.initialized ? 1 : 0;
  std::memcpy(cursor, data.network.networkKey, 16); cursor += 16;
  std::memcpy(cursor, data.network.applicationKey, 16); cursor += 16;
  put32(cursor, data.network.ivIndex);
  put16(cursor, data.network.provisionerAddress);
  put16(cursor, data.network.groupAddress);
  put16(cursor, data.network.nextUnicastAddress);
  put32(cursor, data.network.sequenceHighWater);
  for (uint8_t i = 0; i < data.nodeCount; ++i) {
    const MeshNodeRecord& node = data.nodes[i];
    put32(cursor, node.instanceId);
    put16(cursor, static_cast<uint16_t>(node.model));
    put16(cursor, node.unicastAddress);
    *cursor++ = node.elementCount;
    *cursor++ = node.configured ? 1 : 0;
    std::memcpy(cursor, node.deviceKey, 16); cursor += 16;
    std::memcpy(cursor, node.deviceUuid, 16); cursor += 16;
    std::memcpy(cursor, node.bleAddress, sizeof(node.bleAddress));
    cursor += sizeof(node.bleAddress);
    *cursor++ = node.bleAddressType;
    put16(cursor, node.controlGroupAddress);
    put16(cursor, node.vendorCompanyId);
    put16(cursor, node.vendorModelId);
    *cursor++ = node.routingSelector;
    *cursor++ = node.configurationVersion;
  }
  put32(cursor, checksum(blob, length - 4));
  return static_cast<size_t>(cursor - blob) == length && backend_.write(blob, length);
}

bool SequenceAllocator::begin(MeshStore& store, MeshStoreData& data) {
  store_ = &store;
  data_ = &data;
  cursor_ = limit_ = data.network.sequenceHighWater;
  return true;
}

bool SequenceAllocator::reserve() {
  if (store_ == nullptr || data_ == nullptr || cursor_ >= kSequenceMaximum) return false;
  const uint32_t nextLimit = cursor_ > kSequenceMaximum - kSequenceBlockSize
                                 ? kSequenceMaximum
                                 : cursor_ + kSequenceBlockSize;
  data_->network.sequenceHighWater = nextLimit;
  if (!store_->save(*data_)) return false;
  limit_ = nextLimit;
  return true;
}

bool SequenceAllocator::next(uint32_t& sequence) {
  if (cursor_ >= limit_ && !reserve()) return false;
  sequence = cursor_++;
  return true;
}

MeshNodeRecord* findNode(MeshStoreData& data, studio::InstanceId instanceId) {
  for (uint8_t i = 0; i < data.nodeCount; ++i) {
    if (data.nodes[i].instanceId == instanceId) return &data.nodes[i];
  }
  return nullptr;
}
const MeshNodeRecord* findNode(const MeshStoreData& data,
                               studio::InstanceId instanceId) {
  for (uint8_t i = 0; i < data.nodeCount; ++i) {
    if (data.nodes[i].instanceId == instanceId) return &data.nodes[i];
  }
  return nullptr;
}
bool upsertNode(MeshStoreData& data, const MeshNodeRecord& node) {
  MeshNodeRecord* existing = findNode(data, node.instanceId);
  if (existing != nullptr) { *existing = node; return true; }
  if (data.nodeCount >= kMaxMeshNodes) return false;
  data.nodes[data.nodeCount++] = node;
  return true;
}
bool removeNode(MeshStoreData& data, studio::InstanceId instanceId) {
  for (uint8_t i = 0; i < data.nodeCount; ++i) {
    if (data.nodes[i].instanceId != instanceId) continue;
    for (uint8_t j = i + 1; j < data.nodeCount; ++j) data.nodes[j - 1] = data.nodes[j];
    data.nodes[--data.nodeCount] = MeshNodeRecord{};
    return true;
  }
  return false;
}

uint16_t defaultControlGroupAddress(const MeshStoreData& data,
                                    const MeshNodeRecord& node) {
  if (node.controlGroupAddress != 0) return node.controlGroupAddress;
  if (node.unicastAddress <= data.network.provisionerAddress) return 0;
  const uint32_t address = static_cast<uint32_t>(data.network.groupAddress) +
                           node.unicastAddress -
                           data.network.provisionerAddress;
  return address <= 0xfeff ? static_cast<uint16_t>(address) : 0;
}

uint8_t nextZhiyunRoutingSelector(const MeshStoreData& data) {
  bool used[0xff] = {};
  for (uint8_t i = 0; i < data.nodeCount; ++i) {
    const MeshNodeRecord& node = data.nodes[i];
    if (node.model == studio::DriverId::ZhiyunLight &&
        node.routingSelector != 0xff) {
      used[node.routingSelector] = true;
    }
  }
  for (uint16_t selector = 0; selector < 0xff; ++selector) {
    if (!used[selector]) return static_cast<uint8_t>(selector);
  }
  return 0xff;
}

}  // namespace amaran_light
