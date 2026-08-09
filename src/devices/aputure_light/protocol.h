#pragma once

#include <cstddef>
#include <cstdint>

namespace aputure_light {

constexpr uint16_t kCctMinKelvin = 2300;
constexpr uint16_t kCctMaxKelvin = 10000;
constexpr size_t kAccessPayloadSize = 11;
constexpr size_t kMaxNetworkPduSize = 64;

struct AccessPayload {
  uint8_t bytes[kAccessPayloadSize] = {};
  size_t length = 0;
};

struct NetworkPdu {
  uint8_t bytes[kMaxNetworkPduSize] = {};
  size_t length = 0;
};

struct NetworkPduBatch {
  NetworkPdu pdus[4] = {};
  uint8_t count = 0;
};

struct VendorPowerStatus {
  bool on = false;
  uint8_t storedIntensity = 0;
  uint8_t profile = 0;
};

struct DecodedAccessMessage {
  uint32_t sequence = 0;
  uint16_t source = 0;
  uint16_t destination = 0;
  uint8_t access[15] = {};
  size_t accessLength = 0;
};

bool buildPowerAccess(bool on, AccessPayload& output);
bool buildPowerStatusGetAccess(AccessPayload& output);
bool buildCctAccess(uint16_t kelvin, int16_t tintPermille,
                    uint8_t brightness, AccessPayload& output);
bool buildRgbAccess(uint32_t rgb, uint8_t brightness,
                    AccessPayload& output);
bool buildNodeResetAccess(AccessPayload& output);
bool parseVendorPowerStatus(const uint8_t* access, size_t length,
                            VendorPowerStatus& output);
bool decodeProxyAccessMessage(const uint8_t networkKey[16],
                              const uint8_t applicationKey[16],
                              const uint8_t* proxyPdu, size_t proxyLength,
                              uint32_t ivIndex, DecodedAccessMessage& output);
bool decodeProxyDeviceMessage(const uint8_t networkKey[16],
                              const uint8_t deviceKey[16],
                              const uint8_t* proxyPdu, size_t proxyLength,
                              uint32_t ivIndex, DecodedAccessMessage& output);

bool encodeAccessMessage(const uint8_t networkKey[16],
                         const uint8_t applicationKey[16],
                         const uint8_t* access, size_t accessLength,
                         uint32_t sequence, uint16_t source,
                         uint16_t destination, uint32_t ivIndex,
                         NetworkPdu& output, uint8_t ttl = 6);
bool encodeDeviceMessage(const uint8_t networkKey[16],
                         const uint8_t deviceKey[16],
                         const uint8_t* access, size_t accessLength,
                         uint32_t sequence, uint16_t source,
                         uint16_t destination, uint32_t ivIndex,
                         NetworkPdu& output, uint8_t ttl = 6);
bool encodeSegmentedDeviceMessage(const uint8_t networkKey[16],
                                  const uint8_t deviceKey[16],
                                  const uint8_t* access, size_t accessLength,
                                  const uint32_t* sequences,
                                  size_t sequenceCount, uint16_t source,
                                  uint16_t destination, uint32_t ivIndex,
                                  NetworkPduBatch& output, uint8_t ttl = 6);
bool wrapProxyPdu(const NetworkPdu& network, uint8_t* output,
                  size_t capacity, size_t& outputLength);

// Repairs pre-composition records only when their persisted identity names a
// model whose vendor tuple has been physically confirmed.
bool inferKnownVendorModel(const char* displayName, const char* bleName,
                           uint16_t& companyId, uint16_t& modelId);

// Returns the user-facing fixture name for a physically confirmed vendor
// model tuple. Unknown tuples remain unnamed rather than being guessed.
const char* knownVendorModelName(uint16_t companyId, uint16_t modelId);

uint8_t vendorChecksum(const uint8_t tail[9]);

}  // namespace aputure_light
