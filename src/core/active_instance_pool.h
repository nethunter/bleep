#pragma once

#include "core/device_types.h"

namespace studio {

class ActiveInstancePool {
 public:
  static constexpr size_t kCapacity = CONFIG_MAX_ACTIVE_INSTANCES;

  struct Slot {
    InstanceId instanceId = kInvalidInstanceId;
    uint8_t owners = 0;
    bool retained = false;
    uint32_t lastUsed = 0;
    uint32_t pendingRequestId = 0;
  };

  Slot* find(InstanceId instanceId);
  const Slot* find(InstanceId instanceId) const;
  Slot* at(size_t index) { return index < kCapacity ? &slots_[index] : nullptr; }
  const Slot* at(size_t index) const {
    return index < kCapacity ? &slots_[index] : nullptr;
  }
  Slot* slots() { return slots_; }
  const Slot* slots() const { return slots_; }

  bool add(InstanceId instanceId, ConnectionOwner owner);
  void remove(InstanceId instanceId);
  void releaseOwner(Slot& slot, ConnectionOwner owner);
  void touch(Slot& slot);

  InstanceId foregroundInstance() const;
  bool contains(InstanceId instanceId) const;
  bool ownedBy(InstanceId instanceId, ConnectionOwner owner) const;
  bool retained(InstanceId instanceId) const;
  size_t count() const { return count_; }

  static uint8_t ownerBit(ConnectionOwner owner) {
    return static_cast<uint8_t>(owner);
  }

 private:
  Slot slots_[kCapacity] = {};
  size_t count_ = 0;
  uint32_t useCounter_ = 0;
};

}  // namespace studio
