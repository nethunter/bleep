#include "core/update_bundle.h"

class MemoryRecoveryJournalBackend : public studio::IRecoveryJournalBackend {
 public:
  MemoryRecoveryJournalBackend() { std::memset(slots_, 0xFF, sizeof(slots_)); }

  bool readSlot(uint8_t slot, uint8_t* destination, size_t capacity) override {
    if (slot > 1 || capacity > sizeof(slots_[slot])) return false;
    std::memcpy(destination, slots_[slot], capacity);
    return true;
  }

  bool writeSlot(uint8_t slot, const uint8_t* source, size_t length) override {
    if (failWrites || slot > 1 || length > sizeof(slots_[slot])) return false;
    std::memset(slots_[slot], 0xFF, sizeof(slots_[slot]));
    std::memcpy(slots_[slot], source, length);
    lastWritten = slot;
    return true;
  }

  bool erase() override {
    if (failErase) return false;
    std::memset(slots_, 0xFF, sizeof(slots_));
    return true;
  }

  void corruptLastWrite() { slots_[lastWritten][24] ^= 0x80; }
  void corruptEmptyJournal() { slots_[0][0] = 0; }

  bool failWrites = false;
  bool failErase = false;
  uint8_t lastWritten = 0;

 private:
  uint8_t slots_[2][4096];
};

void test_firmware_update_policy_checks_five_seconds_after_idle_home() {
  studio::FirmwareUpdatePolicy policy;
  policy.begin(1000);
  TEST_ASSERT_FALSE(policy.shouldCheck(1000, true, true));
  TEST_ASSERT_FALSE(policy.shouldCheck(
      1000 + studio::FirmwareUpdatePolicy::kStartupDelayMs - 1, true, true));
  TEST_ASSERT_FALSE(policy.shouldCheck(
      1000 + studio::FirmwareUpdatePolicy::kStartupDelayMs, false, true));
  TEST_ASSERT_FALSE(policy.shouldCheck(
      1000 + studio::FirmwareUpdatePolicy::kStartupDelayMs, true, false));
  TEST_ASSERT_TRUE(policy.shouldCheck(
      1000 + studio::FirmwareUpdatePolicy::kStartupDelayMs, true, true));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::FirmwareUpdatePolicy::ScheduleState::StartupChecking),
      static_cast<int>(policy.scheduleState()));
}

void test_firmware_update_policy_defers_startup_for_foreground_then_resumes() {
  studio::FirmwareUpdatePolicy policy;
  policy.begin(0);
  policy.deferStartup();
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::FirmwareUpdatePolicy::ScheduleState::DeferredByForeground),
      static_cast<int>(policy.scheduleState()));
  TEST_ASSERT_FALSE(policy.shouldCheck(
      studio::FirmwareUpdatePolicy::kStartupDelayMs, false, true));
  TEST_ASSERT_TRUE(policy.shouldCheck(
      studio::FirmwareUpdatePolicy::kStartupDelayMs, true, true));
}

void test_firmware_update_policy_immediate_daily_and_retry_scheduling() {
  studio::FirmwareUpdatePolicy policy;
  policy.begin(0);
  policy.requestImmediate();
  TEST_ASSERT_FALSE(policy.shouldCheck(1, false, true));
  TEST_ASSERT_TRUE(policy.shouldCheck(1, true, true));
  policy.checked(1, true);
  TEST_ASSERT_FALSE(policy.shouldCheck(
      1 + studio::FirmwareUpdatePolicy::kCheckIntervalMs - 1, true, true));
  TEST_ASSERT_TRUE(policy.shouldCheck(
      1 + studio::FirmwareUpdatePolicy::kCheckIntervalMs, true, true));

  const uint32_t failedAt = 1 + studio::FirmwareUpdatePolicy::kCheckIntervalMs;
  policy.checked(failedAt, false);
  TEST_ASSERT_EQUAL_UINT8(1, policy.failureCount());
  TEST_ASSERT_FALSE(policy.shouldCheck(failedAt + 60U * 60U * 1000U - 1,
                                       true, true));
  TEST_ASSERT_TRUE(policy.shouldCheck(failedAt + 60U * 60U * 1000U,
                                      true, true));
}

