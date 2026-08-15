#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace studio {

inline uint32_t fnv1a(const uint8_t* data, size_t length) {
  uint32_t value = 2166136261u;
  for (size_t i = 0; i < length; ++i) {
    value = (value ^ data[i]) * 16777619u;
  }
  return value;
}

class BlobWriter {
 public:
  BlobWriter(uint8_t* data, size_t capacity)
      : data_(data), capacity_(capacity) {}

  bool u8(uint8_t value) { return bytes(&value, sizeof(value)); }

  bool u16(uint16_t value) {
    const uint8_t encoded[] = {
        static_cast<uint8_t>(value), static_cast<uint8_t>(value >> 8)};
    return bytes(encoded, sizeof(encoded));
  }

  bool u32(uint32_t value) {
    const uint8_t encoded[] = {
        static_cast<uint8_t>(value), static_cast<uint8_t>(value >> 8),
        static_cast<uint8_t>(value >> 16), static_cast<uint8_t>(value >> 24)};
    return bytes(encoded, sizeof(encoded));
  }

  bool bytes(const void* source, size_t length) {
    if (source == nullptr || length > remaining()) {
      valid_ = false;
      return false;
    }
    std::memcpy(data_ + position_, source, length);
    position_ += length;
    return true;
  }

  size_t size() const { return position_; }
  size_t remaining() const { return capacity_ - position_; }
  bool valid() const { return valid_; }

 private:
  uint8_t* data_ = nullptr;
  size_t capacity_ = 0;
  size_t position_ = 0;
  bool valid_ = true;
};

class BlobReader {
 public:
  BlobReader(const uint8_t* data, size_t length)
      : data_(data), length_(length) {}

  bool u8(uint8_t& value) { return bytes(&value, sizeof(value)); }

  bool u16(uint16_t& value) {
    uint8_t encoded[2];
    if (!bytes(encoded, sizeof(encoded))) return false;
    value = static_cast<uint16_t>(encoded[0]) |
            static_cast<uint16_t>(encoded[1]) << 8;
    return true;
  }

  bool u32(uint32_t& value) {
    uint8_t encoded[4];
    if (!bytes(encoded, sizeof(encoded))) return false;
    value = static_cast<uint32_t>(encoded[0]) |
            static_cast<uint32_t>(encoded[1]) << 8 |
            static_cast<uint32_t>(encoded[2]) << 16 |
            static_cast<uint32_t>(encoded[3]) << 24;
    return true;
  }

  bool bytes(void* destination, size_t length) {
    if (destination == nullptr || length > remaining()) {
      valid_ = false;
      return false;
    }
    std::memcpy(destination, data_ + position_, length);
    position_ += length;
    return true;
  }

  template <size_t N>
  bool text(char (&destination)[N]) {
    if (!bytes(destination, N)) return false;
    destination[N - 1] = '\0';
    return true;
  }

  size_t position() const { return position_; }
  size_t remaining() const { return length_ - position_; }
  bool valid() const { return valid_; }

 private:
  const uint8_t* data_ = nullptr;
  size_t length_ = 0;
  size_t position_ = 0;
  bool valid_ = true;
};

}  // namespace studio
