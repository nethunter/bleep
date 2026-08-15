#include "captive_dns_codec.h"

namespace {

size_t makeDnsQuery(uint8_t* packet, uint16_t type, bool includeEdns) {
  const uint8_t name[] = {
      17, 'c', 'o', 'n', 'n', 'e', 'c', 't', 'i', 'v', 'i', 't', 'y',
      'c', 'h', 'e', 'c', 'k', 7, 'g', 's', 't', 'a', 't', 'i', 'c',
      3, 'c', 'o', 'm', 0};
  std::memset(packet, 0, portal::dns::kMaxRequestSize);
  packet[0] = 0x12;
  packet[1] = 0x34;
  packet[2] = 0x01;
  packet[5] = 1;
  packet[11] = includeEdns ? 1 : 0;
  std::memcpy(packet + 12, name, sizeof(name));
  size_t cursor = 12 + sizeof(name);
  packet[cursor++] = static_cast<uint8_t>(type >> 8);
  packet[cursor++] = static_cast<uint8_t>(type & 0xff);
  packet[cursor++] = 0;
  packet[cursor++] = 1;
  if (includeEdns) {
    const uint8_t opt[] = {0, 0, 41, 4, 208, 0, 0, 0, 0, 0, 0};
    std::memcpy(packet + cursor, opt, sizeof(opt));
    cursor += sizeof(opt);
  }
  return cursor;
}

}  // namespace

void test_captive_dns_accepts_edns_and_handles_query_types() {
  uint8_t request[portal::dns::kMaxRequestSize] = {};
  uint8_t response[portal::dns::kMaxResponseSize] = {};
  const uint8_t address[] = {192, 168, 4, 1};

  const size_t ednsLength = makeDnsQuery(request, 1, true);
  const size_t aResponse = portal::dns::buildResponse(
      request, ednsLength, address, response, sizeof(response));
  TEST_ASSERT_GREATER_THAN_UINT32(0, aResponse);
  TEST_ASSERT_EQUAL_HEX8(0x12, response[0]);
  TEST_ASSERT_EQUAL_HEX8(0x34, response[1]);
  TEST_ASSERT_EQUAL_UINT8(1, response[5]);
  TEST_ASSERT_EQUAL_UINT8(1, response[7]);
  TEST_ASSERT_EQUAL_UINT8(0, response[11]);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(address, response + aResponse - 4, 4);

  const size_t aaaaLength = makeDnsQuery(request, 28, true);
  const size_t aaaaResponse = portal::dns::buildResponse(
      request, aaaaLength, address, response, sizeof(response));
  TEST_ASSERT_GREATER_THAN_UINT32(0, aaaaResponse);
  TEST_ASSERT_EQUAL_UINT8(0, response[7]);
  TEST_ASSERT_EQUAL_UINT8(0, response[11]);

  const size_t plainLength = makeDnsQuery(request, 1, false);
  request[12] = 64;
  TEST_ASSERT_EQUAL_UINT32(
      0, portal::dns::buildResponse(request, plainLength, address, response,
                                    sizeof(response)));
}
