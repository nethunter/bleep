#include "devices/aputure_light/crypto.h"

#include <cstring>

namespace aputure_light {
namespace {

constexpr uint8_t kSbox[256] = {
    0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
    0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
    0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
    0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
    0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
    0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
    0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
    0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
    0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
    0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
    0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
    0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
    0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
    0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
    0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
    0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16};
constexpr uint8_t kRcon[11] = {0,1,2,4,8,16,32,64,128,0x1b,0x36};

uint8_t xtime(uint8_t value) {
  return static_cast<uint8_t>((value << 1) ^ ((value >> 7) * 0x1b));
}

void expandKey(const uint8_t key[16], uint8_t roundKey[176]) {
  std::memcpy(roundKey, key, 16);
  uint8_t temp[4];
  size_t generated = 16;
  uint8_t rcon = 1;
  while (generated < 176) {
    std::memcpy(temp, roundKey + generated - 4, 4);
    if (generated % 16 == 0) {
      const uint8_t first = temp[0];
      temp[0] = static_cast<uint8_t>(kSbox[temp[1]] ^ kRcon[rcon++]);
      temp[1] = kSbox[temp[2]];
      temp[2] = kSbox[temp[3]];
      temp[3] = kSbox[first];
    }
    for (uint8_t value : temp) {
      roundKey[generated] = static_cast<uint8_t>(roundKey[generated - 16] ^ value);
      ++generated;
    }
  }
}

void addRoundKey(uint8_t state[16], const uint8_t* key) {
  for (size_t i = 0; i < 16; ++i) state[i] ^= key[i];
}

void subShift(uint8_t state[16]) {
  uint8_t copy[16];
  std::memcpy(copy, state, 16);
  constexpr uint8_t map[16] = {0,5,10,15,4,9,14,3,8,13,2,7,12,1,6,11};
  for (size_t i = 0; i < 16; ++i) state[i] = kSbox[copy[map[i]]];
}

void mixColumns(uint8_t state[16]) {
  for (size_t c = 0; c < 4; ++c) {
    uint8_t* a = state + c * 4;
    const uint8_t t = static_cast<uint8_t>(a[0] ^ a[1] ^ a[2] ^ a[3]);
    const uint8_t first = a[0];
    a[0] ^= static_cast<uint8_t>(t ^ xtime(a[0] ^ a[1]));
    a[1] ^= static_cast<uint8_t>(t ^ xtime(a[1] ^ a[2]));
    a[2] ^= static_cast<uint8_t>(t ^ xtime(a[2] ^ a[3]));
    a[3] ^= static_cast<uint8_t>(t ^ xtime(a[3] ^ first));
  }
}

void xorBlock(uint8_t target[16], const uint8_t value[16]) {
  for (size_t i = 0; i < 16; ++i) target[i] ^= value[i];
}

void shiftSubkey(const uint8_t input[16], uint8_t output[16]) {
  uint8_t carry = 0;
  for (int i = 15; i >= 0; --i) {
    const uint8_t next = static_cast<uint8_t>(input[i] >> 7);
    output[i] = static_cast<uint8_t>((input[i] << 1) | carry);
    carry = next;
  }
  if ((input[0] & 0x80) != 0) output[15] ^= 0x87;
}

void cbcMacBlock(const uint8_t key[16], uint8_t state[16],
                 const uint8_t block[16]) {
  xorBlock(state, block);
  uint8_t encrypted[16];
  aes128EncryptBlock(key, state, encrypted);
  std::memcpy(state, encrypted, 16);
}

}  // namespace

void aes128EncryptBlock(const uint8_t key[16], const uint8_t input[16],
                        uint8_t output[16]) {
  uint8_t roundKey[176];
  uint8_t state[16];
  expandKey(key, roundKey);
  std::memcpy(state, input, 16);
  addRoundKey(state, roundKey);
  for (size_t round = 1; round < 10; ++round) {
    subShift(state);
    mixColumns(state);
    addRoundKey(state, roundKey + round * 16);
  }
  subShift(state);
  addRoundKey(state, roundKey + 160);
  std::memcpy(output, state, 16);
}

void aesCmac(const uint8_t key[16], const uint8_t* message, size_t length,
             uint8_t output[16]) {
  uint8_t zero[16] = {};
  uint8_t l[16], k1[16], k2[16];
  aes128EncryptBlock(key, zero, l);
  shiftSubkey(l, k1);
  shiftSubkey(k1, k2);
  const size_t blocks = length == 0 ? 1 : (length + 15) / 16;
  const bool complete = length != 0 && length % 16 == 0;
  uint8_t state[16] = {};
  for (size_t i = 0; i + 1 < blocks; ++i) {
    uint8_t block[16];
    std::memcpy(block, message + i * 16, 16);
    cbcMacBlock(key, state, block);
  }
  uint8_t last[16] = {};
  const size_t offset = (blocks - 1) * 16;
  const size_t remaining = length > offset ? length - offset : 0;
  if (remaining > 0) std::memcpy(last, message + offset, remaining);
  if (complete) {
    xorBlock(last, k1);
  } else {
    last[remaining] = 0x80;
    xorBlock(last, k2);
  }
  cbcMacBlock(key, state, last);
  std::memcpy(output, state, 16);
}

void meshS1(const uint8_t* message, size_t length, uint8_t output[16]) {
  const uint8_t zero[16] = {};
  aesCmac(zero, message, length, output);
}

void meshK1(const uint8_t* secret, size_t secretLength,
            const uint8_t salt[16], const uint8_t* info,
            size_t infoLength, uint8_t output[16]) {
  uint8_t t[16];
  aesCmac(salt, secret, secretLength, t);
  aesCmac(t, info, infoLength, output);
}

void meshK2(const uint8_t networkKey[16], NetworkKeys& output) {
  uint8_t salt[16], t[16], t1[16], t2[16], t3[16];
  meshS1(reinterpret_cast<const uint8_t*>("smk2"), 4, salt);
  aesCmac(salt, networkKey, 16, t);
  const uint8_t p1[] = {0x00, 0x01};
  aesCmac(t, p1, sizeof(p1), t1);
  uint8_t p2[18];
  std::memcpy(p2, t1, 16); p2[16] = 0; p2[17] = 2;
  aesCmac(t, p2, sizeof(p2), t2);
  uint8_t p3[18];
  std::memcpy(p3, t2, 16); p3[16] = 0; p3[17] = 3;
  aesCmac(t, p3, sizeof(p3), t3);
  output.nid = static_cast<uint8_t>(t1[15] & 0x7f);
  std::memcpy(output.encryption, t2, 16);
  std::memcpy(output.privacy, t3, 16);
}

uint8_t meshK4(const uint8_t applicationKey[16]) {
  uint8_t salt[16], t[16], result[16];
  meshS1(reinterpret_cast<const uint8_t*>("smk4"), 4, salt);
  aesCmac(salt, applicationKey, 16, t);
  const uint8_t id6[] = {'i','d','6',0x01};
  aesCmac(t, id6, sizeof(id6), result);
  return static_cast<uint8_t>(result[15] & 0x3f);
}

bool aesCcmEncrypt(const uint8_t key[16], const uint8_t* nonce,
                   size_t nonceLength, const uint8_t* plaintext,
                   size_t plaintextLength, size_t tagLength,
                   uint8_t* ciphertext, uint8_t* tag) {
  const size_t lengthBytes = 15 - nonceLength;
  if (nonce == nullptr || plaintext == nullptr || ciphertext == nullptr ||
      tag == nullptr || lengthBytes < 2 || lengthBytes > 8 || tagLength < 4 ||
      tagLength > 16 || (tagLength & 1) != 0 || plaintextLength > 0xffff) {
    return false;
  }
  uint8_t mac[16] = {};
  uint8_t block[16] = {};
  block[0] = static_cast<uint8_t>(((tagLength - 2) / 2) << 3 |
                                  (lengthBytes - 1));
  std::memcpy(block + 1, nonce, nonceLength);
  for (size_t i = 0; i < lengthBytes; ++i) {
    block[15 - i] = static_cast<uint8_t>(plaintextLength >> (8 * i));
  }
  cbcMacBlock(key, mac, block);
  for (size_t offset = 0; offset < plaintextLength; offset += 16) {
    std::memset(block, 0, 16);
    const size_t count = plaintextLength - offset < 16 ? plaintextLength - offset : 16;
    std::memcpy(block, plaintext + offset, count);
    cbcMacBlock(key, mac, block);
  }
  uint8_t counter[16] = {};
  counter[0] = static_cast<uint8_t>(lengthBytes - 1);
  std::memcpy(counter + 1, nonce, nonceLength);
  uint8_t stream[16];
  aes128EncryptBlock(key, counter, stream);
  for (size_t i = 0; i < tagLength; ++i) tag[i] = static_cast<uint8_t>(mac[i] ^ stream[i]);
  for (size_t offset = 0, index = 1; offset < plaintextLength; offset += 16, ++index) {
    for (size_t i = 0; i < lengthBytes; ++i) {
      counter[15 - i] = static_cast<uint8_t>(index >> (8 * i));
    }
    aes128EncryptBlock(key, counter, stream);
    const size_t count = plaintextLength - offset < 16 ? plaintextLength - offset : 16;
    for (size_t i = 0; i < count; ++i) ciphertext[offset + i] = static_cast<uint8_t>(plaintext[offset + i] ^ stream[i]);
  }
  return true;
}

bool aesCcmDecrypt(const uint8_t key[16], const uint8_t* nonce,
                   size_t nonceLength, const uint8_t* ciphertext,
                   size_t ciphertextLength, const uint8_t* tag,
                   size_t tagLength, uint8_t* plaintext) {
  const size_t lengthBytes = 15 - nonceLength;
  if (key == nullptr || nonce == nullptr || ciphertext == nullptr ||
      tag == nullptr || plaintext == nullptr || lengthBytes < 2 ||
      lengthBytes > 8 || tagLength < 4 || tagLength > 16 ||
      (tagLength & 1) != 0 || ciphertextLength > 0xffff) {
    return false;
  }

  uint8_t counter[16] = {};
  counter[0] = static_cast<uint8_t>(lengthBytes - 1);
  std::memcpy(counter + 1, nonce, nonceLength);
  uint8_t stream[16];
  for (size_t offset = 0, index = 1; offset < ciphertextLength;
       offset += 16, ++index) {
    for (size_t i = 0; i < lengthBytes; ++i) {
      counter[15 - i] = static_cast<uint8_t>(index >> (8 * i));
    }
    aes128EncryptBlock(key, counter, stream);
    const size_t count = ciphertextLength - offset < 16
                             ? ciphertextLength - offset
                             : 16;
    for (size_t i = 0; i < count; ++i) {
      plaintext[offset + i] =
          static_cast<uint8_t>(ciphertext[offset + i] ^ stream[i]);
    }
  }

  uint8_t mac[16] = {};
  uint8_t block[16] = {};
  block[0] = static_cast<uint8_t>(((tagLength - 2) / 2) << 3 |
                                  (lengthBytes - 1));
  std::memcpy(block + 1, nonce, nonceLength);
  for (size_t i = 0; i < lengthBytes; ++i) {
    block[15 - i] = static_cast<uint8_t>(ciphertextLength >> (8 * i));
  }
  cbcMacBlock(key, mac, block);
  for (size_t offset = 0; offset < ciphertextLength; offset += 16) {
    std::memset(block, 0, sizeof(block));
    const size_t count = ciphertextLength - offset < 16
                             ? ciphertextLength - offset
                             : 16;
    std::memcpy(block, plaintext + offset, count);
    cbcMacBlock(key, mac, block);
  }

  std::memset(counter, 0, sizeof(counter));
  counter[0] = static_cast<uint8_t>(lengthBytes - 1);
  std::memcpy(counter + 1, nonce, nonceLength);
  aes128EncryptBlock(key, counter, stream);
  uint8_t difference = 0;
  for (size_t i = 0; i < tagLength; ++i) {
    difference |= static_cast<uint8_t>(tag[i] ^ mac[i] ^ stream[i]);
  }
  if (difference != 0) {
    std::memset(plaintext, 0, ciphertextLength);
    return false;
  }
  return true;
}

}  // namespace aputure_light
