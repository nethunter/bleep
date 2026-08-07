#include "core/mesh/pb_gatt_provisioner.h"

#include <esp_random.h>
#include <mbedtls/ecdh.h>

#include <cstring>

#include "devices/amaran_light/crypto.h"

namespace studio::mesh {
namespace {

int randomCallback(void*, unsigned char* output, size_t length) {
  esp_fill_random(output, length);
  return 0;
}

}  // namespace

PbGattProvisioner::~PbGattProvisioner() { cancel(); }

bool PbGattProvisioner::begin(const uint8_t networkKey[16], uint32_t ivIndex,
                              uint16_t unicastAddress,
                              ProvisioningSender& sender) {
  cancel();
  if (networkKey == nullptr || unicastAddress == 0 || unicastAddress > 0x7fff)
    return false;
  std::memcpy(networkKey_, networkKey, sizeof(networkKey_));
  ivIndex_ = ivIndex;
  unicastAddress_ = unicastAddress;
  sender_ = &sender;
  active_ = true;
  step_ = 1;
  const uint8_t invite[] = {0x00, 0x00};
  if (sender_->sendProvisioningPdu(invite, sizeof(invite))) return true;
  cancel();
  return false;
}

bool PbGattProvisioner::handle(const uint8_t* pdu, size_t length) {
  if (!active_ || pdu == nullptr || length == 0 || pdu[0] == 0x09) return false;
  bool ok = false;
  if (pdu[0] == 0x01 && step_ == 1)
    ok = handleCapabilities(pdu, length);
  else if (pdu[0] == 0x03 && step_ == 2)
    ok = handleDevicePublicKey(pdu, length);
  else if (pdu[0] == 0x05 && step_ == 3)
    ok = handleDeviceConfirmation(pdu, length);
  else if (pdu[0] == 0x06 && step_ == 4)
    ok = handleDeviceRandom(pdu, length);
  else if (pdu[0] == 0x08 && step_ == 5) {
    complete_ = true;
    active_ = false;
    closeKey();
    return true;
  }
  if (!ok) cancel();
  return ok;
}

void PbGattProvisioner::cancel() {
  closeKey();
  sender_ = nullptr;
  active_ = false;
  complete_ = false;
  step_ = 0;
}

void PbGattProvisioner::closeKey() {
  if (!keyActive_) return;
  mbedtls_ecp_point_free(&publicKey_);
  mbedtls_mpi_free(&privateKey_);
  mbedtls_ecp_group_free(&group_);
  keyActive_ = false;
}

bool PbGattProvisioner::handleCapabilities(const uint8_t* pdu, size_t length) {
  // Static OOB may be advertised as available while no-OOB remains selectable.
  if (length != 12 || pdu[2] != 0 || (pdu[3] & 1) == 0 || pdu[4] != 0 ||
      pdu[6] != 0 || pdu[9] != 0)
    return false;
  elementCount_ = pdu[1] == 0 ? 1 : pdu[1];
  std::memcpy(capabilities_, pdu, sizeof(capabilities_));
  const uint8_t start[] = {0x02, 0, 0, 0, 0, 0};
  if (!sender_->sendProvisioningPdu(start, sizeof(start))) return false;

  mbedtls_ecp_group_init(&group_);
  mbedtls_mpi_init(&privateKey_);
  mbedtls_ecp_point_init(&publicKey_);
  keyActive_ = true;
  if (mbedtls_ecp_group_load(&group_, MBEDTLS_ECP_DP_SECP256R1) != 0 ||
      mbedtls_ecp_gen_keypair(&group_, &privateKey_, &publicKey_,
                              randomCallback, nullptr) != 0)
    return false;
  uint8_t encoded[65];
  size_t encodedLength = 0;
  if (mbedtls_ecp_point_write_binary(
          &group_, &publicKey_, MBEDTLS_ECP_PF_UNCOMPRESSED, &encodedLength,
          encoded, sizeof(encoded)) != 0 ||
      encodedLength != sizeof(encoded))
    return false;
  std::memcpy(localPublic_, encoded + 1, sizeof(localPublic_));
  uint8_t publicPdu[65] = {0x03};
  std::memcpy(publicPdu + 1, localPublic_, sizeof(localPublic_));
  step_ = 2;
  return sender_->sendProvisioningPdu(publicPdu, sizeof(publicPdu));
}

bool PbGattProvisioner::handleDevicePublicKey(const uint8_t* pdu,
                                               size_t length) {
  if (length != 65 || !keyActive_) return false;
  std::memcpy(remotePublic_, pdu + 1, sizeof(remotePublic_));
  uint8_t encoded[65] = {0x04};
  std::memcpy(encoded + 1, remotePublic_, sizeof(remotePublic_));
  mbedtls_ecp_point remote;
  mbedtls_ecp_point_init(&remote);
  mbedtls_mpi secret;
  mbedtls_mpi_init(&secret);
  const bool ok =
      mbedtls_ecp_point_read_binary(&group_, &remote, encoded,
                                    sizeof(encoded)) == 0 &&
      mbedtls_ecdh_compute_shared(&group_, &secret, &remote, &privateKey_,
                                  randomCallback, nullptr) == 0 &&
      mbedtls_mpi_write_binary(&secret, ecdhSecret_, sizeof(ecdhSecret_)) == 0;
  mbedtls_mpi_free(&secret);
  mbedtls_ecp_point_free(&remote);
  if (!ok) return false;

  uint8_t inputs[145];
  size_t offset = 0;
  inputs[offset++] = 0;
  std::memcpy(inputs + offset, capabilities_ + 1, 11);
  offset += 11;
  std::memset(inputs + offset, 0, 5);
  offset += 5;
  std::memcpy(inputs + offset, localPublic_, 64);
  offset += 64;
  std::memcpy(inputs + offset, remotePublic_, 64);
  amaran_light::meshS1(inputs, sizeof(inputs), confirmationSalt_);
  const uint8_t prck[] = {'p', 'r', 'c', 'k'};
  amaran_light::meshK1(ecdhSecret_, sizeof(ecdhSecret_), confirmationSalt_,
                       prck, sizeof(prck), confirmationKey_);
  esp_fill_random(localRandom_, sizeof(localRandom_));
  uint8_t material[32];
  std::memcpy(material, localRandom_, 16);
  std::memset(material + 16, 0, 16);
  uint8_t confirmation[16];
  amaran_light::aesCmac(confirmationKey_, material, sizeof(material),
                        confirmation);
  uint8_t confirmationPdu[17] = {0x05};
  std::memcpy(confirmationPdu + 1, confirmation, 16);
  step_ = 3;
  return sender_->sendProvisioningPdu(confirmationPdu,
                                      sizeof(confirmationPdu));
}

bool PbGattProvisioner::handleDeviceConfirmation(const uint8_t* pdu,
                                                  size_t length) {
  if (length != 17) return false;
  std::memcpy(remoteConfirmation_, pdu + 1, sizeof(remoteConfirmation_));
  uint8_t randomPdu[17] = {0x06};
  std::memcpy(randomPdu + 1, localRandom_, sizeof(localRandom_));
  step_ = 4;
  return sender_->sendProvisioningPdu(randomPdu, sizeof(randomPdu));
}

bool PbGattProvisioner::handleDeviceRandom(const uint8_t* pdu,
                                            size_t length) {
  if (length != 17) return false;
  uint8_t material[32];
  std::memcpy(material, pdu + 1, 16);
  std::memset(material + 16, 0, 16);
  uint8_t expected[16];
  amaran_light::aesCmac(confirmationKey_, material, sizeof(material), expected);
  if (std::memcmp(expected, remoteConfirmation_, sizeof(expected)) != 0)
    return false;

  uint8_t saltMaterial[48];
  std::memcpy(saltMaterial, confirmationSalt_, 16);
  std::memcpy(saltMaterial + 16, localRandom_, 16);
  std::memcpy(saltMaterial + 32, pdu + 1, 16);
  uint8_t provisioningSalt[16], sessionKey[16], nonceFull[16];
  amaran_light::meshS1(saltMaterial, sizeof(saltMaterial), provisioningSalt);
  const uint8_t prsk[] = {'p', 'r', 's', 'k'};
  const uint8_t prsn[] = {'p', 'r', 's', 'n'};
  const uint8_t prdk[] = {'p', 'r', 'd', 'k'};
  amaran_light::meshK1(ecdhSecret_, 32, provisioningSalt, prsk, 4, sessionKey);
  amaran_light::meshK1(ecdhSecret_, 32, provisioningSalt, prsn, 4, nonceFull);
  amaran_light::meshK1(ecdhSecret_, 32, provisioningSalt, prdk, 4, deviceKey_);

  uint8_t provisioningData[25];
  std::memcpy(provisioningData, networkKey_, 16);
  provisioningData[16] = provisioningData[17] = provisioningData[18] = 0;
  provisioningData[19] = static_cast<uint8_t>(ivIndex_ >> 24);
  provisioningData[20] = static_cast<uint8_t>(ivIndex_ >> 16);
  provisioningData[21] = static_cast<uint8_t>(ivIndex_ >> 8);
  provisioningData[22] = static_cast<uint8_t>(ivIndex_);
  provisioningData[23] = static_cast<uint8_t>(unicastAddress_ >> 8);
  provisioningData[24] = static_cast<uint8_t>(unicastAddress_);
  uint8_t encrypted[25], tag[8];
  if (!amaran_light::aesCcmEncrypt(sessionKey, nonceFull + 3, 13,
                                   provisioningData, sizeof(provisioningData),
                                   sizeof(tag), encrypted, tag))
    return false;
  uint8_t dataPdu[34] = {0x07};
  std::memcpy(dataPdu + 1, encrypted, sizeof(encrypted));
  std::memcpy(dataPdu + 26, tag, sizeof(tag));
  step_ = 5;
  return sender_->sendProvisioningPdu(dataPdu, sizeof(dataPdu));
}

}  // namespace studio::mesh
