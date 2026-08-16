#pragma once

#include "core/recovery_journal.h"

namespace studio {

class PartitionRecoveryJournalBackend final : public IRecoveryJournalBackend {
 public:
  bool readSlot(uint8_t slot, uint8_t* destination, size_t capacity) override;
  bool writeSlot(uint8_t slot, const uint8_t* source, size_t length) override;
  bool erase() override;
};

}  // namespace studio
