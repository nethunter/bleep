#include "captive_dns_codec.h"

#include <cstring>

namespace portal::dns {
namespace {

constexpr size_t kHeaderSize = 12;
constexpr uint16_t kTypeA = 1;
constexpr uint16_t kClassIn = 1;

uint16_t readU16(const uint8_t* value) {
  return static_cast<uint16_t>((static_cast<uint16_t>(value[0]) << 8) |
                               value[1]);
}

void writeU16(uint8_t* destination, uint16_t value) {
  destination[0] = static_cast<uint8_t>(value >> 8);
  destination[1] = static_cast<uint8_t>(value & 0xff);
}

}  // namespace

size_t buildResponse(const uint8_t* request, size_t requestLength,
                     const uint8_t address[4], uint8_t* response,
                     size_t responseCapacity) {
  if (request == nullptr || address == nullptr || response == nullptr ||
      requestLength < kHeaderSize || requestLength > kMaxRequestSize ||
      (request[2] & 0xf8) != 0 || readU16(request + 4) != 1) {
    return 0;
  }

  size_t cursor = kHeaderSize;
  while (true) {
    if (cursor >= requestLength) return 0;
    const uint8_t labelLength = request[cursor++];
    if (labelLength == 0) break;
    if ((labelLength & 0xc0) != 0 || labelLength > 63 ||
        cursor + labelLength > requestLength) {
      return 0;
    }
    cursor += labelLength;
  }
  if (cursor + 4 > requestLength) return 0;

  const uint16_t queryType = readU16(request + cursor);
  const uint16_t queryClass = readU16(request + cursor + 2);
  const size_t questionEnd = cursor + 4;
  const bool includeAddress = queryType == kTypeA && queryClass == kClassIn;
  const size_t responseLength = questionEnd + (includeAddress ? 16 : 0);
  if (responseLength > responseCapacity) return 0;

  std::memset(response, 0, responseLength);
  response[0] = request[0];
  response[1] = request[1];
  response[2] = static_cast<uint8_t>(0x84 | (request[2] & 0x01));
  writeU16(response + 4, 1);
  writeU16(response + 6, includeAddress ? 1 : 0);
  std::memcpy(response + kHeaderSize, request + kHeaderSize,
              questionEnd - kHeaderSize);

  if (!includeAddress) return responseLength;
  uint8_t* answer = response + questionEnd;
  answer[0] = 0xc0;
  answer[1] = 0x0c;
  writeU16(answer + 2, kTypeA);
  writeU16(answer + 4, kClassIn);
  answer[9] = 60;
  writeU16(answer + 10, 4);
  std::memcpy(answer + 12, address, 4);
  return responseLength;
}

}  // namespace portal::dns
