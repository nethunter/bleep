#pragma once

#include "core/config_store.h"
#include "core/scene_registry.h"

namespace studio {

class SceneStore {
 public:
  static constexpr uint16_t kSchemaVersion = 4;
  // The NVS partition, not a product policy, is the persistence boundary.
  // This guard keeps corrupt metadata from requesting an unbounded allocation.
  static constexpr size_t kMaxBlobSize = 16 * 1024;

  explicit SceneStore(IConfigBackend& backend) : backend_(backend) {}

  ConfigLoadStatus load(SceneRegistry& registry, bool* migrated = nullptr);
  bool save(const SceneRegistry& registry);

 private:
  IConfigBackend& backend_;
};

}  // namespace studio
