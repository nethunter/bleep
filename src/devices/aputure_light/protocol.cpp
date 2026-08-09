#include "devices/aputure_light/protocol.h"

#include <cmath>
#include <cctype>
#include <cstring>

#include "devices/aputure_light/crypto.h"

namespace aputure_light {
namespace {

constexpr uint8_t kPowerOn[] = {0x26,0x8d,0,0,0,0,0,0,0,1,0x8c};
constexpr uint8_t kPowerOff[] = {0x26,0x8c,0,0,0,0,0,0,0,0,0x8c};
constexpr uint8_t kPowerStatusGet[] = {0x26,0x0e,0,0,0,0,0,0,0,0,0x0e};
constexpr uint8_t kNodeReset[] = {0x26,0x9d,0,0,0,0,0,0,0,0,0x9d};

bool containsIgnoreCase(const char* text, const char* token) {
  if (text == nullptr || token == nullptr || token[0] == '\0') return false;
  for (const char* start = text; *start != '\0'; ++start) {
    const char* candidate = start;
    const char* expected = token;
    while (*candidate != '\0' && *expected != '\0' &&
           std::tolower(static_cast<unsigned char>(*candidate)) ==
               std::tolower(static_cast<unsigned char>(*expected))) {
      ++candidate;
      ++expected;
    }
    if (*expected == '\0') return true;
  }
  return false;
}

uint8_t scaleGamma(uint8_t channel, uint8_t maximum) {
  const double normalized = static_cast<double>(channel) / 255.0;
  return static_cast<uint8_t>(std::lround(std::pow(normalized, 0.75) * maximum));
}

uint8_t scaleLinear(uint8_t percent, uint8_t maximum) {
  return static_cast<uint8_t>(std::lround(static_cast<double>(percent) * maximum / 100.0));
}

bool buildVendor(const uint8_t tail[9], AccessPayload& output) {
  output.length = kAccessPayloadSize;
  output.bytes[0] = 0x26;
  output.bytes[1] = vendorChecksum(tail);
  std::memcpy(output.bytes + 2, tail, 9);
  return true;
}

void putBe16(uint8_t* out, uint16_t value) {
  out[0] = static_cast<uint8_t>(value >> 8);
  out[1] = static_cast<uint8_t>(value);
}

uint16_t getBe16(const uint8_t* input) {
  return static_cast<uint16_t>(input[0]) << 8 | input[1];
}

bool encodeNetworkTransport(const uint8_t networkKey[16], const uint8_t* lower,
                            size_t lowerLength, uint32_t sequence,
                            uint16_t source, uint16_t destination,
                            uint32_t ivIndex, uint8_t ttl, NetworkPdu& output) {
  if (lower == nullptr || lowerLength == 0 || lowerLength > 16) return false;
  NetworkKeys keys;
  meshK2(networkKey, keys);
  const uint8_t sequenceBytes[3] = {static_cast<uint8_t>(sequence >> 16),
      static_cast<uint8_t>(sequence >> 8), static_cast<uint8_t>(sequence)};
  uint8_t plain[18];
  putBe16(plain, destination);
  std::memcpy(plain + 2, lower, lowerLength);
  const size_t plainLength = lowerLength + 2;
  uint8_t nonce[13] = {0x00, ttl};
  std::memcpy(nonce + 2, sequenceBytes, 3);
  putBe16(nonce + 5, source);
  nonce[7] = nonce[8] = 0;
  nonce[9] = static_cast<uint8_t>(ivIndex >> 24);
  nonce[10] = static_cast<uint8_t>(ivIndex >> 16);
  nonce[11] = static_cast<uint8_t>(ivIndex >> 8);
  nonce[12] = static_cast<uint8_t>(ivIndex);
  uint8_t encrypted[22], mic[4];
  if (!aesCcmEncrypt(keys.encryption, nonce, sizeof(nonce), plain, plainLength,
                     sizeof(mic), encrypted, mic)) return false;
  std::memcpy(encrypted + plainLength, mic, sizeof(mic));
  uint8_t privacyInput[16] = {};
  privacyInput[5] = static_cast<uint8_t>(ivIndex >> 24);
  privacyInput[6] = static_cast<uint8_t>(ivIndex >> 16);
  privacyInput[7] = static_cast<uint8_t>(ivIndex >> 8);
  privacyInput[8] = static_cast<uint8_t>(ivIndex);
  std::memcpy(privacyInput + 9, encrypted, 7);
  uint8_t pecb[16];
  aes128EncryptBlock(keys.privacy, privacyInput, pecb);
  const uint8_t header[6] = {ttl, sequenceBytes[0], sequenceBytes[1],
      sequenceBytes[2], static_cast<uint8_t>(source >> 8),
      static_cast<uint8_t>(source)};
  output.bytes[0] = static_cast<uint8_t>(((ivIndex >> 24) & 0x80) | keys.nid);
  for (size_t i = 0; i < 6; ++i) output.bytes[i + 1] = header[i] ^ pecb[i];
  std::memcpy(output.bytes + 7, encrypted, plainLength + sizeof(mic));
  output.length = 7 + plainLength + sizeof(mic);
  return true;
}

}  // namespace

bool inferKnownVendorModel(const char* displayName, const char* bleName,
                           uint16_t& companyId, uint16_t& modelId) {
  if (containsIgnoreCase(displayName, "MC Pro") ||
      containsIgnoreCase(bleName, "MC Pro") ||
      containsIgnoreCase(bleName, "Mesh Device")) {
    companyId = 0x03f6;
    modelId = 0x1000;
    return true;
  }
  if (containsIgnoreCase(displayName, "Ace 25c") ||
      containsIgnoreCase(bleName, "Ace 25c") ||
      containsIgnoreCase(bleName, "SLCK")) {
    companyId = 0x0211;
    modelId = 0x0000;
    return true;
  }
  return false;
}

uint8_t vendorChecksum(const uint8_t tail[9]) {
  uint8_t sum = 0;
  for (size_t i = 0; i < 9; ++i) sum = static_cast<uint8_t>(sum + tail[i]);
  return sum;
}

bool buildPowerAccess(bool on, AccessPayload& output) {
  std::memcpy(output.bytes, on ? kPowerOn : kPowerOff, kAccessPayloadSize);
  output.length = kAccessPayloadSize;
  return true;
}

bool buildPowerStatusGetAccess(AccessPayload& output) {
  std::memcpy(output.bytes, kPowerStatusGet, kAccessPayloadSize);
  output.length = kAccessPayloadSize;
  return true;
}

bool parseVendorPowerStatus(const uint8_t* access, size_t length,
                            VendorPowerStatus& output) {
  if (access == nullptr || length != kAccessPayloadSize || access[0] != 0x26 ||
      vendorChecksum(access + 2) != access[1] || access[2] > 1 ||
      access[3] != 0 || access[4] != 0 || access[5] != 0 ||
      access[9] > 250 || (access[10] != 1 && access[10] != 2)) {
    return false;
  }
  output.on = access[2] != 0;
  output.storedIntensity = access[9];
  output.profile = access[10];
  return true;
}

bool buildNodeResetAccess(AccessPayload& output) {
  std::memcpy(output.bytes, kNodeReset, kAccessPayloadSize);
  output.length = kAccessPayloadSize;
  return true;
}

bool buildCctAccess(uint16_t kelvin, int16_t tintPermille,
                    uint8_t brightness, AccessPayload& output) {
  if (kelvin < kCctMinKelvin || kelvin > kCctMaxKelvin ||
      tintPermille < -1000 || tintPermille > 1000 || brightness > 100) {
    return false;
  }
  int32_t cct = static_cast<int32_t>(std::lround(
      0x9f41 + (static_cast<int32_t>(kelvin) - 5000) * 8.0 / 5.0));
  const double tint = static_cast<double>(tintPermille) / 1000.0;
  const int32_t tintByte = static_cast<int32_t>(std::lround(0x40 - tint * 0x40));
  cct -= static_cast<int32_t>(std::lround(tint));
  const uint8_t tail[9] = {
      0,0,0,0,static_cast<uint8_t>(tintByte),static_cast<uint8_t>(cct),
      static_cast<uint8_t>(cct >> 8),static_cast<uint8_t>(brightness * 250 / 100),0x82};
  return buildVendor(tail, output);
}

bool buildRgbAccess(uint32_t rgb, uint8_t brightness,
                    AccessPayload& output) {
  if (rgb > 0xffffff || brightness > 100) return false;
  const uint8_t red = static_cast<uint8_t>(rgb >> 16);
  const uint8_t green = static_cast<uint8_t>(rgb >> 8);
  const uint8_t blue = static_cast<uint8_t>(rgb);
  const uint8_t tail[9] = {
      scaleLinear(brightness, 0x80), scaleLinear(brightness, 0x3e), 0, 0,
      scaleGamma(blue, 0xa0),
      static_cast<uint8_t>(scaleGamma(green, 0x80) + scaleGamma(blue, 0x0f)),
      scaleGamma(green, 0x3e), scaleGamma(red, 0xfa), 0x84};
  return buildVendor(tail, output);
}

bool encodeAccessMessage(const uint8_t networkKey[16],
                         const uint8_t applicationKey[16],
                         const uint8_t* access, size_t accessLength,
                         uint32_t sequence, uint16_t source,
                         uint16_t destination, uint32_t ivIndex,
                         NetworkPdu& output, uint8_t ttl) {
  if (networkKey == nullptr || applicationKey == nullptr || access == nullptr ||
      accessLength == 0 || accessLength > 15 || sequence > 0xffffff ||
      source == 0 || source > 0x7fff || destination == 0 || ttl > 0x7f) {
    return false;
  }
  NetworkKeys keys;
  meshK2(networkKey, keys);
  const uint8_t aid = meshK4(applicationKey);
  const uint8_t sequenceBytes[3] = {
      static_cast<uint8_t>(sequence >> 16), static_cast<uint8_t>(sequence >> 8),
      static_cast<uint8_t>(sequence)};
  uint8_t applicationNonce[13] = {0x01,0x00};
  std::memcpy(applicationNonce + 2, sequenceBytes, 3);
  putBe16(applicationNonce + 5, source);
  putBe16(applicationNonce + 7, destination);
  applicationNonce[9] = static_cast<uint8_t>(ivIndex >> 24);
  applicationNonce[10] = static_cast<uint8_t>(ivIndex >> 16);
  applicationNonce[11] = static_cast<uint8_t>(ivIndex >> 8);
  applicationNonce[12] = static_cast<uint8_t>(ivIndex);
  uint8_t encryptedAccess[19] = {};
  uint8_t appMic[4];
  if (!aesCcmEncrypt(applicationKey, applicationNonce, sizeof(applicationNonce),
                     access, accessLength, sizeof(appMic), encryptedAccess,
                     appMic)) return false;
  encryptedAccess[accessLength] = appMic[0];
  encryptedAccess[accessLength + 1] = appMic[1];
  encryptedAccess[accessLength + 2] = appMic[2];
  encryptedAccess[accessLength + 3] = appMic[3];
  uint8_t lower[20] = {static_cast<uint8_t>(0x40 | aid)};
  std::memcpy(lower + 1, encryptedAccess, accessLength + 4);
  const size_t lowerLength = accessLength + 5;
  uint8_t networkPlain[32];
  putBe16(networkPlain, destination);
  std::memcpy(networkPlain + 2, lower, lowerLength);
  const size_t networkPlainLength = 2 + lowerLength;
  uint8_t networkNonce[13] = {0x00, ttl};
  std::memcpy(networkNonce + 2, sequenceBytes, 3);
  putBe16(networkNonce + 5, source);
  networkNonce[7] = networkNonce[8] = 0;
  networkNonce[9] = static_cast<uint8_t>(ivIndex >> 24);
  networkNonce[10] = static_cast<uint8_t>(ivIndex >> 16);
  networkNonce[11] = static_cast<uint8_t>(ivIndex >> 8);
  networkNonce[12] = static_cast<uint8_t>(ivIndex);
  uint8_t encryptedNetwork[36];
  uint8_t netMic[4];
  if (!aesCcmEncrypt(keys.encryption, networkNonce, sizeof(networkNonce),
                     networkPlain, networkPlainLength, sizeof(netMic),
                     encryptedNetwork, netMic)) return false;
  std::memcpy(encryptedNetwork + networkPlainLength, netMic, sizeof(netMic));
  const size_t encryptedNetworkLength = networkPlainLength + sizeof(netMic);
  uint8_t privacyInput[16] = {};
  privacyInput[5] = static_cast<uint8_t>(ivIndex >> 24);
  privacyInput[6] = static_cast<uint8_t>(ivIndex >> 16);
  privacyInput[7] = static_cast<uint8_t>(ivIndex >> 8);
  privacyInput[8] = static_cast<uint8_t>(ivIndex);
  std::memcpy(privacyInput + 9, encryptedNetwork, 7);
  uint8_t pecb[16];
  aes128EncryptBlock(keys.privacy, privacyInput, pecb);
  const uint8_t header[6] = {ttl,sequenceBytes[0],sequenceBytes[1],sequenceBytes[2],
                             static_cast<uint8_t>(source >> 8),static_cast<uint8_t>(source)};
  output.bytes[0] = static_cast<uint8_t>(((ivIndex >> 24) & 0x80) | keys.nid);
  for (size_t i = 0; i < 6; ++i) output.bytes[i + 1] = static_cast<uint8_t>(header[i] ^ pecb[i]);
  std::memcpy(output.bytes + 7, encryptedNetwork, encryptedNetworkLength);
  output.length = 7 + encryptedNetworkLength;
  return output.length <= sizeof(output.bytes);
}

bool encodeDeviceMessage(const uint8_t networkKey[16],
                         const uint8_t deviceKey[16],
                         const uint8_t* access, size_t accessLength,
                         uint32_t sequence, uint16_t source,
                         uint16_t destination, uint32_t ivIndex,
                         NetworkPdu& output, uint8_t ttl) {
  if (networkKey == nullptr || deviceKey == nullptr || access == nullptr ||
      accessLength == 0 || accessLength > 11 || sequence > 0xffffff ||
      source == 0 || source > 0x7fff || destination == 0 || ttl > 0x7f) return false;
  NetworkKeys keys;
  meshK2(networkKey, keys);
  const uint8_t sequenceBytes[3] = {static_cast<uint8_t>(sequence >> 16),
      static_cast<uint8_t>(sequence >> 8), static_cast<uint8_t>(sequence)};
  uint8_t nonce[13] = {0x02,0x00};
  std::memcpy(nonce + 2, sequenceBytes, 3);
  putBe16(nonce + 5, source);
  putBe16(nonce + 7, destination);
  nonce[9] = static_cast<uint8_t>(ivIndex >> 24);
  nonce[10] = static_cast<uint8_t>(ivIndex >> 16);
  nonce[11] = static_cast<uint8_t>(ivIndex >> 8);
  nonce[12] = static_cast<uint8_t>(ivIndex);
  uint8_t encryptedAccess[40] = {};
  uint8_t appMic[4];
  if (!aesCcmEncrypt(deviceKey, nonce, sizeof(nonce), access, accessLength,
                     sizeof(appMic), encryptedAccess, appMic)) return false;
  std::memcpy(encryptedAccess + accessLength, appMic, 4);
  uint8_t networkPlain[48];
  putBe16(networkPlain, destination);
  networkPlain[2] = 0x00;
  std::memcpy(networkPlain + 3, encryptedAccess, accessLength + 4);
  const size_t networkPlainLength = accessLength + 7;
  uint8_t networkNonce[13] = {0x00, ttl};
  std::memcpy(networkNonce + 2, sequenceBytes, 3);
  putBe16(networkNonce + 5, source);
  networkNonce[7] = networkNonce[8] = 0;
  networkNonce[9] = static_cast<uint8_t>(ivIndex >> 24);
  networkNonce[10] = static_cast<uint8_t>(ivIndex >> 16);
  networkNonce[11] = static_cast<uint8_t>(ivIndex >> 8);
  networkNonce[12] = static_cast<uint8_t>(ivIndex);
  uint8_t encryptedNetwork[56];
  uint8_t netMic[4];
  if (!aesCcmEncrypt(keys.encryption, networkNonce, sizeof(networkNonce),
                     networkPlain, networkPlainLength, sizeof(netMic),
                     encryptedNetwork, netMic)) return false;
  std::memcpy(encryptedNetwork + networkPlainLength, netMic, 4);
  const size_t encryptedNetworkLength = networkPlainLength + 4;
  uint8_t privacyInput[16] = {};
  privacyInput[5] = static_cast<uint8_t>(ivIndex >> 24);
  privacyInput[6] = static_cast<uint8_t>(ivIndex >> 16);
  privacyInput[7] = static_cast<uint8_t>(ivIndex >> 8);
  privacyInput[8] = static_cast<uint8_t>(ivIndex);
  std::memcpy(privacyInput + 9, encryptedNetwork, 7);
  uint8_t pecb[16];
  aes128EncryptBlock(keys.privacy, privacyInput, pecb);
  const uint8_t header[6] = {ttl,sequenceBytes[0],sequenceBytes[1],sequenceBytes[2],
      static_cast<uint8_t>(source >> 8),static_cast<uint8_t>(source)};
  output.bytes[0] = static_cast<uint8_t>(((ivIndex >> 24) & 0x80) | keys.nid);
  for (size_t i = 0; i < 6; ++i) output.bytes[i + 1] = header[i] ^ pecb[i];
  std::memcpy(output.bytes + 7, encryptedNetwork, encryptedNetworkLength);
  output.length = 7 + encryptedNetworkLength;
  return output.length <= sizeof(output.bytes);
}


bool encodeSegmentedDeviceMessage(const uint8_t networkKey[16],
                                  const uint8_t deviceKey[16],
                                  const uint8_t* access, size_t accessLength,
                                  const uint32_t* sequences,
                                  size_t sequenceCount, uint16_t source,
                                  uint16_t destination, uint32_t ivIndex,
                                  NetworkPduBatch& output, uint8_t ttl) {
  output = NetworkPduBatch{};
  if (networkKey == nullptr || deviceKey == nullptr || access == nullptr ||
      sequences == nullptr || accessLength == 0 || accessLength > 32 ||
      source == 0 || source > 0x7fff || destination == 0 || ttl > 0x7f) return false;
  const size_t upperLength = accessLength + 4;
  const size_t segmentCount = (upperLength + 11) / 12;
  if (segmentCount < 2 || segmentCount > 4 || sequenceCount != segmentCount) return false;
  for (size_t i = 0; i < segmentCount; ++i) {
    if (sequences[i] > 0xffffff || (i > 0 && sequences[i] != sequences[i - 1] + 1)) return false;
  }
  const uint32_t firstSequence = sequences[0];
  const uint8_t sequenceBytes[3] = {static_cast<uint8_t>(firstSequence >> 16),
      static_cast<uint8_t>(firstSequence >> 8), static_cast<uint8_t>(firstSequence)};
  uint8_t nonce[13] = {0x02, 0x00};
  std::memcpy(nonce + 2, sequenceBytes, 3);
  putBe16(nonce + 5, source);
  putBe16(nonce + 7, destination);
  nonce[9] = static_cast<uint8_t>(ivIndex >> 24);
  nonce[10] = static_cast<uint8_t>(ivIndex >> 16);
  nonce[11] = static_cast<uint8_t>(ivIndex >> 8);
  nonce[12] = static_cast<uint8_t>(ivIndex);
  uint8_t upper[36] = {}, transMic[4];
  if (!aesCcmEncrypt(deviceKey, nonce, sizeof(nonce), access, accessLength,
                     sizeof(transMic), upper, transMic)) return false;
  std::memcpy(upper + accessLength, transMic, sizeof(transMic));
  const uint16_t seqZero = static_cast<uint16_t>(firstSequence & 0x1fff);
  const uint8_t segN = static_cast<uint8_t>(segmentCount - 1);
  for (uint8_t segO = 0; segO <= segN; ++segO) {
    const size_t offset = static_cast<size_t>(segO) * 12;
    const size_t partLength = upperLength - offset > 12 ? 12 : upperLength - offset;
    uint8_t lower[16] = {0x80,
        static_cast<uint8_t>(seqZero >> 6),
        static_cast<uint8_t>(((seqZero & 0x3f) << 2) | (segO >> 3)),
        static_cast<uint8_t>((segO << 5) | segN)};
    std::memcpy(lower + 4, upper + offset, partLength);
    if (!encodeNetworkTransport(networkKey, lower, partLength + 4,
          sequences[segO], source, destination, ivIndex, ttl,
          output.pdus[segO])) return false;
  }
  output.count = static_cast<uint8_t>(segmentCount);
  return true;
}

bool wrapProxyPdu(const NetworkPdu& network, uint8_t* output,
                  size_t capacity, size_t& outputLength) {
  if (output == nullptr || network.length == 0 || capacity < network.length + 1) return false;
  output[0] = 0x00;
  std::memcpy(output + 1, network.bytes, network.length);
  outputLength = network.length + 1;
  return true;
}

bool decodeProxyAccessMessage(const uint8_t networkKey[16],
                              const uint8_t applicationKey[16],
                              const uint8_t* proxyPdu, size_t proxyLength,
                              uint32_t ivIndex, DecodedAccessMessage& output) {
  output = DecodedAccessMessage{};
  if (networkKey == nullptr || applicationKey == nullptr || proxyPdu == nullptr ||
      proxyLength < 16 || proxyPdu[0] != 0x00) {
    return false;
  }
  const uint8_t* network = proxyPdu + 1;
  const size_t networkLength = proxyLength - 1;
  NetworkKeys keys;
  meshK2(networkKey, keys);
  if ((network[0] & 0x7f) != keys.nid || networkLength < 14) return false;

  const uint8_t* encryptedNetwork = network + 7;
  const size_t encryptedNetworkLength = networkLength - 7;
  if (encryptedNetworkLength < 11) return false;
  uint8_t privacyInput[16] = {};
  privacyInput[5] = static_cast<uint8_t>(ivIndex >> 24);
  privacyInput[6] = static_cast<uint8_t>(ivIndex >> 16);
  privacyInput[7] = static_cast<uint8_t>(ivIndex >> 8);
  privacyInput[8] = static_cast<uint8_t>(ivIndex);
  std::memcpy(privacyInput + 9, encryptedNetwork, 7);
  uint8_t pecb[16];
  aes128EncryptBlock(keys.privacy, privacyInput, pecb);
  uint8_t clearHeader[6];
  for (size_t i = 0; i < sizeof(clearHeader); ++i) {
    clearHeader[i] = static_cast<uint8_t>(network[i + 1] ^ pecb[i]);
  }
  if ((clearHeader[0] & 0x80) != 0) return false;
  output.sequence = static_cast<uint32_t>(clearHeader[1]) << 16 |
                    static_cast<uint32_t>(clearHeader[2]) << 8 |
                    clearHeader[3];
  output.source = getBe16(clearHeader + 4);

  uint8_t networkNonce[13] = {0x00};
  std::memcpy(networkNonce + 1, clearHeader, sizeof(clearHeader));
  networkNonce[7] = networkNonce[8] = 0;
  networkNonce[9] = static_cast<uint8_t>(ivIndex >> 24);
  networkNonce[10] = static_cast<uint8_t>(ivIndex >> 16);
  networkNonce[11] = static_cast<uint8_t>(ivIndex >> 8);
  networkNonce[12] = static_cast<uint8_t>(ivIndex);
  const size_t networkPlainLength = encryptedNetworkLength - 4;
  uint8_t networkPlain[64] = {};
  if (networkPlainLength > sizeof(networkPlain) ||
      !aesCcmDecrypt(keys.encryption, networkNonce, sizeof(networkNonce),
                     encryptedNetwork, networkPlainLength,
                     encryptedNetwork + networkPlainLength, 4,
                     networkPlain) ||
      networkPlainLength < 8) {
    return false;
  }
  output.destination = getBe16(networkPlain);

  const uint8_t* lower = networkPlain + 2;
  const size_t lowerLength = networkPlainLength - 2;
  if (lowerLength < 6 || (lower[0] & 0x80) != 0 ||
      (lower[0] & 0x40) == 0 ||
      (lower[0] & 0x3f) != meshK4(applicationKey)) {
    return false;
  }
  const uint8_t* encryptedUpper = lower + 1;
  const size_t encryptedUpperLength = lowerLength - 1;
  if (encryptedUpperLength < 5) return false;
  output.accessLength = encryptedUpperLength - 4;
  if (output.accessLength > sizeof(output.access)) return false;

  uint8_t applicationNonce[13] = {0x01,0x00};
  applicationNonce[2] = static_cast<uint8_t>(output.sequence >> 16);
  applicationNonce[3] = static_cast<uint8_t>(output.sequence >> 8);
  applicationNonce[4] = static_cast<uint8_t>(output.sequence);
  putBe16(applicationNonce + 5, output.source);
  putBe16(applicationNonce + 7, output.destination);
  applicationNonce[9] = static_cast<uint8_t>(ivIndex >> 24);
  applicationNonce[10] = static_cast<uint8_t>(ivIndex >> 16);
  applicationNonce[11] = static_cast<uint8_t>(ivIndex >> 8);
  applicationNonce[12] = static_cast<uint8_t>(ivIndex);
  return aesCcmDecrypt(applicationKey, applicationNonce,
                       sizeof(applicationNonce), encryptedUpper,
                       output.accessLength,
                       encryptedUpper + output.accessLength, 4,
                       output.access);
}

bool decodeProxyDeviceMessage(const uint8_t networkKey[16],
                              const uint8_t deviceKey[16],
                              const uint8_t* proxyPdu, size_t proxyLength,
                              uint32_t ivIndex, DecodedAccessMessage& output) {
  output = DecodedAccessMessage{};
  if (networkKey == nullptr || deviceKey == nullptr || proxyPdu == nullptr ||
      proxyLength < 16 || proxyPdu[0] != 0x00) {
    return false;
  }
  const uint8_t* network = proxyPdu + 1;
  const size_t networkLength = proxyLength - 1;
  NetworkKeys keys;
  meshK2(networkKey, keys);
  if ((network[0] & 0x7f) != keys.nid || networkLength < 14) return false;

  const uint8_t* encryptedNetwork = network + 7;
  const size_t encryptedNetworkLength = networkLength - 7;
  if (encryptedNetworkLength < 11) return false;
  uint8_t privacyInput[16] = {};
  privacyInput[5] = static_cast<uint8_t>(ivIndex >> 24);
  privacyInput[6] = static_cast<uint8_t>(ivIndex >> 16);
  privacyInput[7] = static_cast<uint8_t>(ivIndex >> 8);
  privacyInput[8] = static_cast<uint8_t>(ivIndex);
  std::memcpy(privacyInput + 9, encryptedNetwork, 7);
  uint8_t pecb[16];
  aes128EncryptBlock(keys.privacy, privacyInput, pecb);
  uint8_t clearHeader[6];
  for (size_t i = 0; i < sizeof(clearHeader); ++i) {
    clearHeader[i] = static_cast<uint8_t>(network[i + 1] ^ pecb[i]);
  }
  if ((clearHeader[0] & 0x80) != 0) return false;
  output.sequence = static_cast<uint32_t>(clearHeader[1]) << 16 |
                    static_cast<uint32_t>(clearHeader[2]) << 8 |
                    clearHeader[3];
  output.source = getBe16(clearHeader + 4);

  uint8_t networkNonce[13] = {0x00};
  std::memcpy(networkNonce + 1, clearHeader, sizeof(clearHeader));
  networkNonce[7] = networkNonce[8] = 0;
  networkNonce[9] = static_cast<uint8_t>(ivIndex >> 24);
  networkNonce[10] = static_cast<uint8_t>(ivIndex >> 16);
  networkNonce[11] = static_cast<uint8_t>(ivIndex >> 8);
  networkNonce[12] = static_cast<uint8_t>(ivIndex);
  const size_t networkPlainLength = encryptedNetworkLength - 4;
  uint8_t networkPlain[64] = {};
  if (networkPlainLength > sizeof(networkPlain) ||
      !aesCcmDecrypt(keys.encryption, networkNonce, sizeof(networkNonce),
                     encryptedNetwork, networkPlainLength,
                     encryptedNetwork + networkPlainLength, 4,
                     networkPlain) ||
      networkPlainLength < 8) {
    return false;
  }
  output.destination = getBe16(networkPlain);

  const uint8_t* lower = networkPlain + 2;
  const size_t lowerLength = networkPlainLength - 2;
  if (lowerLength < 6 || (lower[0] & 0xc0) != 0) return false;
  const uint8_t* encryptedUpper = lower + 1;
  const size_t encryptedUpperLength = lowerLength - 1;
  if (encryptedUpperLength < 5) return false;
  output.accessLength = encryptedUpperLength - 4;
  if (output.accessLength > sizeof(output.access)) return false;

  uint8_t deviceNonce[13] = {0x02, 0x00};
  deviceNonce[2] = static_cast<uint8_t>(output.sequence >> 16);
  deviceNonce[3] = static_cast<uint8_t>(output.sequence >> 8);
  deviceNonce[4] = static_cast<uint8_t>(output.sequence);
  putBe16(deviceNonce + 5, output.source);
  putBe16(deviceNonce + 7, output.destination);
  deviceNonce[9] = static_cast<uint8_t>(ivIndex >> 24);
  deviceNonce[10] = static_cast<uint8_t>(ivIndex >> 16);
  deviceNonce[11] = static_cast<uint8_t>(ivIndex >> 8);
  deviceNonce[12] = static_cast<uint8_t>(ivIndex);
  return aesCcmDecrypt(deviceKey, deviceNonce, sizeof(deviceNonce),
                       encryptedUpper, output.accessLength,
                       encryptedUpper + output.accessLength, 4,
                       output.access);
}

}  // namespace aputure_light
