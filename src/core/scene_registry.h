#pragma once

#include "core/scene_types.h"

namespace studio {

enum class SceneRegistryStatus : uint8_t {
  Ok,
  Full,
  Invalid,
  NotFound,
};

class SceneRegistry {
 public:
  size_t capacity() const { return CONFIG_MAX_SCENES; }
  size_t count() const { return count_; }
  bool initialized() const { return initialized_; }
  SceneId nextSceneId() const { return nextSceneId_; }

  const SceneRecord* at(size_t index) const;
  SceneRecord* at(size_t index);
  const SceneRecord* find(SceneId sceneId) const;
  SceneRecord* find(SceneId sceneId);

  SceneRegistryStatus add(const char* name, SceneId& outId);
  SceneRegistryStatus remove(SceneId sceneId);
  SceneRegistryStatus rename(SceneId sceneId, const char* name);
  SceneRegistryStatus setEnabled(SceneId sceneId, bool enabled);
  SceneRegistryStatus replace(const SceneRecord& record);

  void clear(bool initialized = true);
  bool restore(const SceneRecord* records, size_t count, SceneId nextSceneId,
               bool initialized);

 private:
  SceneRecord records_[CONFIG_MAX_SCENES] = {};
  size_t count_ = 0;
  SceneId nextSceneId_ = 1;
  bool initialized_ = false;
};

}  // namespace studio
