#include "core/mesh/mesh_repository.h"

#include <esp_random.h>

#include "core/preferences_store.h"

namespace studio::mesh {
namespace {

Repository instance;
studio::PreferencesMeshBackend backend;
Store store(backend);

}  // namespace

Repository& repository() { return instance; }

bool Repository::begin() {
  if (loaded_) return true;
  const studio::ConfigLoadStatus status = store.load(data_);
  if (status == studio::ConfigLoadStatus::Corrupt) return false;
  if (status == studio::ConfigLoadStatus::Missing) {
    data_ = StoreData{};
    esp_fill_random(data_.network.networkKey, 16);
    esp_fill_random(data_.network.applicationKey, 16);
    data_.network.initialized = true;
    if (!store.save(data_)) return false;
  }
  if (!data_.network.initialized || data_.network.nextUnicastAddress == 0 ||
      data_.network.nextUnicastAddress > 0x7fff)
    return false;
  sequences_.begin(store, data_);
  loaded_ = true;
  return true;
}

bool Repository::save() { return loaded_ && store.save(data_); }

}  // namespace studio::mesh
