#pragma once

#include <cstddef>

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
  SceneRegistry() = default;
  SceneRegistry(const SceneRegistry& other);
  SceneRegistry(SceneRegistry&& other) noexcept;
  SceneRegistry& operator=(const SceneRegistry& other);
  SceneRegistry& operator=(SceneRegistry&& other) noexcept;
  ~SceneRegistry();

  size_t count() const { return count_; }
  bool initialized() const { return initialized_; }
  SceneId nextSceneId() const { return nextSceneId_; }
  bool healthy() const { return healthy_; }

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
  void swap(SceneRegistry& other) noexcept;

 private:
  bool reserve(size_t requested);

  SceneRecord* records_ = nullptr;
  size_t count_ = 0;
  size_t capacity_ = 0;
  SceneId nextSceneId_ = 1;
  bool initialized_ = false;
  bool healthy_ = true;
};

}  // namespace studio
