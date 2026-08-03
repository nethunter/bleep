#pragma once

#include <cstddef>

#include "core/device_types.h"

namespace studio {

class DriverCatalog {
 public:
  static size_t count();
  static const DriverDescriptor* at(size_t index);
  static const DriverDescriptor* find(DriverId id);
};

}  // namespace studio

