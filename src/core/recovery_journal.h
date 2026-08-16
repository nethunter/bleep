#pragma once

#include <cstddef>
#include <cstdint>

namespace studio {

constexpr size_t kRecoveryManifestCapacity = 1536;
constexpr size_t kRecoverySignatureCapacity = 80;

enum class RecoveryOperation : uint8_t {
  None = 0,
  InstallRequested = 1,
  FactoryResetRequested = 2,
  ImageVerifiedResetPending = 3,
  ResetComplete = 4,
  RecoveryModeRequested = 5,
};

enum class RecoveryJournalLoadStatus : uint8_t {
  Loaded,
  Empty,
  Corrupt,
  ReadFailed,
};

struct RecoveryRecord {
  RecoveryOperation operation = RecoveryOperation::None;
  uint8_t channel = 0;
  uint64_t releaseSequence = 0;
  uint16_t manifestLength = 0;
  uint16_t signatureLength = 0;
  char manifest[kRecoveryManifestCapacity] = {};
  uint8_t signature[kRecoverySignatureCapacity] = {};
};

class IRecoveryJournalBackend {
 public:
  virtual ~IRecoveryJournalBackend() = default;
  virtual bool readSlot(uint8_t slot, uint8_t* destination, size_t capacity) = 0;
  virtual bool writeSlot(uint8_t slot, const uint8_t* source, size_t length) = 0;
  virtual bool erase() = 0;
};

class RecoveryJournal {
 public:
  explicit RecoveryJournal(IRecoveryJournalBackend& backend) : backend_(backend) {}

  RecoveryJournalLoadStatus loadStatus(RecoveryRecord& record);
  bool load(RecoveryRecord& record);
  bool save(const RecoveryRecord& record);
  bool clear();

 private:
  IRecoveryJournalBackend& backend_;
  uint32_t generation_ = 0;
  uint8_t activeSlot_ = 1;
};

}  // namespace studio
