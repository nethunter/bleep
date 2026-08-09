#include "devices/insta360/protocol.h"
#include <cstring>
namespace insta360 {
namespace { bool begins(const char* value, const char* prefix) { return value && std::strncmp(value, prefix, std::strlen(prefix)) == 0; } }
bool matchesCameraName(const char* name) {
  return begins(name, "X3 ") || begins(name, "X4 ") || begins(name, "X5 ") ||
         begins(name, "RS ") || begins(name, "ONE ") || begins(name, "GO 3") ||
         begins(name, "Insta360 GO 3") || begins(name, "GO Ultra") ||
         begins(name, "Insta360 GO Ultra");
}
bool isGoUltra(const char* name) { return name && (std::strstr(name, "GO Ultra") || std::strstr(name, "GO ULTRA")); }
}  // namespace insta360
