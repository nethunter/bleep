#pragma once

#include <cstddef>
#include <cstdint>

#include "core/config_store.h"
#include "core/device_types.h"

namespace aputure_light {

constexpr size_t kMaxMeshNodes = CONFIG_MAX_DEVICE_INSTANCES;
constexpr uint32_t kSequenceBlockSize = 256;
constexpr uint32_t kSequenceMaximum = 0x1000000;
constexpr uint8_t kCurrentConfigurationVersion = 3;

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
  // Vendor access messages are addressed to a model subscription rather than
  // the node unicast address. Zero means this record predates per-node groups.
  uint16_t controlGroupAddress = 0;
  uint16_t vendorCompanyId = 0;
  uint16_t vendorModelId = 0;
  // Zero identifies records whose configuration writes were never confirmed
  // by decoded mesh status responses.
  uint8_t configurationVersion = 0;
  // Zhiyun's cleartext FEE9 protocol routes members with an ordinal selector.
  // This is independent of product model and mesh unicast address.
  uint8_t routingSelector = 0xff;
};

struct MeshStoreData {
  MeshNetworkRecord network;
  MeshNodeRecord nodes[kMaxMeshNodes] = {};
  uint8_t nodeCount = 0;
};

class MeshStore {
 public:
  static constexpr uint16_t kSchemaVersion = 3;
  static constexpr size_t kMaxBlobSize = 2048;

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
uint16_t defaultControlGroupAddress(const MeshStoreData& data,
                                    const MeshNodeRecord& node);
uint16_t memberControlGroupAddress(const MeshStoreData& data,
                                   studio::InstanceId instanceId);
bool assignVendorModel(MeshStoreData& data, studio::InstanceId instanceId,
                       uint16_t companyId, uint16_t modelId);
bool isKnownMeshProxyAddress(const MeshStoreData& data, const char* address);
uint8_t nextZhiyunRoutingSelector(const MeshStoreData& data);

}  // namespace aputure_light
