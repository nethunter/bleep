#pragma once

#include <cstddef>
#include <cstdint>

namespace aputure_light {

void aes128EncryptBlock(const uint8_t key[16], const uint8_t input[16],
                        uint8_t output[16]);
void aesCmac(const uint8_t key[16], const uint8_t* message, size_t length,
             uint8_t output[16]);
void meshS1(const uint8_t* message, size_t length, uint8_t output[16]);
void meshK1(const uint8_t* secret, size_t secretLength,
            const uint8_t salt[16], const uint8_t* info,
            size_t infoLength, uint8_t output[16]);

struct NetworkKeys {
  uint8_t nid = 0;
  uint8_t encryption[16] = {};
  uint8_t privacy[16] = {};
};

void meshK2(const uint8_t networkKey[16], NetworkKeys& output);
void meshK3(const uint8_t networkKey[16], uint8_t output[8]);
uint8_t meshK4(const uint8_t applicationKey[16]);

bool aesCcmEncrypt(const uint8_t key[16], const uint8_t* nonce,
                   size_t nonceLength, const uint8_t* plaintext,
                   size_t plaintextLength, size_t tagLength,
                   uint8_t* ciphertext, uint8_t* tag);
bool aesCcmDecrypt(const uint8_t key[16], const uint8_t* nonce,
                   size_t nonceLength, const uint8_t* ciphertext,
                   size_t ciphertextLength, const uint8_t* tag,
                   size_t tagLength, uint8_t* plaintext);

}  // namespace aputure_light
