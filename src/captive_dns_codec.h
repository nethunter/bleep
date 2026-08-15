#pragma once

#include <cstddef>
#include <cstdint>

namespace portal::dns {

constexpr size_t kMaxRequestSize = 512;
constexpr size_t kMaxResponseSize = kMaxRequestSize + 16;

// Builds an authoritative response for one-question DNS queries. A/IN queries
// receive the supplied IPv4 address; other types receive a successful empty
// answer so clients can fall back without waiting for a timeout.
size_t buildResponse(const uint8_t* request, size_t requestLength,
                     const uint8_t address[4], uint8_t* response,
                     size_t responseCapacity);

}  // namespace portal::dns
