#include "core/scene_registry.h"

#include <cstring>
#include <limits>
#include <new>
#include <utility>

namespace studio {
namespace {

void copyName(char (&destination)[kDeviceNameCapacity], const char* name) {
  if (name == nullptr) {
    destination[0] = '\0';
    return;
  }
  std::strncpy(destination, name, sizeof(destination) - 1);
  destination[sizeof(destination) - 1] = '\0';
}

bool validSteps(const SceneStep* steps, uint8_t count) {
  if (count > CONFIG_MAX_SCENE_STEPS) {
    return false;
  }
  for (uint8_t i = 0; i < count; ++i) {
    if (steps[i].type == SceneStepType::Wait) {
      continue;
    }
    if (steps[i].type != SceneStepType::Action ||
        steps[i].targetId == kInvalidInstanceId) {
      return false;
    }
  }
  return true;
}

}  // namespace

SceneRegistry::SceneRegistry(const SceneRegistry& other)
    : count_(other.count_),
      nextSceneId_(other.nextSceneId_),
      initialized_(other.initialized_),
      healthy_(other.healthy_) {
  if (!healthy_ || count_ == 0) return;
  count_ = 0;
  if (!reserve(other.count_)) {
    healthy_ = false;
    return;
  }
  for (size_t i = 0; i < other.count_; ++i) records_[i] = other.records_[i];
  count_ = other.count_;
}

SceneRegistry::SceneRegistry(SceneRegistry&& other) noexcept { swap(other); }

SceneRegistry& SceneRegistry::operator=(const SceneRegistry& other) {
  if (this == &other) return *this;
  SceneRegistry copy(other);
  if (copy.healthy()) swap(copy);
  return *this;
}

SceneRegistry& SceneRegistry::operator=(SceneRegistry&& other) noexcept {
  if (this == &other) return *this;
  SceneRegistry empty;
  swap(empty);
  swap(other);
  return *this;
}

SceneRegistry::~SceneRegistry() { delete[] records_; }

void SceneRegistry::swap(SceneRegistry& other) noexcept {
  std::swap(records_, other.records_);
  std::swap(count_, other.count_);
  std::swap(capacity_, other.capacity_);
  std::swap(nextSceneId_, other.nextSceneId_);
  std::swap(initialized_, other.initialized_);
  std::swap(healthy_, other.healthy_);
}

bool SceneRegistry::reserve(size_t requested) {
  if (requested <= capacity_) return true;
  if (requested > std::numeric_limits<size_t>::max() / sizeof(SceneRecord)) {
    return false;
  }
  size_t expanded = capacity_ == 0 ? 4 : capacity_;
  while (expanded < requested) {
    if (expanded > std::numeric_limits<size_t>::max() / 2) {
      expanded = requested;
      break;
    }
    expanded *= 2;
  }
  SceneRecord* replacement = new (std::nothrow) SceneRecord[expanded]();
  if (replacement == nullptr) return false;
  for (size_t i = 0; i < count_; ++i) replacement[i] = records_[i];
  delete[] records_;
  records_ = replacement;
  capacity_ = expanded;
  return true;
}

const SceneRecord* SceneRegistry::at(size_t index) const {
  return index < count_ ? &records_[index] : nullptr;
}

SceneRecord* SceneRegistry::at(size_t index) {
  return index < count_ ? &records_[index] : nullptr;
}

const SceneRecord* SceneRegistry::find(SceneId sceneId) const {
  for (size_t i = 0; i < count_; ++i) {
    if (records_[i].sceneId == sceneId) {
      return &records_[i];
    }
  }
  return nullptr;
}

SceneRecord* SceneRegistry::find(SceneId sceneId) {
  for (size_t i = 0; i < count_; ++i) {
    if (records_[i].sceneId == sceneId) {
      return &records_[i];
    }
  }
  return nullptr;
}

SceneRegistryStatus SceneRegistry::add(const char* name, SceneId& outId) {
  outId = kInvalidSceneId;
  if (name == nullptr || name[0] == '\0') return SceneRegistryStatus::Invalid;
  if (!reserve(count_ + 1)) return SceneRegistryStatus::Full;
  SceneRecord& record = records_[count_];
  record = SceneRecord{};
  record.sceneId = nextSceneId_++;
  record.enabled = true;
  copyName(record.name, name);
  ++count_;
  initialized_ = true;
  outId = record.sceneId;
  return SceneRegistryStatus::Ok;
}

SceneRegistryStatus SceneRegistry::remove(SceneId sceneId) {
  for (size_t i = 0; i < count_; ++i) {
    if (records_[i].sceneId != sceneId) {
      continue;
    }
    for (size_t j = i + 1; j < count_; ++j) {
      records_[j - 1] = records_[j];
    }
    --count_;
    records_[count_] = SceneRecord{};
    initialized_ = true;
    return SceneRegistryStatus::Ok;
  }
  return SceneRegistryStatus::NotFound;
}

SceneRegistryStatus SceneRegistry::rename(SceneId sceneId, const char* name) {
  SceneRecord* record = find(sceneId);
  if (record == nullptr) {
    return SceneRegistryStatus::NotFound;
  }
  if (name == nullptr || name[0] == '\0') {
    return SceneRegistryStatus::Invalid;
  }
  copyName(record->name, name);
  return SceneRegistryStatus::Ok;
}

SceneRegistryStatus SceneRegistry::setEnabled(SceneId sceneId, bool enabled) {
  SceneRecord* record = find(sceneId);
  if (record == nullptr) {
    return SceneRegistryStatus::NotFound;
  }
  record->enabled = enabled;
  return SceneRegistryStatus::Ok;
}

SceneRegistryStatus SceneRegistry::replace(const SceneRecord& record) {
  if (record.sceneId == kInvalidSceneId || record.name[0] == '\0' ||
      !validSteps(record.startSteps, record.startCount) ||
      !validSteps(record.stopSteps, record.stopCount)) {
    return SceneRegistryStatus::Invalid;
  }
  SceneRecord* existing = find(record.sceneId);
  if (existing == nullptr) {
    return SceneRegistryStatus::NotFound;
  }
  *existing = record;
  return SceneRegistryStatus::Ok;
}

void SceneRegistry::clear(bool initialized) {
  for (size_t i = 0; i < count_; ++i) {
    records_[i] = SceneRecord{};
  }
  count_ = 0;
  nextSceneId_ = 1;
  initialized_ = initialized;
  healthy_ = true;
}

bool SceneRegistry::restore(const SceneRecord* records, size_t count,
                            SceneId nextSceneId, bool initialized) {
  if (records == nullptr && count > 0) {
    return false;
  }
  if (nextSceneId == kInvalidSceneId) return false;
  clear(initialized);
  if (!reserve(count)) return false;
  for (size_t i = 0; i < count; ++i) {
    if (records[i].sceneId == kInvalidSceneId ||
        !validSteps(records[i].startSteps, records[i].startCount) ||
        !validSteps(records[i].stopSteps, records[i].stopCount)) {
      clear(true);
      return false;
    }
    records_[i] = records[i];
  }
  count_ = count;
  nextSceneId_ = nextSceneId;
  initialized_ = initialized;
  return true;
}

}  // namespace studio
