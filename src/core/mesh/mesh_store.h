#pragma once

#include <cstddef>
#include <cstdint>

#include "core/config_store.h"
#include "core/device_types.h"

namespace studio::mesh {

constexpr size_t kMaxMeshNodes = CONFIG_MAX_DEVICE_INSTANCES;
constexpr uint32_t kSequenceBlockSize = 256;
constexpr uint32_t kSequenceMaximum = 0x1000000;
constexpr uint8_t kCurrentConfigurationVersion = 3;

struct NetworkRecord {
  bool initialized = false;
  uint8_t networkKey[16] = {};
  uint8_t applicationKey[16] = {};
  uint32_t ivIndex = 0;
  uint16_t provisionerAddress = 1;
  uint16_t groupAddress = 0xc000;
  uint16_t nextUnicastAddress = 2;
  uint32_t sequenceHighWater = 0;
};

struct NodeRecord {
  InstanceId instanceId = kInvalidInstanceId;
  DriverId model = DriverId::Unknown;
  uint16_t unicastAddress = 0;
  uint8_t elementCount = 1;
  bool configured = false;
  uint8_t deviceKey[16] = {};
  uint8_t deviceUuid[16] = {};
  char bleAddress[kBleAddressCapacity] = "";
  uint8_t bleAddressType = 0;
  uint16_t controlGroupAddress = 0;
  uint16_t vendorCompanyId = 0;
  uint16_t vendorModelId = 0;
  uint8_t configurationVersion = 0;
  uint8_t routingSelector = 0xff;
};

struct StoreData {
  NetworkRecord network;
  NodeRecord nodes[kMaxMeshNodes] = {};
  uint8_t nodeCount = 0;
};

class Store {
 public:
  static constexpr uint16_t kSchemaVersion = 3;
  static constexpr size_t kMaxBlobSize = 2048;

  explicit Store(IConfigBackend& backend) : backend_(backend) {}
  ConfigLoadStatus load(StoreData& data);
  bool save(const StoreData& data);

 private:
  IConfigBackend& backend_;
};

class SequenceAllocator {
 public:
  bool begin(Store& store, StoreData& data);
  bool next(uint32_t& sequence);
  uint32_t remaining() const { return limit_ > cursor_ ? limit_ - cursor_ : 0; }

 private:
  bool reserve();
  Store* store_ = nullptr;
  StoreData* data_ = nullptr;
  uint32_t cursor_ = 0;
  uint32_t limit_ = 0;
};

NodeRecord* findNode(StoreData& data, InstanceId instanceId);
const NodeRecord* findNode(const StoreData& data, InstanceId instanceId);
bool upsertNode(StoreData& data, const NodeRecord& node);
bool removeNode(StoreData& data, InstanceId instanceId);
uint16_t defaultControlGroupAddress(const StoreData& data,
                                    const NodeRecord& node);
uint16_t memberControlGroupAddress(const StoreData& data,
                                   InstanceId instanceId);
bool assignVendorModel(StoreData& data, InstanceId instanceId,
                       uint16_t companyId, uint16_t modelId);
bool isKnownMeshProxyAddress(const StoreData& data, const char* address);
uint8_t nextZhiyunRoutingSelector(const StoreData& data);

}  // namespace studio::mesh