void test_recovery_journal_alternates_recovers_and_clears() {
  MemoryRecoveryJournalBackend backend;
  studio::RecoveryJournal journal(backend);
  studio::RecoveryRecord loaded;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RecoveryJournalLoadStatus::Empty),
      static_cast<int>(journal.loadStatus(loaded)));
  TEST_ASSERT_FALSE(journal.load(loaded));

  studio::RecoveryRecord first;
  first.operation = studio::RecoveryOperation::InstallRequested;
  first.channel = 1;
  first.releaseSequence = 42;
  const char manifest[] = "{\"release_sequence\":42}";
  std::memcpy(first.manifest, manifest, sizeof(manifest));
  first.manifestLength = sizeof(manifest) - 1;
  first.signature[0] = 0x30;
  first.signatureLength = 1;
  TEST_ASSERT_TRUE(journal.save(first));

  studio::RecoveryRecord second = first;
  second.operation = studio::RecoveryOperation::FactoryResetRequested;
  second.releaseSequence = 43;
  TEST_ASSERT_TRUE(journal.save(second));
  TEST_ASSERT_TRUE(journal.load(loaded));
  TEST_ASSERT_EQUAL_UINT64(43, loaded.releaseSequence);

  backend.corruptLastWrite();
  TEST_ASSERT_TRUE(journal.load(loaded));
  TEST_ASSERT_EQUAL_UINT64(42, loaded.releaseSequence);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::RecoveryOperation::InstallRequested),
                        static_cast<int>(loaded.operation));
  TEST_ASSERT_TRUE(journal.clear());
  TEST_ASSERT_FALSE(journal.load(loaded));

  studio::RecoveryRecord recoveryMode;
  recoveryMode.operation = studio::RecoveryOperation::RecoveryModeRequested;
  TEST_ASSERT_TRUE(journal.save(recoveryMode));
  TEST_ASSERT_TRUE(journal.load(loaded));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RecoveryOperation::RecoveryModeRequested),
      static_cast<int>(loaded.operation));
}

void test_recovery_install_updates_recovery_before_booting_it() {
  using studio::RecoveryInstallStep;
  using studio::RecoveryOperation;

  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(RecoveryInstallStep::Reject),
      static_cast<int>(studio::recoveryInstallStep(
          RecoveryOperation::InstallRequested, 43, 42, false, false)));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(RecoveryInstallStep::WriteRecovery),
      static_cast<int>(studio::recoveryInstallStep(
          RecoveryOperation::InstallRequested, 43, 42, true, false)));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(RecoveryInstallStep::BootRecovery),
      static_cast<int>(studio::recoveryInstallStep(
          RecoveryOperation::InstallRequested, 43, 42, true, true)));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(RecoveryInstallStep::Finish),
      static_cast<int>(studio::recoveryInstallStep(
          RecoveryOperation::InstallRequested, 43, 43, true, true)));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(RecoveryInstallStep::Finish),
      static_cast<int>(studio::recoveryInstallStep(
          RecoveryOperation::ResetComplete, 43, 42, true, true)));
}

void test_early_recovery_update_preserves_new_main_health_gate() {
  TEST_ASSERT_TRUE(studio::canRunEarlyRecoveryUpdate(true, false));
  TEST_ASSERT_FALSE(studio::canRunEarlyRecoveryUpdate(true, true));
  TEST_ASSERT_FALSE(studio::canRunEarlyRecoveryUpdate(false, false));
}

void test_recovery_startup_auto_boots_only_clean_valid_main() {
  MemoryRecoveryJournalBackend backend;
  studio::RecoveryJournal journal(backend);
  studio::RecoveryRecord record;

  studio::RecoveryJournalLoadStatus status = journal.loadStatus(record);
  TEST_ASSERT_TRUE(studio::shouldAutoBootMain(status, record, true));
  TEST_ASSERT_FALSE(studio::shouldAutoBootMain(status, record, false));

  backend.corruptEmptyJournal();
  status = journal.loadStatus(record);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RecoveryJournalLoadStatus::Corrupt),
      static_cast<int>(status));
  TEST_ASSERT_FALSE(studio::shouldAutoBootMain(status, record, true));

  MemoryRecoveryJournalBackend requestedBackend;
  studio::RecoveryJournal requestedJournal(requestedBackend);
  record.operation = studio::RecoveryOperation::RecoveryModeRequested;
  TEST_ASSERT_TRUE(requestedJournal.save(record));
  status = requestedJournal.loadStatus(record);
  TEST_ASSERT_FALSE(studio::shouldAutoBootMain(status, record, true));
}

