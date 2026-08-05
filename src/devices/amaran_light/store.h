#pragma once

#include <cstddef>
#include <cstdint>

#include "core/config_store.h"
#include "core/device_types.h"

namespace amaran_light {

constexpr size_t kMaxMeshNodes = CONFIG_MAX_DEVICE_INSTANCES;
constexpr uint32_t kSequenceBlockSize = 256;
constexpr uint32_t kSequenceMaximum = 0x1000000;

struct MeshNetworkRecord {
  bool initialized = false;
  uint8_t networkKey[16] = {};
  uint8_t applicationKey[16] = {};
  uint32_t ivIndex = 0;
  uint16_t provisionerAddress = 1;
  uint16_t groupAddress = 0xc000;
  uint16_t nextUnicastAddress = 2;
  uint32_t sequenceHighWater = 0;
};

struct MeshNodeRecord {
  studio::InstanceId instanceId = studio::kInvalidInstanceId;
  studio::DriverId model = studio::DriverId::Unknown;
  uint16_t unicastAddress = 0;
  uint8_t elementCount = 1;
  bool configured = false;
  uint8_t deviceKey[16] = {};
  uint8_t deviceUuid[16] = {};
  char bleAddress[studio::kBleAddressCapacity] = "";
  uint8_t bleAddressType = 0;
};

struct MeshStoreData {
  MeshNetworkRecord network;
  MeshNodeRecord nodes[kMaxMeshNodes] = {};
  uint8_t nodeCount = 0;
};

class MeshStore {
 public:
  static constexpr uint16_t kSchemaVersion = 1;
  static constexpr size_t kMaxBlobSize = 1280;

  explicit MeshStore(studio::IConfigBackend& backend) : backend_(backend) {}
  studio::ConfigLoadStatus load(MeshStoreData& data);
  bool save(const MeshStoreData& data);

 private:
  studio::IConfigBackend& backend_;
};

class SequenceAllocator {
 public:
  bool begin(MeshStore& store, MeshStoreData& data);
  bool next(uint32_t& sequence);
  uint32_t remaining() const { return limit_ > cursor_ ? limit_ - cursor_ : 0; }

 private:
  bool reserve();
  MeshStore* store_ = nullptr;
  MeshStoreData* data_ = nullptr;
  uint32_t cursor_ = 0;
  uint32_t limit_ = 0;
};

MeshNodeRecord* findNode(MeshStoreData& data, studio::InstanceId instanceId);
const MeshNodeRecord* findNode(const MeshStoreData& data,
                               studio::InstanceId instanceId);
bool upsertNode(MeshStoreData& data, const MeshNodeRecord& node);
bool removeNode(MeshStoreData& data, studio::InstanceId instanceId);

}  // namespace amaran_light
