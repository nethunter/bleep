#include "core/scene_registry.h"

#include <cstring>

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
  if (count_ >= CONFIG_MAX_SCENES || name == nullptr || name[0] == '\0') {
    return count_ >= CONFIG_MAX_SCENES ? SceneRegistryStatus::Full
                                      : SceneRegistryStatus::Invalid;
  }
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
  for (size_t i = 0; i < CONFIG_MAX_SCENES; ++i) {
    records_[i] = SceneRecord{};
  }
  count_ = 0;
  nextSceneId_ = 1;
  initialized_ = initialized;
}

bool SceneRegistry::restore(const SceneRecord* records, size_t count,
                            SceneId nextSceneId, bool initialized) {
  if (records == nullptr && count > 0) {
    return false;
  }
  if (count > CONFIG_MAX_SCENES || nextSceneId == kInvalidSceneId) {
    return false;
  }
  clear(initialized);
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
