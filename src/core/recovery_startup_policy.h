#pragma once

#include "core/recovery_journal.h"

namespace studio {

inline bool shouldAutoBootMain(RecoveryJournalLoadStatus status,
                               const RecoveryRecord& record,
                               bool mainValid) {
  if (!mainValid) return false;
  return status == RecoveryJournalLoadStatus::Empty ||
      (status == RecoveryJournalLoadStatus::Loaded &&
       record.operation == RecoveryOperation::None);
}

}  // namespace studio
