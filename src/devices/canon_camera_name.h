#pragma once

#include <cctype>
#include <cstddef>
#include <cstring>

namespace canon_camera {

inline bool equalsIgnoreCase(const char* left, const char* right) {
  if (left == nullptr || right == nullptr) return false;
  while (*left != '\0' && *right != '\0') {
    if (std::tolower(static_cast<unsigned char>(*left)) !=
        std::tolower(static_cast<unsigned char>(*right))) {
      return false;
    }
    ++left;
    ++right;
  }
  return *left == '\0' && *right == '\0';
}

inline bool containsIgnoreCase(const char* text, const char* token) {
  if (text == nullptr || token == nullptr || token[0] == '\0') return false;
  for (const char* start = text; *start != '\0'; ++start) {
    const char* hay = start;
    const char* needle = token;
    while (*hay != '\0' && *needle != '\0' &&
           std::tolower(static_cast<unsigned char>(*hay)) ==
               std::tolower(static_cast<unsigned char>(*needle))) {
      ++hay;
      ++needle;
    }
    if (*needle == '\0') return true;
  }
  return false;
}

inline bool isGenericDisplayName(const char* name) {
  return equalsIgnoreCase(name, "Canon (Smart)") ||
         equalsIgnoreCase(name, "Canon Smart") ||
         equalsIgnoreCase(name, "Canon (Trigger)") ||
         equalsIgnoreCase(name, "Canon Trigger");
}

inline bool canonicalDisplayName(const char* advertisedName, char* output,
                                 size_t capacity) {
  if (output == nullptr || capacity == 0 || advertisedName == nullptr ||
      advertisedName[0] == '\0' || isGenericDisplayName(advertisedName)) {
    return false;
  }
  const char* canonical = advertisedName;
  if (containsIgnoreCase(advertisedName, "EOSR6m2")) {
    canonical = "Canon EOS R6 Mark II";
  } else if (containsIgnoreCase(advertisedName, "EOSR6m3")) {
    canonical = "Canon EOS R6 Mark III";
  }
  std::strncpy(output, canonical, capacity - 1);
  output[capacity - 1] = '\0';
  return output[0] != '\0';
}

}  // namespace canon_camera
