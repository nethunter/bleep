#pragma once

#include <cstdint>

#include "core/recovery_journal.h"

namespace studio {

enum class RecoveryInstallStep : uint8_t {
  Reject,
  WriteRecovery,
  BootRecovery,
  Finish,
};

inline bool canRunEarlyRecoveryUpdate(bool runningMain,
                                      bool pendingBootValidation) {
  return runningMain && !pendingBootValidation;
}

inline RecoveryInstallStep recoveryInstallStep(
    RecoveryOperation operation, uint64_t targetReleaseSequence,
    uint64_t runningReleaseSequence, bool recoveryMetadataValid,
    bool recoveryCurrent) {
  if (!recoveryMetadataValid) return RecoveryInstallStep::Reject;
  if (!recoveryCurrent) return RecoveryInstallStep::WriteRecovery;
  if (operation == RecoveryOperation::InstallRequested &&
      targetReleaseSequence > runningReleaseSequence) {
    return RecoveryInstallStep::BootRecovery;
  }
  return RecoveryInstallStep::Finish;
}

}  // namespace studio
