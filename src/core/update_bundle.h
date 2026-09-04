#pragma once

#include <cstddef>
#include <cstdint>

namespace studio {

// A signed update bundle is the canonical manifest bytes (exactly the bytes
// the detached `bleep-update.sig` covers, ending in one newline) followed by
// the base64 text of that DER ECDSA signature and a final newline. Fetching
// the bundle needs one release-asset download instead of two, which halves
// the TLS handshakes to github.com; those handshakes run within a few KB of
// the ESP32-C3 heap floor and intermittently fail with cert-verify errors.
//
// Splits `data` in place: `manifestLength` receives the manifest byte count
// (including its newline) and the decoded signature is written to
// `signature`. Returns false for malformed input or a signature that does not
// fit `signatureCapacity`.
inline int base64Value(char value) {
  if (value >= 'A' && value <= 'Z') return value - 'A';
  if (value >= 'a' && value <= 'z') return value - 'a' + 26;
  if (value >= '0' && value <= '9') return value - '0' + 52;
  if (value == '+') return 62;
  if (value == '/') return 63;
  return -1;
}

inline bool decodeBase64(const char* text, size_t length, uint8_t* output,
                         size_t capacity, size_t& written) {
  written = 0;
  uint32_t accumulator = 0;
  int bits = 0;
  size_t padding = 0;
  for (size_t i = 0; i < length; ++i) {
    const char value = text[i];
    if (value == '=') {
      ++padding;
      continue;
    }
    if (padding != 0) return false;
    const int digit = base64Value(value);
    if (digit < 0) return false;
    accumulator = (accumulator << 6) | static_cast<uint32_t>(digit);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      if (written >= capacity) return false;
      output[written++] = static_cast<uint8_t>((accumulator >> bits) & 0xFF);
    }
  }
  return padding <= 2 && written > 0;
}

inline bool splitUpdateBundle(const char* data, size_t length,
                              size_t& manifestLength, uint8_t* signature,
                              size_t signatureCapacity,
                              size_t& signatureLength) {
  manifestLength = 0;
  signatureLength = 0;
  if (data == nullptr || length < 4) return false;
  // The manifest is one JSON object terminated by the first newline; the
  // signature line follows it.
  size_t newline = 0;
  while (newline < length && data[newline] != '\n') ++newline;
  if (newline == 0 || newline + 1 >= length || data[0] != '{') return false;
  manifestLength = newline + 1;
  const char* line = data + manifestLength;
  size_t lineLength = length - manifestLength;
  while (lineLength > 0 &&
         (line[lineLength - 1] == '\n' || line[lineLength - 1] == '\r')) {
    --lineLength;
  }
  if (lineLength == 0) return false;
  for (size_t i = 0; i < lineLength; ++i) {
    if (line[i] == '\n') return false;  // more than two lines
  }
  return decodeBase64(line, lineLength, signature, signatureCapacity,
                      signatureLength);
}

}  // namespace studio
