#pragma once

#include "core/mesh/mesh_store.h"

namespace studio::mesh {

class Repository {
 public:
  bool begin();
  bool save();
  StoreData& data() { return data_; }
  const StoreData& data() const { return data_; }
  SequenceAllocator& sequences() { return sequences_; }

 private:
  bool loaded_ = false;
  StoreData data_;
  SequenceAllocator sequences_;
};

Repository& repository();

}  // namespace studio::mesh
