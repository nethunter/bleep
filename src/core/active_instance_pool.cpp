#include "core/active_instance_pool.h"

namespace studio {

ActiveInstancePool::Slot* ActiveInstancePool::find(InstanceId instanceId) {
  for (Slot& slot : slots_) {
    if (slot.instanceId == instanceId) return &slot;
  }
  return nullptr;
}

const ActiveInstancePool::Slot* ActiveInstancePool::find(
    InstanceId instanceId) const {
  for (const Slot& slot : slots_) {
    if (slot.instanceId == instanceId) return &slot;
  }
  return nullptr;
}

bool ActiveInstancePool::add(InstanceId instanceId, ConnectionOwner owner) {
  Slot* existing = find(instanceId);
  if (existing != nullptr) {
    existing->owners |= ownerBit(owner);
    touch(*existing);
    return true;
  }
  for (Slot& slot : slots_) {
    if (slot.instanceId != kInvalidInstanceId) continue;
    slot.instanceId = instanceId;
    slot.owners = ownerBit(owner);
    slot.retained = false;
    touch(slot);
    ++count_;
    return true;
  }
  return false;
}

void ActiveInstancePool::remove(InstanceId instanceId) {
  Slot* slot = find(instanceId);
  if (slot == nullptr) return;
  *slot = {};
  if (count_ > 0) --count_;
}

void ActiveInstancePool::releaseOwner(Slot& slot, ConnectionOwner owner) {
  slot.owners &= ~ownerBit(owner);
  touch(slot);
}

void ActiveInstancePool::touch(Slot& slot) {
  ++useCounter_;
  if (useCounter_ == 0) useCounter_ = 1;
  slot.lastUsed = useCounter_;
}

InstanceId ActiveInstancePool::foregroundInstance() const {
  for (const Slot& slot : slots_) {
    if ((slot.owners & ownerBit(ConnectionOwner::Foreground)) != 0) {
      return slot.instanceId;
    }
  }
  return kInvalidInstanceId;
}

bool ActiveInstancePool::contains(InstanceId instanceId) const {
  return instanceId != kInvalidInstanceId && find(instanceId) != nullptr;
}

bool ActiveInstancePool::ownedBy(InstanceId instanceId,
                                 ConnectionOwner owner) const {
  const Slot* slot = find(instanceId);
  return slot != nullptr && (slot->owners & ownerBit(owner)) != 0;
}

bool ActiveInstancePool::retained(InstanceId instanceId) const {
  const Slot* slot = find(instanceId);
  return slot != nullptr && slot->retained;
}

}  // namespace studio
