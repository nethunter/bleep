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
