#pragma once

#include "core/config_store.h"
#include "core/scene_registry.h"

namespace studio {

class SceneStore {
 public:
  static constexpr uint16_t kSchemaVersion = 1;
  static constexpr size_t kMaxBlobSize = 1536;

  explicit SceneStore(IConfigBackend& backend) : backend_(backend) {}

  ConfigLoadStatus load(SceneRegistry& registry);
  bool save(const SceneRegistry& registry);

 private:
  IConfigBackend& backend_;
};

}  // namespace studio
