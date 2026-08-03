#include "devices/canon_ble/protocol.h"

#include <cstring>

namespace canon_ble {

PairingName buildPairingName(const char* name) {
  PairingName result;
  result.bytes[0] = kPairingNamePrefix;
  result.len = 1;
  if (name == nullptr) {
    return result;
  }
  const size_t nameLength = std::strlen(name);
  const size_t copyLength =
      nameLength < sizeof(result.bytes) - 1 ? nameLength : sizeof(result.bytes) - 1;
  std::memcpy(&result.bytes[1], name, copyLength);
  result.len += copyLength;
  return result;
}

}  // namespace canon_ble
