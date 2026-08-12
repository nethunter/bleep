#include "core/mesh/mesh_initializer.h"

namespace studio::mesh {

bool initializeNewMesh(Store& store, StoreData& data, RandomFill randomFill,
                       void* context) {
  if (randomFill == nullptr) return false;

  StoreData created;
  if (!randomFill(created.network.networkKey,
                  sizeof(created.network.networkKey), context) ||
      !randomFill(created.network.applicationKey,
                  sizeof(created.network.applicationKey), context)) {
    return false;
  }
  created.network.initialized = true;
  if (!store.save(created)) return false;

  data = created;
  return true;
}

bool persistReplacement(Store& store, StoreData& data,
                        const StoreData& replacement) {
  if (!store.save(replacement)) return false;
  data = replacement;
  return true;
}

}  // namespace studio::mesh
