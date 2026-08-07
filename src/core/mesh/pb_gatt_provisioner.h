#pragma once

#include <cstddef>
#include <cstdint>

#include <mbedtls/ecp.h>

namespace studio::mesh {

class ProvisioningSender {
 public:
  virtual ~ProvisioningSender() = default;
  virtual bool sendProvisioningPdu(const uint8_t* pdu, size_t length) = 0;
};

class PbGattProvisioner {
 public:
  PbGattProvisioner() = default;
  ~PbGattProvisioner();

  bool begin(const uint8_t networkKey[16], uint32_t ivIndex,
             uint16_t unicastAddress, ProvisioningSender& sender);
  bool handle(const uint8_t* pdu, size_t length);
  void cancel();

  bool active() const { return active_; }
  bool complete() const { return complete_; }
  uint8_t elementCount() const { return elementCount_; }
  const uint8_t* deviceKey() const { return deviceKey_; }

 private:
  bool handleCapabilities(const uint8_t* pdu, size_t length);
  bool handleDevicePublicKey(const uint8_t* pdu, size_t length);
  bool handleDeviceConfirmation(const uint8_t* pdu, size_t length);
  bool handleDeviceRandom(const uint8_t* pdu, size_t length);
  void closeKey();

  ProvisioningSender* sender_ = nullptr;
  bool active_ = false;
  bool complete_ = false;
  bool keyActive_ = false;
  uint8_t step_ = 0;
  uint8_t elementCount_ = 1;
  uint8_t networkKey_[16] = {};
  uint32_t ivIndex_ = 0;
  uint16_t unicastAddress_ = 0;
  uint8_t capabilities_[12] = {};
  uint8_t localPublic_[64] = {};
  uint8_t remotePublic_[64] = {};
  uint8_t ecdhSecret_[32] = {};
  uint8_t confirmationSalt_[16] = {};
  uint8_t confirmationKey_[16] = {};
  uint8_t localRandom_[16] = {};
  uint8_t remoteConfirmation_[16] = {};
  uint8_t deviceKey_[16] = {};
  mbedtls_ecp_group group_;
  mbedtls_mpi privateKey_;
  mbedtls_ecp_point publicKey_;
};

}  // namespace studio::mesh
