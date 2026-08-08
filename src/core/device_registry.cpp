#include "core/device_registry.h"

#include <cstring>
#include <new>

namespace studio {

namespace {

template <size_t N>
void copyText(char (&destination)[N], const char* source) {
  if (source == nullptr) {
    destination[0] = '\0';
    return;
  }
  std::strncpy(destination, source, N - 1);
  destination[N - 1] = '\0';
}

}  // namespace

DeviceRegistry::DeviceRegistry(const DeviceRegistry& other)
    : count_(other.count_),
      nextInstanceId_(other.nextInstanceId_),
      initialized_(other.initialized_),
      valid_(other.valid_) {
  if (count_ == 0) return;
  allocated_ = count_;
  records_ = new (std::nothrow) DeviceRecord[allocated_];
  if (records_ == nullptr) {
    allocated_ = 0;
    count_ = 0;
    valid_ = false;
    return;
  }
  std::memcpy(records_, other.records_, count_ * sizeof(DeviceRecord));
}

DeviceRegistry& DeviceRegistry::operator=(const DeviceRegistry& other) {
  if (this == &other) return *this;
  if (other.count_ > allocated_) {
    DeviceRecord* replacement =
        new (std::nothrow) DeviceRecord[other.count_];
    if (replacement == nullptr) {
      valid_ = false;
      return *this;
    }
    delete[] records_;
    records_ = replacement;
    allocated_ = other.count_;
  }
  if (other.count_ > 0) {
    std::memcpy(records_, other.records_, other.count_ * sizeof(DeviceRecord));
  }
  count_ = other.count_;
  nextInstanceId_ = other.nextInstanceId_;
  initialized_ = other.initialized_;
  valid_ = other.valid_;
  return *this;
}

DeviceRegistry::~DeviceRegistry() {
  delete[] records_;
}

bool DeviceRegistry::reserve(size_t required) {
  if (required <= allocated_) return true;
  constexpr size_t kAllocationBlock = 4;
  size_t requested =
      ((required + kAllocationBlock - 1) / kAllocationBlock) * kAllocationBlock;
  if (requested > capacity()) requested = capacity();
  DeviceRecord* replacement =
      new (std::nothrow) DeviceRecord[requested];
  if (replacement == nullptr) return false;
  if (count_ > 0) {
    std::memcpy(replacement, records_, count_ * sizeof(DeviceRecord));
  }
  delete[] records_;
  records_ = replacement;
  allocated_ = requested;
  return true;
}

const DeviceRecord* DeviceRegistry::at(size_t index) const {
  return index < count_ ? &records_[index] : nullptr;
}

DeviceRecord* DeviceRegistry::at(size_t index) {
  return index < count_ ? &records_[index] : nullptr;
}

const DeviceRecord* DeviceRegistry::find(InstanceId instanceId) const {
  for (size_t i = 0; i < count_; ++i) {
    if (records_[i].instanceId == instanceId) {
      return &records_[i];
    }
  }
  return nullptr;
}

DeviceRecord* DeviceRegistry::find(InstanceId instanceId) {
  for (size_t i = 0; i < count_; ++i) {
    if (records_[i].instanceId == instanceId) {
      return &records_[i];
    }
  }
  return nullptr;
}

size_t DeviceRegistry::countByDriver(DriverId driverId) const {
  size_t matches = 0;
  for (size_t i = 0; i < count_; ++i) {
    if (records_[i].driverId == driverId) {
      ++matches;
    }
  }
  return matches;
}

RegistryStatus DeviceRegistry::add(DriverId driverId, const char* displayName,
                                   uint8_t maxForDriver, InstanceId& outId) {
  outId = kInvalidInstanceId;
  if (driverId == DriverId::Unknown || displayName == nullptr || displayName[0] == '\0' ||
      maxForDriver == 0) {
    return RegistryStatus::Invalid;
  }
  if (count_ >= capacity()) {
    return RegistryStatus::Full;
  }
  if (countByDriver(driverId) >= maxForDriver) {
    return RegistryStatus::DuplicateDriver;
  }
  if (!reserve(count_ + 1)) {
    return RegistryStatus::Full;
  }

  DeviceRecord record;
  record.instanceId = nextInstanceId_++;
  record.driverId = driverId;
  record.enabled = true;
  copyText(record.displayName, displayName);
  records_[count_++] = record;
  initialized_ = true;
  outId = record.instanceId;
  return RegistryStatus::Ok;
}

RegistryStatus DeviceRegistry::commitPrepared(const DeviceRecord& record,
                                              uint8_t maxForDriver) {
  if (record.instanceId == kInvalidInstanceId ||
      record.driverId == DriverId::Unknown || record.displayName[0] == '\0' ||
      maxForDriver == 0) {
    return RegistryStatus::Invalid;
  }
  if (count_ >= capacity()) {
    return RegistryStatus::Full;
  }
  if (find(record.instanceId) != nullptr ||
      countByDriver(record.driverId) >= maxForDriver) {
    return RegistryStatus::DuplicateDriver;
  }
  if (!reserve(count_ + 1)) {
    return RegistryStatus::Full;
  }
  records_[count_++] = record;
  if (nextInstanceId_ <= record.instanceId) {
    nextInstanceId_ = record.instanceId + 1;
    if (nextInstanceId_ == kInvalidInstanceId) {
      --count_;
      records_[count_] = DeviceRecord{};
      return RegistryStatus::Invalid;
    }
  }
  initialized_ = true;
  return RegistryStatus::Ok;
}

RegistryStatus DeviceRegistry::remove(InstanceId instanceId) {
  for (size_t i = 0; i < count_; ++i) {
    if (records_[i].instanceId != instanceId) {
      continue;
    }
    for (size_t move = i + 1; move < count_; ++move) {
      records_[move - 1] = records_[move];
    }
    records_[--count_] = DeviceRecord{};
    initialized_ = true;
    return RegistryStatus::Ok;
  }
  return RegistryStatus::NotFound;
}

RegistryStatus DeviceRegistry::rename(InstanceId instanceId, const char* displayName) {
  DeviceRecord* record = find(instanceId);
  if (record == nullptr) {
    return RegistryStatus::NotFound;
  }
  if (displayName == nullptr || displayName[0] == '\0') {
    return RegistryStatus::Invalid;
  }
  copyText(record->displayName, displayName);
  return RegistryStatus::Ok;
}

RegistryStatus DeviceRegistry::setEnabled(InstanceId instanceId, bool enabled) {
  DeviceRecord* record = find(instanceId);
  if (record == nullptr) {
    return RegistryStatus::NotFound;
  }
  record->enabled = enabled;
  return RegistryStatus::Ok;
}

RegistryStatus DeviceRegistry::updatePairing(InstanceId instanceId, const char* address,
                                             uint8_t addressType,
                                             const char* advertisedName) {
  DeviceRecord* record = find(instanceId);
  if (record == nullptr) {
    return RegistryStatus::NotFound;
  }
  if (address == nullptr || address[0] == '\0') {
    return RegistryStatus::Invalid;
  }
  copyText(record->bleAddress, address);
  copyText(record->bleName, advertisedName);
  record->bleAddressType = addressType;
  record->paired = true;
  return RegistryStatus::Ok;
}

RegistryStatus DeviceRegistry::clearPairing(InstanceId instanceId) {
  DeviceRecord* record = find(instanceId);
  if (record == nullptr) {
    return RegistryStatus::NotFound;
  }
  record->paired = false;
  record->bleAddress[0] = '\0';
  record->bleName[0] = '\0';
  record->bleAddressType = 0;
  return RegistryStatus::Ok;
}

RegistryStatus DeviceRegistry::configureHomeAssistant(
    InstanceId instanceId, HomeAssistantDomain domain, const char* entityId) {
  DeviceRecord* record = find(instanceId);
  if (record == nullptr) {
    return RegistryStatus::NotFound;
  }
  if (record->driverId != DriverId::HomeAssistant ||
      domain == HomeAssistantDomain::None || entityId == nullptr ||
      entityId[0] == '\0') {
    return RegistryStatus::Invalid;
  }
  for (size_t i = 0; i < count_; ++i) {
    if (records_[i].instanceId != instanceId &&
        records_[i].driverId == DriverId::HomeAssistant &&
        std::strncmp(records_[i].homeAssistantEntityId, entityId,
                     sizeof(records_[i].homeAssistantEntityId)) == 0) {
      return RegistryStatus::DuplicateDriver;
    }
  }
  record->homeAssistantDomain = domain;
  copyText(record->homeAssistantEntityId, entityId);
  return RegistryStatus::Ok;
}

void DeviceRegistry::clear(bool initialized) {
  delete[] records_;
  records_ = nullptr;
  allocated_ = 0;
  count_ = 0;
  nextInstanceId_ = 1;
  initialized_ = initialized;
  valid_ = true;
}

bool DeviceRegistry::restore(const DeviceRecord* records, size_t count,
                             InstanceId nextInstanceId, bool initialized) {
  if ((count > 0 && records == nullptr) || count > capacity() ||
      nextInstanceId == kInvalidInstanceId) {
    return false;
  }
  clear(initialized);
  if (!reserve(count)) return false;
  for (size_t i = 0; i < count; ++i) {
    if (records[i].instanceId == kInvalidInstanceId || find(records[i].instanceId) != nullptr) {
      clear(false);
      return false;
    }
    records_[count_++] = records[i];
  }
  nextInstanceId_ = nextInstanceId;
  return true;
}

}  // namespace studio
