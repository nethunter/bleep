#pragma once

#include <cstddef>
#include <cstdint>

#include "core/mesh/mesh_store.h"

namespace studio::mesh {

using RandomFill = bool (*)(uint8_t* output, size_t length, void* context);

// Creates and persists a complete mesh record before publishing it to the
// caller. A failed entropy source or write leaves both caller and store-facing
// live state untouched.
bool initializeNewMesh(Store& store, StoreData& data, RandomFill randomFill,
                       void* context = nullptr);

}  // namespace studio::mesh
