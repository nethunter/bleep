#pragma once

#include <cstddef>
#include <cstdint>

namespace studio::mesh {

inline bool supportsNoOobProvisioning(const uint8_t* pdu, size_t length) {
  // A non-zero static-OOB capability only advertises an additional method;
  // selecting Authentication Method 0 remains valid.
  return pdu != nullptr && length == 12 && pdu[0] == 0x01 && pdu[1] != 0 &&
         pdu[2] == 0 && (pdu[3] & 1) != 0 && pdu[4] == 0 && pdu[6] == 0 &&
         pdu[9] == 0;
}

}  // namespace studio::mesh