void test_recovery_touch_requires_a_settled_release_after_reboot() {
  studio::RecoveryTouchGate gate;
  TEST_ASSERT_FALSE(gate.update(false, 0));
  TEST_ASSERT_FALSE(gate.update(false, 1499));
  TEST_ASSERT_FALSE(gate.update(true, 1500));
  TEST_ASSERT_FALSE(gate.update(true, 5000));
  TEST_ASSERT_FALSE(gate.update(false, 5010));
  TEST_ASSERT_FALSE(gate.update(false, 5309));
  TEST_ASSERT_TRUE(gate.update(false, 5310));
  TEST_ASSERT_TRUE(gate.armed());
  TEST_ASSERT_TRUE(gate.update(true, 5220));
}

void test_update_bundle_splits_manifest_and_base64_signature() {
  // Manifest bytes are exactly what the detached signature covers, including
  // the trailing newline; the second line carries the signature in base64.
  const char bundle[] =
      "{\"channel\":\"stable\",\"schema\":1}\n"
      "MEUCIQDx8vP09fb3+Pn6+/z9/v8AAQIDBAUGBwgJCgsMDQ4PEBESExQVFhcYGRobHB0eHwIgEBESExQVFhcYGRobHB0eHyA=\n";
  size_t manifestLength = 0;
  uint8_t signature[80] = {};
  size_t signatureLength = 0;
  TEST_ASSERT_TRUE(studio::splitUpdateBundle(bundle, sizeof(bundle) - 1,
                                             manifestLength, signature,
                                             sizeof(signature),
                                             signatureLength));
  TEST_ASSERT_EQUAL_UINT32(std::strlen("{\"channel\":\"stable\",\"schema\":1}\n"),
                           manifestLength);
  TEST_ASSERT_EQUAL_UINT32(71, signatureLength);
  TEST_ASSERT_EQUAL_HEX8(0x30, signature[0]);
  TEST_ASSERT_EQUAL_HEX8(0x45, signature[1]);
  TEST_ASSERT_EQUAL_HEX8(0x20, signature[70]);

  // Without the trailing newline the split still works.
  TEST_ASSERT_TRUE(studio::splitUpdateBundle(bundle, sizeof(bundle) - 2,
                                             manifestLength, signature,
                                             sizeof(signature),
                                             signatureLength));
  TEST_ASSERT_EQUAL_UINT32(71, signatureLength);

  // Malformed inputs: no signature line, a third line, bad base64, and a
  // signature larger than the buffer are all rejected.
  const char onlyManifest[] = "{\"schema\":1}\n";
  TEST_ASSERT_FALSE(studio::splitUpdateBundle(
      onlyManifest, sizeof(onlyManifest) - 1, manifestLength, signature,
      sizeof(signature), signatureLength));
  const char threeLines[] = "{\"schema\":1}\nMEUC\nextra\n";
  TEST_ASSERT_FALSE(studio::splitUpdateBundle(
      threeLines, sizeof(threeLines) - 1, manifestLength, signature,
      sizeof(signature), signatureLength));
  const char badBase64[] = "{\"schema\":1}\nME*C\n";
  TEST_ASSERT_FALSE(studio::splitUpdateBundle(
      badBase64, sizeof(badBase64) - 1, manifestLength, signature,
      sizeof(signature), signatureLength));
  uint8_t tiny[8] = {};
  TEST_ASSERT_FALSE(studio::splitUpdateBundle(bundle, sizeof(bundle) - 1,
                                              manifestLength, tiny,
                                              sizeof(tiny), signatureLength));
}
