#include "core/ble/ble_types.h"

#include <cctype>
#include <cstdio>
#include <cstring>

namespace studio::ble {

namespace {

struct Field {
  uint8_t type = 0;
  const uint8_t* data = nullptr;
  size_t length = 0;
};

bool nextField(const Advertisement& advertisement, size_t& offset,
               Field& field) {
  if (offset >= advertisement.payloadLength) {
    return false;
  }
  const size_t fieldLength = advertisement.payload[offset];
  if (fieldLength == 0 ||
      offset + fieldLength >= advertisement.payloadLength) {
    offset = advertisement.payloadLength;
    return false;
  }
  field.type = advertisement.payload[offset + 1];
  field.data = advertisement.payload + offset + 2;
  field.length = fieldLength - 1;
  offset += fieldLength + 1;
  return true;
}

int hexNibble(char value) {
  if (value >= '0' && value <= '9') {
    return value - '0';
  }
  value = static_cast<char>(std::tolower(static_cast<unsigned char>(value)));
  return value >= 'a' && value <= 'f' ? value - 'a' + 10 : -1;
}

size_t parseUuid(const char* text, uint8_t* bytes, size_t capacity) {
  if (text == nullptr) {
    return 0;
  }
  size_t count = 0;
  int high = -1;
  for (; *text != '\0'; ++text) {
    if (*text == '-') {
      continue;
    }
    const int nibble = hexNibble(*text);
    if (nibble < 0) {
      return 0;
    }
    if (high < 0) {
      high = nibble;
    } else {
      if (count >= capacity) {
        return 0;
      }
      bytes[count++] = static_cast<uint8_t>((high << 4) | nibble);
      high = -1;
    }
  }
  return high < 0 ? count : 0;
}

}  // namespace

bool addressEqual(const Address& left, const Address& right) {
  return left.type == right.type &&
         std::strncmp(left.value, right.value, sizeof(left.value)) == 0;
}

uint8_t identityAddressType(uint8_t addressType) {
  return addressType == 2 || addressType == 3
             ? static_cast<uint8_t>(addressType - 2)
             : addressType;
}

bool advertisementName(const Advertisement& advertisement, char* output,
                       size_t capacity) {
  if (output == nullptr || capacity == 0) {
    return false;
  }
  output[0] = '\0';
  size_t offset = 0;
  Field field;
  while (nextField(advertisement, offset, field)) {
    if (field.type != 0x08 && field.type != 0x09) {
      continue;
    }
    const size_t copyLength =
        field.length < capacity - 1 ? field.length : capacity - 1;
    std::memcpy(output, field.data, copyLength);
    output[copyLength] = '\0';
    return true;
  }
  return false;
}

bool advertisementNameEquals(const Advertisement& advertisement,
                             const char* expected) {
  char name[kBleNameCapacity];
  return expected != nullptr && advertisementName(advertisement, name,
                                                   sizeof(name)) &&
         std::strcmp(name, expected) == 0;
}

bool advertisementNameContains(const Advertisement& advertisement,
                               const char* token) {
  char name[kBleNameCapacity];
  return token != nullptr &&
         advertisementName(advertisement, name, sizeof(name)) &&
         std::strstr(name, token) != nullptr;
}

bool advertisesService(const Advertisement& advertisement, const char* uuid) {
  uint8_t parsed[16];
  const size_t parsedLength = parseUuid(uuid, parsed, sizeof(parsed));
  if (parsedLength != 2 && parsedLength != 16) {
    return false;
  }

  size_t offset = 0;
  Field field;
  while (nextField(advertisement, offset, field)) {
    const bool is16 = parsedLength == 2 &&
                      (field.type == 0x02 || field.type == 0x03);
    const bool is128 = parsedLength == 16 &&
                       (field.type == 0x06 || field.type == 0x07);
    if (!is16 && !is128) {
      continue;
    }
    for (size_t start = 0; start + parsedLength <= field.length;
         start += parsedLength) {
      bool match = true;
      for (size_t i = 0; i < parsedLength; ++i) {
        if (field.data[start + i] != parsed[parsedLength - 1 - i]) {
          match = false;
          break;
        }
      }
      if (match) {
        return true;
      }
    }
  }
  return false;
}

uint16_t manufacturerCompanyId(const Advertisement& advertisement) {
  size_t offset = 0;
  Field field;
  while (nextField(advertisement, offset, field)) {
    if (field.type == 0xff && field.length >= 2) {
      return static_cast<uint16_t>(field.data[0]) |
             (static_cast<uint16_t>(field.data[1]) << 8);
    }
  }
  return 0xffff;
}

bool meshProxyNetworkId(const Advertisement& advertisement,
                        uint8_t output[8]) {
  if (output == nullptr) return false;
  size_t offset = 0;
  Field field;
  while (nextField(advertisement, offset, field)) {
    // Service Data - 16-bit UUID, Mesh Proxy UUID 0x1828, Network ID type.
    if (field.type == 0x16 && field.length >= 11 && field.data[0] == 0x28 &&
        field.data[1] == 0x18 && field.data[2] == 0x00) {
      std::memcpy(output, field.data + 3, 8);
      return true;
    }
  }
  return false;
}

}  // namespace studio::ble
