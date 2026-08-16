#include "core/mesh/mesh_store.h"

#include <cstring>

#include "core/blob_codec.h"

namespace studio::mesh {
namespace {

constexpr uint8_t kMagic[] = {'A', 'M', 'S', 'H'};
constexpr size_t kHeaderSize = 8;
constexpr size_t kNetworkSize = 1 + 16 + 16 + 4 + 2 + 2 + 2 + 4;
constexpr size_t kNodeSize =
    4 + 2 + 2 + 1 + 1 + 16 + 16 + kBleAddressCapacity + 1 + 2 + 2 + 2 +
    1 + 1;
constexpr size_t kChecksumSize = 4;

static_assert(kHeaderSize + kNetworkSize + kMaxMeshNodes * kNodeSize +
                      kChecksumSize <=
                  Store::kMaxBlobSize,
              "mesh blob is too small for configured node capacity");

bool decodeNetwork(BlobReader& reader, NetworkRecord& network) {
  uint8_t initialized = 0;
  if (!reader.u8(initialized) || initialized > 1 ||
      !reader.bytes(network.networkKey, sizeof(network.networkKey)) ||
      !reader.bytes(network.applicationKey, sizeof(network.applicationKey)) ||
      !reader.u32(network.ivIndex) ||
      !reader.u16(network.provisionerAddress) ||
      !reader.u16(network.groupAddress) ||
      !reader.u16(network.nextUnicastAddress) ||
      !reader.u32(network.sequenceHighWater)) {
    return false;
  }
  network.initialized = initialized != 0;
  return true;
}

bool decodeNode(BlobReader& reader, NodeRecord& node) {
  uint16_t model = 0;
  uint8_t configured = 0;
  if (!reader.u32(node.instanceId) || !reader.u16(model) ||
      !reader.u16(node.unicastAddress) || !reader.u8(node.elementCount) ||
      !reader.u8(configured) || configured > 1 ||
      !reader.bytes(node.deviceKey, sizeof(node.deviceKey)) ||
      !reader.bytes(node.deviceUuid, sizeof(node.deviceUuid)) ||
      !reader.text(node.bleAddress) || !reader.u8(node.bleAddressType) ||
      !reader.u16(node.controlGroupAddress) ||
      !reader.u16(node.vendorCompanyId) ||
      !reader.u16(node.vendorModelId) ||
      !reader.u8(node.routingSelector) ||
      !reader.u8(node.configurationVersion)) {
    return false;
  }
  node.model = static_cast<DriverId>(model);
  node.configured = configured != 0;
  return true;
}

void encodeNetwork(BlobWriter& writer, const NetworkRecord& network) {
  writer.u8(network.initialized ? 1 : 0);
  writer.bytes(network.networkKey, sizeof(network.networkKey));
  writer.bytes(network.applicationKey, sizeof(network.applicationKey));
  writer.u32(network.ivIndex);
  writer.u16(network.provisionerAddress);
  writer.u16(network.groupAddress);
  writer.u16(network.nextUnicastAddress);
  writer.u32(network.sequenceHighWater);
}

void encodeNode(BlobWriter& writer, const NodeRecord& node) {
  writer.u32(node.instanceId);
  writer.u16(static_cast<uint16_t>(node.model));
  writer.u16(node.unicastAddress);
  writer.u8(node.elementCount);
  writer.u8(node.configured ? 1 : 0);
  writer.bytes(node.deviceKey, sizeof(node.deviceKey));
  writer.bytes(node.deviceUuid, sizeof(node.deviceUuid));
  writer.bytes(node.bleAddress, sizeof(node.bleAddress));
  writer.u8(node.bleAddressType);
  writer.u16(node.controlGroupAddress);
  writer.u16(node.vendorCompanyId);
  writer.u16(node.vendorModelId);
  writer.u8(node.routingSelector);
  writer.u8(node.configurationVersion);
}

}  // namespace

ConfigLoadStatus Store::load(StoreData& data) {
  data = StoreData{};
  uint8_t blob[kMaxBlobSize] = {};
  const size_t length = backend_.read(blob, sizeof(blob));
  if (length == 0) return ConfigLoadStatus::Missing;
  if (length < kHeaderSize + kNetworkSize + kChecksumSize ||
      std::memcmp(blob, kMagic, sizeof(kMagic)) != 0) {
    return ConfigLoadStatus::Corrupt;
  }

  BlobReader reader(blob + sizeof(kMagic), length - sizeof(kMagic));
  uint16_t version = 0;
  uint8_t count = 0;
  uint8_t reserved = 0;
  if (!reader.u16(version) || !reader.u8(count) || !reader.u8(reserved) ||
      version != kSchemaVersion || count > kMaxMeshNodes || reserved != 0 ||
      length != kHeaderSize + kNetworkSize +
                    static_cast<size_t>(count) * kNodeSize + kChecksumSize) {
    return ConfigLoadStatus::Corrupt;
  }

  BlobReader checksumReader(blob + length - kChecksumSize, kChecksumSize);
  uint32_t storedChecksum = 0;
  if (!checksumReader.u32(storedChecksum) ||
      storedChecksum != fnv1a(blob, length - kChecksumSize) ||
      !decodeNetwork(reader, data.network)) {
    data = StoreData{};
    return ConfigLoadStatus::Corrupt;
  }
  data.nodeCount = count;
  for (uint8_t index = 0; index < count; ++index) {
    if (!decodeNode(reader, data.nodes[index])) {
      data = StoreData{};
      return ConfigLoadStatus::Corrupt;
    }
  }
  if (reader.position() != length - sizeof(kMagic) - kChecksumSize) {
    data = StoreData{};
    return ConfigLoadStatus::Corrupt;
  }
  return ConfigLoadStatus::Loaded;
}

bool Store::save(const StoreData& data) {
  if (data.nodeCount > kMaxMeshNodes) return false;
  const size_t length = kHeaderSize + kNetworkSize +
                        static_cast<size_t>(data.nodeCount) * kNodeSize +
                        kChecksumSize;
  if (length > kMaxBlobSize) return false;
  uint8_t blob[kMaxBlobSize] = {};
  BlobWriter writer(blob, length);
  writer.bytes(kMagic, sizeof(kMagic));
  writer.u16(kSchemaVersion);
  writer.u8(data.nodeCount);
  writer.u8(0);
  encodeNetwork(writer, data.network);
  for (uint8_t index = 0; index < data.nodeCount; ++index) {
    encodeNode(writer, data.nodes[index]);
  }
  writer.u32(fnv1a(blob, length - kChecksumSize));
  return writer.valid() && writer.size() == length &&
         backend_.write(blob, length);
}

bool SequenceAllocator::begin(Store& store, StoreData& data) {
  store_ = &store;
  data_ = &data;
  cursor_ = limit_ = data.network.sequenceHighWater;
  return true;
}

bool SequenceAllocator::reserve() {
  if (store_ == nullptr || data_ == nullptr || cursor_ >= kSequenceMaximum) {
    return false;
  }
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

NodeRecord* findNode(StoreData& data, InstanceId instanceId) {
  for (uint8_t index = 0; index < data.nodeCount; ++index) {
    if (data.nodes[index].instanceId == instanceId) return &data.nodes[index];
  }
  return nullptr;
}

const NodeRecord* findNode(const StoreData& data, InstanceId instanceId) {
  for (uint8_t index = 0; index < data.nodeCount; ++index) {
    if (data.nodes[index].instanceId == instanceId) return &data.nodes[index];
  }
  return nullptr;
}

bool upsertNode(StoreData& data, const NodeRecord& node) {
  NodeRecord* existing = findNode(data, node.instanceId);
  if (existing != nullptr) {
    *existing = node;
    return true;
  }
  if (data.nodeCount >= kMaxMeshNodes) return false;
  data.nodes[data.nodeCount++] = node;
  return true;
}

bool removeNode(StoreData& data, InstanceId instanceId) {
  for (uint8_t index = 0; index < data.nodeCount; ++index) {
    if (data.nodes[index].instanceId != instanceId) continue;
    for (uint8_t next = index + 1; next < data.nodeCount; ++next) {
      data.nodes[next - 1] = data.nodes[next];
    }
    data.nodes[--data.nodeCount] = {};
    return true;
  }
  return false;
}

uint16_t defaultControlGroupAddress(const StoreData& data,
                                    const NodeRecord& node) {
  if (node.controlGroupAddress != 0) return node.controlGroupAddress;
  if (node.unicastAddress <= data.network.provisionerAddress) return 0;
  const uint32_t address = static_cast<uint32_t>(data.network.groupAddress) +
                           node.unicastAddress -
                           data.network.provisionerAddress;
  return address <= 0xfeff ? static_cast<uint16_t>(address) : 0;
}

uint16_t memberControlGroupAddress(const StoreData& data,
                                   InstanceId instanceId) {
  const NodeRecord* node = findNode(data, instanceId);
  return node != nullptr ? defaultControlGroupAddress(data, *node) : 0;
}

bool assignVendorModel(StoreData& data, InstanceId instanceId,
                       uint16_t companyId, uint16_t modelId) {
  NodeRecord* node = findNode(data, instanceId);
  if (node == nullptr || node->configured || companyId == 0) return false;
  node->vendorCompanyId = companyId;
  node->vendorModelId = modelId;
  node->controlGroupAddress = defaultControlGroupAddress(data, *node);
  return true;
}

bool isKnownMeshProxyAddress(const StoreData& data, const char* address) {
  if (address == nullptr || address[0] == '\0') return false;
  for (uint8_t index = 0; index < data.nodeCount; ++index) {
    if (data.nodes[index].bleAddress[0] != '\0' &&
        std::strcmp(data.nodes[index].bleAddress, address) == 0) {
      return true;
    }
  }
  return false;
}

uint8_t nextZhiyunRoutingSelector(const StoreData& data) {
  bool used[0xff] = {};
  for (uint8_t index = 0; index < data.nodeCount; ++index) {
    const NodeRecord& node = data.nodes[index];
    if (node.model == DriverId::ZhiyunLight && node.routingSelector != 0xff) {
      used[node.routingSelector] = true;
    }
  }
  for (uint16_t selector = 0; selector < 0xff; ++selector) {
    if (!used[selector]) return static_cast<uint8_t>(selector);
  }
  return 0xff;
}

}  // namespace studio::mesh
