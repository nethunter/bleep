void test_crc_and_known_frame_fixture() {
  const uint8_t standard[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
  TEST_ASSERT_EQUAL_HEX32(0xCBF43926u, crc32Ieee(standard, sizeof(standard)));

  const uint8_t expected[] = {
      0xAA, 0xBB, 0x06, 0x15, 0x00, 0x01, 0x42, 0xAF, 0x6C, 0xB4, 0xED, 0xBB, 0xAA,
  };
  assertFrameEquals(buildControlPing(0x15, 0x42), expected, sizeof(expected));

  TEST_ASSERT_EQUAL_UINT32(0, encodeFrame(1, 2, nullptr, 1).len);
  uint8_t tooLarge[kMaxFrame] = {};
  TEST_ASSERT_EQUAL_UINT32(0, encodeFrame(1, 2, tooLarge, sizeof(tooLarge)).len);
}

void test_scanner_handles_fragmentation_noise_and_multiple_frames() {
  const FrameBytes first = buildControlPing(0x15, 0x42);
  const FrameBytes second = buildRunState(kRunStandby, 0x43);
  FrameScanner scanner;
  int count = 0;
  uint8_t codes[2] = {};

  scanner.feed(first.data(), 1, [&](const ParsedFrame&) { TEST_FAIL_MESSAGE("partial frame"); });
  scanner.feed(first.data() + 1, first.len - 1, [&](const ParsedFrame& frame) {
    codes[count++] = frame.code;
    TEST_ASSERT_EQUAL_HEX8(0x42, frame.data[0]);
  });

  uint8_t combined[2 + kMaxFrame * 2] = {0x00, 0x7F};
  std::memcpy(combined + 2, first.data(), first.len);
  std::memcpy(combined + 2 + first.len, second.data(), second.len);
  scanner.feed(combined, 2 + first.len + second.len, [&](const ParsedFrame& frame) {
    if (count < 2) {
      codes[count] = frame.code;
    }
    ++count;
  });

  TEST_ASSERT_EQUAL_INT(3, count);
  TEST_ASSERT_EQUAL_HEX8(0x15, codes[0]);
  TEST_ASSERT_EQUAL_HEX8(0x15, codes[1]);
}

void test_scanner_resynchronizes_after_bad_candidates() {
  const FrameBytes valid = buildControlPing(0x03, 0x17);
  uint8_t corrupt[kMaxFrame] = {};
  std::memcpy(corrupt, valid.data(), valid.len);
  corrupt[valid.len - 3] ^= 0x01;

  uint8_t input[kMaxFrame * 2] = {};
  std::memcpy(input, corrupt, valid.len);
  std::memcpy(input + valid.len, valid.data(), valid.len);

  FrameScanner scanner;
  int count = 0;
  scanner.feed(input, valid.len * 2, [&](const ParsedFrame&) { ++count; });
  TEST_ASSERT_EQUAL_INT(1, count);

  const uint8_t oversizedHeader[] = {0xAA, 0xBB, 0x06, 0x00, 0x01, 0xF4};
  uint8_t oversizedThenValid[kMaxFrame * 2] = {};
  std::memcpy(oversizedThenValid, oversizedHeader, sizeof(oversizedHeader));
  std::memcpy(oversizedThenValid + sizeof(oversizedHeader), valid.data(), valid.len);
  count = 0;
  scanner.feed(oversizedThenValid, sizeof(oversizedHeader) + valid.len,
               [&](const ParsedFrame&) { ++count; });
  TEST_ASSERT_EQUAL_INT(1, count);
}

void test_command_builders_and_timing_patch() {
  const FrameBytes motion = buildMotionVector(150, -150, 0x09);
  FrameScanner scanner;
  scanner.feed(motion.data(), motion.len, [](const ParsedFrame& frame) {
    TEST_ASSERT_EQUAL_HEX8(0x03, frame.family);
    TEST_ASSERT_EQUAL_HEX8(0x04, frame.code);
    TEST_ASSERT_EQUAL_HEX8(100, frame.data[1]);
    TEST_ASSERT_EQUAL_HEX8(static_cast<uint8_t>(-100), frame.data[2]);
  });

  uint8_t table[kTimingDataLen] = {};
  table[0] = 0x01;
  table[3] = 25;
  table[4] = 8;
  FrameBytes patched;
  TEST_ASSERT_TRUE(patchTimingTable(table, sizeof(table), 1, 80, 12, 0x22, patched));
  scanner.feed(patched.data(), patched.len, [](const ParsedFrame& frame) {
    TEST_ASSERT_EQUAL_HEX8(0x03, frame.family);
    TEST_ASSERT_EQUAL_HEX8(0x08, frame.code);
    TEST_ASSERT_EQUAL_HEX8(0x22, frame.data[0]);
    TEST_ASSERT_EQUAL_UINT8(80, frame.data[3]);
    TEST_ASSERT_EQUAL_UINT8(12, frame.data[4]);
  });
  TEST_ASSERT_FALSE(patchTimingTable(table, sizeof(table), 0, 50, 2, 1, patched));
  TEST_ASSERT_EQUAL_UINT32(0, buildKeypointSlots(1, nullptr).len);
  TEST_ASSERT_EQUAL_UINT32(0, buildRouteConfig(1, nullptr, nullptr, 1).len);
}

void test_state_reducer_decodes_snapshots_and_progress() {
  SharkState state;

  uint8_t status[41] = {};
  status[2] = 73;
  reduceFrame(state, parsed(0x06, 0x00, status, sizeof(status)));
  TEST_ASSERT_EQUAL_INT(73, state.battery);

  const uint8_t presence[] = {0x10, 1, 1, 1, 0, 0, 0, 0, 0};
  reduceFrame(state, parsed(0x06, 0x03, presence, sizeof(presence)));
  TEST_ASSERT_TRUE(state.presenceKnown);
  TEST_ASSERT_TRUE(state.present[2]);
  TEST_ASSERT_FALSE(state.present[3]);

  uint8_t timing[kTimingDataLen] = {};
  timing[3] = 65;
  timing[4] = 9;
  reduceFrame(state, parsed(0x06, 0x08, timing, sizeof(timing)));
  TEST_ASSERT_TRUE(state.timingKnown);
  TEST_ASSERT_EQUAL_INT(65, state.speed[1]);
  TEST_ASSERT_EQUAL_INT(9, state.hold[1]);

  uint8_t progress[kRunProgressDataLen] = {};
  progress[2] = kRunStart;
  progress[3] = 1;
  progress[5] = 0x01;
  progress[6] = 0xF4;
  reduceFrame(state, parsed(0x16, 0x10, progress, sizeof(progress)));
  TEST_ASSERT_EQUAL_HEX8(kRunStart, state.runStateCode);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 75.0f, state.runPercent);
  TEST_ASSERT_EQUAL_STRING("running", state.runText);

  progress[2] = kRunStop;
  reduceFrame(state, parsed(0x16, 0x10, progress, sizeof(progress)));
  TEST_ASSERT_EQUAL_HEX8(kRunStop, state.runStateCode);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, state.runPercent);
}

void test_reset_preserves_link_identity_and_preferences() {
  SharkState state;
  state.link = SharkState::Link::Connected;
  std::strcpy(state.deviceName, "Shark");
  state.hasSavedDevice = true;
  state.loopOn = true;
  state.battery = 50;
  state.present[0] = true;

  resetDeviceState(state);

  TEST_ASSERT_EQUAL_INT(static_cast<int>(SharkState::Link::Connected),
                        static_cast<int>(state.link));
  TEST_ASSERT_EQUAL_STRING("Shark", state.deviceName);
  TEST_ASSERT_TRUE(state.hasSavedDevice);
  TEST_ASSERT_TRUE(state.loopOn);
  TEST_ASSERT_EQUAL_INT(-1, state.battery);
  TEST_ASSERT_FALSE(state.present[0]);
}

void test_canon_smartphone_handshake_and_record_protocol() {
  const canon_ble::CommandBytes request =
      canon_ble::buildHandshakeRequest("Ble(e)p");
  TEST_ASSERT_EQUAL_UINT32(8, request.len);
  TEST_ASSERT_EQUAL_HEX8(0x01, request.bytes[0]);
  TEST_ASSERT_EQUAL_UINT8_ARRAY("Ble(e)p", &request.bytes[1], 7);

  uint8_t controllerId[16];
  for (size_t i = 0; i < sizeof(controllerId); ++i) {
    controllerId[i] = static_cast<uint8_t>(i + 1);
  }
  const canon_ble::CommandBytes id = canon_ble::buildControllerId(controllerId);
  TEST_ASSERT_EQUAL_UINT32(17, id.len);
  TEST_ASSERT_EQUAL_HEX8(0x03, id.bytes[0]);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(controllerId, &id.bytes[1],
                                sizeof(controllerId));

  const canon_ble::CommandBytes name =
      canon_ble::buildDeviceName("Ble(e)p");
  TEST_ASSERT_EQUAL_HEX8(0x04, name.bytes[0]);
  const canon_ble::CommandBytes type = canon_ble::buildAndroidDeviceType();
  const uint8_t expectedType[] = {0x05, 0x02};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expectedType, type.bytes, sizeof(expectedType));
  const uint8_t confirmed[] = {0x02};
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(canon_ble::PairingResponse::Confirmed),
      static_cast<int>(
          canon_ble::parsePairingResponse(confirmed, sizeof(confirmed))));

  const uint8_t postPairCommands[] = {0x06, 0x07, 0x08, 0x0c};
  const uint8_t postPairResponses[] = {0x01, 0x02, 0x03, 0x07};
  for (size_t i = 0; i < sizeof(postPairCommands); ++i) {
    const canon_ble::CommandBytes command =
        canon_ble::buildPostPairCommand(postPairCommands[i]);
    TEST_ASSERT_EQUAL_UINT32(1, command.len);
    TEST_ASSERT_EQUAL_HEX8(postPairCommands[i], command.bytes[0]);
    TEST_ASSERT_TRUE(canon_ble::isPostPairResponse(
        postPairCommands[i], &postPairResponses[i], 1));
  }

  const canon_ble::CommandBytes wake =
      canon_ble::buildModeCommand(canon_ble::kWakeMode);
  const canon_ble::CommandBytes leave =
      canon_ble::buildModeCommand(canon_ble::kLeaveShootingMode);
  const canon_ble::CommandBytes powerOff =
      canon_ble::buildModeCommand(canon_ble::kPowerOffMode);
  TEST_ASSERT_EQUAL_HEX8(0x03, wake.bytes[0]);
  TEST_ASSERT_EQUAL_HEX8(0x04, leave.bytes[0]);
  TEST_ASSERT_EQUAL_HEX8(0x05, powerOff.bytes[0]);

  const uint8_t acknowledged[] = {0x01};
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(canon_ble::ModeEvent::Acknowledged),
      static_cast<int>(
          canon_ble::parseModeEvent(acknowledged, sizeof(acknowledged))));
  const uint8_t sessionReady[] = {0x05};
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(canon_ble::ModeEvent::SessionReady),
      static_cast<int>(
          canon_ble::parseModeEvent(sessionReady, sizeof(sessionReady))));
  const uint8_t shootingReady[] = {0x04};
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(canon_ble::ModeEvent::SessionReady),
      static_cast<int>(
          canon_ble::parseModeEvent(shootingReady, sizeof(shootingReady))));

  const uint8_t expectedStart[] = {0x00, 0x10};
  const uint8_t expectedStop[] = {0x00, 0x11};
  const canon_ble::CommandBytes start = canon_ble::buildRecordCommand(true);
  const canon_ble::CommandBytes stop = canon_ble::buildRecordCommand(false);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expectedStart, start.bytes,
                                sizeof(expectedStart));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expectedStop, stop.bytes, sizeof(expectedStop));
}

void test_canon_trigger_pairing_name_and_trigger_bytes() {
  TEST_ASSERT_EQUAL_STRING("00050000-0000-1000-0000-d8492fffa821",
                           canon_trigger::kPrimaryServiceUuid);
  TEST_ASSERT_EQUAL_HEX8(0x88, canon_trigger::kRecordTriggerPress);
  TEST_ASSERT_EQUAL_HEX8(0x08, canon_trigger::kRecordTriggerRelease);
  TEST_ASSERT_EQUAL_UINT32(200, canon_trigger::kTriggerHoldMs);

  const canon_trigger::PairingName name =
      canon_trigger::buildPairingName("Studio Panel");
  TEST_ASSERT_EQUAL_UINT32(13, name.len);
  TEST_ASSERT_EQUAL_HEX8(0x03, name.bytes[0]);
  TEST_ASSERT_EQUAL_STRING("Studio Panel",
                           reinterpret_cast<const char*>(&name.bytes[1]));
}

void test_canon_camera_names_and_identity_address_types() {
  char displayName[studio::kDeviceNameCapacity] = "";
  TEST_ASSERT_TRUE(canon_camera::canonicalDisplayName(
      "EOSR6m2_D4D530", displayName, sizeof(displayName)));
  TEST_ASSERT_EQUAL_STRING("Canon EOS R6 Mark II", displayName);
  TEST_ASSERT_TRUE(canon_camera::canonicalDisplayName(
      "EOSR6m3_ABC123", displayName, sizeof(displayName)));
  TEST_ASSERT_EQUAL_STRING("Canon EOS R6 Mark III", displayName);
  TEST_ASSERT_FALSE(canon_camera::canonicalDisplayName(
      "Canon (Smart)", displayName, sizeof(displayName)));
  TEST_ASSERT_EQUAL_UINT8(0, studio::ble::identityAddressType(2));
  TEST_ASSERT_EQUAL_UINT8(1, studio::ble::identityAddressType(3));
  TEST_ASSERT_EQUAL_UINT8(1, studio::ble::identityAddressType(1));
}

void test_canon_trigger_state_tracks_trigger_outcome() {
  canon_trigger::CanonTriggerState state;
  state.claimedPeerVisible = true;
  canon_trigger::resetTransientState(state);
  TEST_ASSERT_FALSE(state.claimedPeerVisible);
  canon_trigger::markTriggerQueued(state);
  TEST_ASSERT_TRUE(state.triggerPending);
  TEST_ASSERT_EQUAL_UINT32(0, state.triggerCount);

  canon_trigger::markTriggerComplete(state, true);
  TEST_ASSERT_FALSE(state.triggerPending);
  TEST_ASSERT_TRUE(state.lastTriggerSucceeded);
  TEST_ASSERT_EQUAL_UINT32(1, state.triggerCount);

  canon_trigger::markTriggerQueued(state);
  canon_trigger::markTriggerComplete(state, false);
  TEST_ASSERT_FALSE(state.triggerPending);
  TEST_ASSERT_FALSE(state.lastTriggerSucceeded);
  TEST_ASSERT_EQUAL_UINT32(1, state.triggerCount);
}

void test_canon_state_requires_camera_notifications() {
  canon_ble::CanonBleState state;
  canon_ble::markCommandQueued(state, true);
  TEST_ASSERT_TRUE(state.commandPending);
  TEST_ASSERT_FALSE(state.recordingConfirmed);

  const uint8_t pressed[] = {0x10, 0x10, 0x10};
  canon_ble::reduceRecordNotification(state, pressed, sizeof(pressed));
  TEST_ASSERT_TRUE(state.commandPending);
  TEST_ASSERT_FALSE(state.recordingConfirmed);

  const uint8_t recording[] = {0x01, 0x01, 0x02};
  canon_ble::reduceRecordNotification(state, recording, sizeof(recording));
  TEST_ASSERT_FALSE(state.commandPending);
  TEST_ASSERT_TRUE(state.recordingConfirmed);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(canon_ble::CanonBleState::Recording::Recording),
      static_cast<int>(state.recording));

  const uint8_t stopped[] = {0x01, 0x01, 0x01};
  canon_ble::reduceRecordNotification(state, stopped, sizeof(stopped));
  TEST_ASSERT_TRUE(state.recordingConfirmed);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(canon_ble::CanonBleState::Recording::Stopped),
      static_cast<int>(state.recording));

  canon_ble::markCommandQueued(state, true);
  canon_ble::reduceRecordNotification(state, stopped, sizeof(stopped));
  TEST_ASSERT_FALSE(state.commandPending);
  TEST_ASSERT_TRUE(state.recordingConfirmed);
  TEST_ASSERT_TRUE(state.lastCommandFailed);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(canon_ble::CanonBleState::Recording::Stopped),
      static_cast<int>(state.recording));
  TEST_ASSERT_TRUE(canon_ble::completeStopIfAlreadyStopped(state));
  TEST_ASSERT_FALSE(state.lastCommandFailed);

  canon_ble::markCommandQueued(state, false);
  canon_ble::reduceRecordNotification(state, recording, sizeof(recording));
  TEST_ASSERT_FALSE(state.commandPending);
  TEST_ASSERT_TRUE(state.recordingConfirmed);
  TEST_ASSERT_TRUE(state.lastCommandFailed);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(canon_ble::CanonBleState::Recording::Recording),
      static_cast<int>(state.recording));
}

void test_tascam_cobs_commands_match_capture() {
  const uint8_t expectedStart[] = {
      0x00, 0x05, 0x44, 0x52, 0x10, 0x41, 0x02, 0x0B, 0x01,
      0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x00,
  };
  const uint8_t expectedStop[] = {
      0x00, 0x05, 0x44, 0x52, 0x10, 0x41, 0x02, 0x08, 0x01,
      0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x00,
  };
  const tascam_x8::FrameBytes start = tascam_x8::buildRecordStart();
  const tascam_x8::FrameBytes stop = tascam_x8::buildRecordStop();
  TEST_ASSERT_EQUAL_UINT32(sizeof(expectedStart), start.len);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expectedStart, start.data(), start.len);
  TEST_ASSERT_EQUAL_UINT32(sizeof(expectedStop), stop.len);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expectedStop, stop.data(), stop.len);
  TEST_ASSERT_EQUAL_UINT32(221, tascam_x8::buildInitialization().len);
  TEST_ASSERT_EQUAL_STRING("2456e1b9-26e2-8f83-e744-f34f01e9d701",
                           tascam_x8::kPrimaryServiceUuid);
}

void test_tascam_scanner_and_confirmed_state() {
  tascam_x8::FrameScanner scanner;
  tascam_x8::TascamX8State state;
  uint8_t payload[18] = {'D', 'R', 0x20, 0x20, 0x24, 0x00};
  tascam_x8::FrameBytes release =
      tascam_x8::encodeFrame(payload, sizeof(payload));
  scanner.feed(release.data(), 4, [&](const tascam_x8::ParsedFrame&) {
    TEST_FAIL_MESSAGE("partial COBS frame");
  });
  scanner.feed(release.data() + 4, release.len - 4,
               [&](const tascam_x8::ParsedFrame& frame) {
                 tascam_x8::reduceFrame(state, frame);
               });
  TEST_ASSERT_FALSE(state.recordingConfirmed);

  payload[5] = 0x01;
  const tascam_x8::FrameBytes started =
      tascam_x8::encodeFrame(payload, sizeof(payload));
  uint8_t combined[tascam_x8::kMaxEncodedFrame * 2] = {};
  combined[0] = 0x72;
  combined[1] = 0x00;
  std::memcpy(combined + 2, started.data(), started.len);
  scanner.feed(combined, started.len + 2,
               [&](const tascam_x8::ParsedFrame& frame) {
                 tascam_x8::reduceFrame(state, frame);
               });
  TEST_ASSERT_TRUE(state.recordingConfirmed);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(tascam_x8::TascamX8State::Recording::Recording),
      static_cast<int>(state.recording));

  std::memset(payload, 0, sizeof(payload));
  payload[0] = 'D';
  payload[1] = 'R';
  payload[2] = 0x10;
  payload[3] = 0x20;
  payload[4] = 0x08;
  const tascam_x8::FrameBytes stopped =
      tascam_x8::encodeFrame(payload, sizeof(payload));
  scanner.feed(stopped.data(), stopped.len,
               [&](const tascam_x8::ParsedFrame& frame) {
                 tascam_x8::reduceFrame(state, frame);
               });
  TEST_ASSERT_TRUE(state.recordingConfirmed);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(tascam_x8::TascamX8State::Recording::Stopped),
      static_cast<int>(state.recording));

  uint8_t statusPayload[18] = {'D', 'R', 0x20, 0x20, 0x00, 0x81};
  const tascam_x8::FrameBytes recordingStatus =
      tascam_x8::encodeFrame(statusPayload, sizeof(statusPayload));
  scanner.feed(recordingStatus.data(), recordingStatus.len,
               [&](const tascam_x8::ParsedFrame& frame) {
                 tascam_x8::reduceFrame(state, frame);
               });
  TEST_ASSERT_TRUE(state.recordingConfirmed);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(tascam_x8::TascamX8State::Recording::Recording),
      static_cast<int>(state.recording));

  statusPayload[5] = 0x82;
  const tascam_x8::FrameBytes transitionalStatus =
      tascam_x8::encodeFrame(statusPayload, sizeof(statusPayload));
  scanner.feed(transitionalStatus.data(), transitionalStatus.len,
               [&](const tascam_x8::ParsedFrame& frame) {
                 tascam_x8::reduceFrame(state, frame);
               });
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(tascam_x8::TascamX8State::Recording::Recording),
      static_cast<int>(state.recording));

  statusPayload[5] = 0x10;
  const tascam_x8::FrameBytes stoppedStatus =
      tascam_x8::encodeFrame(statusPayload, sizeof(statusPayload));
  scanner.feed(stoppedStatus.data(), stoppedStatus.len,
               [&](const tascam_x8::ParsedFrame& frame) {
                 tascam_x8::reduceFrame(state, frame);
               });
  TEST_ASSERT_TRUE(state.recordingConfirmed);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(tascam_x8::TascamX8State::Recording::Stopped),
      static_cast<int>(state.recording));
  state.lastCommandFailed = true;
  TEST_ASSERT_TRUE(tascam_x8::completeStopIfAlreadyStopped(state));
  TEST_ASSERT_FALSE(state.lastCommandFailed);
}

void test_driver_catalog_exposes_shark_and_canon() {
  TEST_ASSERT_EQUAL_UINT32(12, studio::DriverCatalog::count());
  const studio::DriverDescriptor* descriptor =
      studio::DriverCatalog::find(studio::DriverId::SharkNanoII);
  TEST_ASSERT_NOT_NULL(descriptor);
  TEST_ASSERT_EQUAL_STRING("ifootage.shark_nano_ii", descriptor->stableId);
  TEST_ASSERT_EQUAL_UINT8(1, descriptor->maxInstances);
  const studio::DriverDescriptor* trigger =
      studio::DriverCatalog::find(studio::DriverId::CanonTrigger);
  TEST_ASSERT_NOT_NULL(trigger);
  TEST_ASSERT_EQUAL_STRING("Canon (Trigger)", trigger->model);
  TEST_ASSERT_EQUAL_STRING("canon.eos_r6.trigger", trigger->stableId);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::DeviceType::Camera),
                        static_cast<int>(trigger->type));
  TEST_ASSERT_BITS_HIGH(
      studio::capabilityBit(studio::Capability::Link) |
          studio::capabilityBit(studio::Capability::RecordTrigger),
      trigger->capabilities);
  const studio::DriverDescriptor* canon =
      studio::DriverCatalog::find(studio::DriverId::CanonBle);
  TEST_ASSERT_NOT_NULL(canon);
  TEST_ASSERT_EQUAL_STRING("Canon (Smart)", canon->model);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::DeviceType::Camera),
                        static_cast<int>(canon->type));
  TEST_ASSERT_EQUAL_STRING("canon.eos_r6.smartphone_ble", canon->stableId);
  TEST_ASSERT_BITS_HIGH(
      studio::capabilityBit(studio::Capability::RecordStart) |
          studio::capabilityBit(studio::Capability::RecordStop) |
          studio::capabilityBit(studio::Capability::RecordingState),
      canon->capabilities);
  const studio::DriverDescriptor* tascam =
      studio::DriverCatalog::find(studio::DriverId::TascamX8);
  TEST_ASSERT_NOT_NULL(tascam);
  TEST_ASSERT_EQUAL_STRING("tascam.portacapture_x8", tascam->stableId);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::DeviceType::Recorder),
                        static_cast<int>(tascam->type));
  TEST_ASSERT_BITS_HIGH(studio::capabilityBit(studio::Capability::RecordStart) |
                            studio::capabilityBit(studio::Capability::RecordStop) |
                            studio::capabilityBit(studio::Capability::RecordingState),
                        tascam->capabilities);
  const studio::DriverDescriptor* homeAssistant =
      studio::DriverCatalog::find(studio::DriverId::HomeAssistant);
  TEST_ASSERT_NOT_NULL(homeAssistant);
  TEST_ASSERT_EQUAL_UINT8(4, homeAssistant->maxInstances);
  const studio::DriverDescriptor* aputure =
      studio::DriverCatalog::find(studio::DriverId::AputureLight);
  TEST_ASSERT_NOT_NULL(aputure);
  TEST_ASSERT_EQUAL_STRING("aputure.light", aputure->stableId);
  TEST_ASSERT_EQUAL_STRING("Aputure", aputure->brand);
  TEST_ASSERT_EQUAL_STRING("Aputure Light", aputure->model);
  TEST_ASSERT_TRUE(aputure->discoverable);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::DeviceType::Light),
                        static_cast<int>(aputure->type));
  TEST_ASSERT_BITS_HIGH(
      studio::capabilityBit(studio::Capability::SetLightCct) |
          studio::capabilityBit(studio::Capability::SetLightRgb),
      aputure->capabilities);
  TEST_ASSERT_BITS_HIGH(
      studio::capabilityBit(studio::Capability::TurnOn) |
          studio::capabilityBit(studio::Capability::TurnOff),
      aputure->capabilities);
  const studio::DriverDescriptor* zhiyun =
      studio::DriverCatalog::find(studio::DriverId::ZhiyunLight);
  TEST_ASSERT_NOT_NULL(zhiyun);
  TEST_ASSERT_EQUAL_STRING("zhiyun.light", zhiyun->stableId);
  TEST_ASSERT_EQUAL_STRING("Zhiyun Light", zhiyun->model);
  TEST_ASSERT_EQUAL_UINT8(4, zhiyun->maxInstances);
  TEST_ASSERT_BITS_HIGH(
      studio::capabilityBit(studio::Capability::TurnOn) |
          studio::capabilityBit(studio::Capability::TurnOff) |
          studio::capabilityBit(studio::Capability::SetLightCct) |
          studio::capabilityBit(studio::Capability::SetLightRgb),
      zhiyun->capabilities);
  TEST_ASSERT_BITS_LOW(
      studio::capabilityBit(studio::Capability::SetLightTint),
      zhiyun->capabilities);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(zhiyun_light::MolusModel::X100),
      static_cast<int>(zhiyun_light::modelFromName("MOLUS X100")));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(zhiyun_light::MolusModel::X60Rgb),
      static_cast<int>(zhiyun_light::modelFromName("MOLUS X60RGB")));
  const studio::DriverDescriptor* goPro =
      studio::DriverCatalog::find(studio::DriverId::GoPro);
  TEST_ASSERT_NOT_NULL(goPro);
  TEST_ASSERT_EQUAL_STRING("GoPro", goPro->model);
  TEST_ASSERT_EQUAL_UINT8(4, goPro->maxInstances);
  TEST_ASSERT_BITS_HIGH(
      studio::capabilityBit(studio::Capability::RecordStart) |
          studio::capabilityBit(studio::Capability::RecordStop) |
          studio::capabilityBit(studio::Capability::RecordingState),
      goPro->capabilities);
  const studio::DriverDescriptor* insta360Driver =
      studio::DriverCatalog::find(studio::DriverId::Insta360);
  TEST_ASSERT_NOT_NULL(insta360Driver);
  TEST_ASSERT_EQUAL_STRING("Insta360", insta360Driver->model);
  TEST_ASSERT_BITS_HIGH(
      studio::capabilityBit(studio::Capability::RecordStart) |
          studio::capabilityBit(studio::Capability::RecordStop) |
          studio::capabilityBit(studio::Capability::RecordingState),
      insta360Driver->capabilities);
  TEST_ASSERT_BITS_LOW(
      studio::capabilityBit(studio::Capability::RecordTrigger),
      insta360Driver->capabilities);
  TEST_ASSERT_EQUAL_STRING(
      "DJI Osmo", studio::DriverCatalog::find(studio::DriverId::DjiOsmo)->model);
  TEST_ASSERT_EQUAL_STRING(
      "Sony Camera", studio::DriverCatalog::find(studio::DriverId::SonyCamera)->model);
  TEST_ASSERT_EQUAL_STRING(
      "Phone Camera", studio::DriverCatalog::find(studio::DriverId::PhoneCamera)->model);
  TEST_ASSERT_EQUAL_STRING(
      "phone.camera.hid",
      studio::DriverCatalog::find(studio::DriverId::PhoneCamera)->stableId);
  TEST_ASSERT_BITS_HIGH(
      studio::capabilityBit(studio::Capability::RecordTrigger),
      studio::DriverCatalog::find(studio::DriverId::PhoneCamera)->capabilities);
  TEST_ASSERT_NULL(studio::DriverCatalog::find(static_cast<studio::DriverId>(99)));
}

void test_manager_keeps_every_compiled_camera_driver_reachable() {
  MemoryBackend backend;
  FakeDriver shark(studio::DriverId::SharkNanoII);
  FakeDriver canonSmart(studio::DriverId::CanonBle);
  FakeDriver tascam(studio::DriverId::TascamX8);
  FakeDriver canonTrigger(studio::DriverId::CanonTrigger);
  FakeDriver homeAssistant(studio::DriverId::HomeAssistant);
  FakeDriver aputure(studio::DriverId::AputureLight);
  FakeDriver zhiyun(studio::DriverId::ZhiyunLight);
  FakeDriver goPro(studio::DriverId::GoPro);
  FakeDriver insta360(studio::DriverId::Insta360);
  FakeDriver dji(studio::DriverId::DjiOsmo);
  FakeDriver sony(studio::DriverId::SonyCamera);
  FakeDriver phone(studio::DriverId::PhoneCamera);
  studio::DeviceDriver* drivers[] = {
      &shark, &canonSmart, &tascam, &canonTrigger, &homeAssistant,
      &aputure, &zhiyun, &goPro, &insta360, &dji,
      &sony, &phone};
  studio::DeviceManager manager(backend, drivers,
                                sizeof(drivers) / sizeof(drivers[0]));
  TEST_ASSERT_TRUE(manager.begin());

  studio::InstanceId instanceId = studio::kInvalidInstanceId;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(manager.beginAdd(studio::DriverId::PhoneCamera,
                                        "Phone Camera", instanceId)));
  TEST_ASSERT_TRUE(
      manager.acquire(instanceId, studio::ConnectionOwner::Foreground));
  TEST_ASSERT_TRUE(
      manager.ownedBy(instanceId, studio::ConnectionOwner::Foreground));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(manager.cancelPendingAdd(instanceId)));
}

void test_insta360_explicit_recording_is_available_to_scenes() {
  MemoryBackend deviceBackend;
  MemoryBackend sceneBackend;
  FakeDriver insta360(studio::DriverId::Insta360);
  studio::DeviceDriver* drivers[] = {&insta360};
  studio::DeviceManager devices(deviceBackend, drivers, 1);
  TEST_ASSERT_TRUE(devices.begin());

  studio::InstanceId cameraId = studio::kInvalidInstanceId;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(devices.add(studio::DriverId::Insta360, "Insta360",
                                   cameraId)));
  studio::SceneService scenes(sceneBackend, devices);
  TEST_ASSERT_TRUE(scenes.begin());
  studio::SceneId sceneId = studio::kInvalidSceneId;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::SceneRegistryStatus::Ok),
      static_cast<int>(scenes.add("Insta360 camera", sceneId)));
  studio::SceneRecord record = *scenes.find(sceneId);
  record.startCount = 1;
  record.startSteps[0] =
      studio::makeActionStep(cameraId, studio::CommandType::RecordStart);
  record.stopCount = 1;
  record.stopSteps[0] =
      studio::makeActionStep(cameraId, studio::CommandType::RecordStop);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::SceneRegistryStatus::Ok),
      static_cast<int>(scenes.replace(record)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::SceneValidationStatus::Ok),
                        static_cast<int>(scenes.validate(sceneId)));

  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::SceneRunStatus::Ok),
                        static_cast<int>(scenes.start(sceneId)));
  for (uint32_t now = 1; now < 20; ++now) {
    devices.loop();
    scenes.loop(now);
  }
  TEST_ASSERT_EQUAL_INT(1, insta360.dispatchCount);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::CommandType::RecordStart),
                        static_cast<int>(insta360.lastCommand));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::SceneRunStatus::Ok),
                        static_cast<int>(scenes.stop()));
  for (uint32_t now = 20; now < 40; ++now) {
    devices.loop();
    scenes.loop(now);
  }
  TEST_ASSERT_EQUAL_INT(2, insta360.dispatchCount);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::CommandType::RecordStop),
                        static_cast<int>(insta360.lastCommand));
}

void test_gopro_open_ble_packets_and_confirmed_state() {
  const gopro::Packet start = gopro::buildSetShutter(true);
  const uint8_t expectedStart[] = {0x03, 0x01, 0x01, 0x01};
  TEST_ASSERT_EQUAL_UINT32(sizeof(expectedStart), start.len);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expectedStart, start.bytes, start.len);
  const gopro::Packet stop = gopro::buildSetShutter(false);
  const uint8_t expectedStop[] = {0x03, 0x01, 0x01, 0x00};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expectedStop, stop.bytes, stop.len);
  const gopro::Packet sleep = gopro::buildSleep();
  const uint8_t expectedSleep[] = {0x01, 0x05};
  TEST_ASSERT_EQUAL_UINT32(sizeof(expectedSleep), sleep.len);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expectedSleep, sleep.bytes, sleep.len);
  const gopro::Packet pairing = gopro::buildSetPairingState();
  const uint8_t expectedPairing[] = {0x03, 0x17, 0x01, 0x01};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expectedPairing, pairing.bytes, pairing.len);
  const gopro::Packet hardware = gopro::buildGetHardwareInfo();
  const uint8_t expectedHardware[] = {0x01, 0x3c};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expectedHardware, hardware.bytes, hardware.len);
  const gopro::Packet encoding = gopro::buildRegisterEncoding();
  const uint8_t expectedEncoding[] = {0x02, 0x53, 0x0a};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expectedEncoding, encoding.bytes, encoding.len);
  const gopro::Packet encoding2 = gopro::buildRegisterEncoding(true);
  const uint8_t expectedEncoding2[] = {0x03, 0x56, 0x00, 0x0a};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expectedEncoding2, encoding2.bytes,
                               encoding2.len);

  const uint8_t ok[] = {0x02, 0x01, 0x00};
  const gopro::Response response = gopro::parseResponse(ok, sizeof(ok));
  TEST_ASSERT_TRUE(response.valid);
  TEST_ASSERT_EQUAL_HEX8(gopro::kSetShutterCommand, response.command);
  TEST_ASSERT_EQUAL_HEX8(gopro::kSuccessStatus, response.status);
  TEST_ASSERT_FALSE(gopro::parseResponse(ok, 2).valid);

  studio::ble::Advertisement sleeping;
  const uint8_t lowPowerAdvertisement[] = {
      3, 0x03, 0xa6, 0xfe, 5, 0xff, 0xf2, 0x02, 0x03, 0x00};
  std::memcpy(sleeping.payload, lowPowerAdvertisement,
              sizeof(lowPowerAdvertisement));
  sleeping.payloadLength = sizeof(lowPowerAdvertisement);
  bool processorKnown = false;
  TEST_ASSERT_FALSE(gopro::processorAwake(sleeping, processorKnown));
  TEST_ASSERT_TRUE(processorKnown);
  sleeping.payload[9] = 0x01;
  TEST_ASSERT_TRUE(gopro::processorAwake(sleeping, processorKnown));
  TEST_ASSERT_TRUE(processorKnown);

  gopro::PacketAccumulator commandPackets;
  gopro::Message message;
  const uint8_t hardwareFirst[] = {0x20, 0x16, 0x3c, 0x00, 0x01, 0x3e};
  const uint8_t hardwareSecond[] = {
      0x80, 'M', 'A', 'X', '2', 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  TEST_ASSERT_FALSE(commandPackets.feed(hardwareFirst, sizeof(hardwareFirst),
                                        message));
  TEST_ASSERT_TRUE(commandPackets.feed(hardwareSecond, sizeof(hardwareSecond),
                                       message));
  TEST_ASSERT_EQUAL_UINT32(22, message.len);
  const gopro::Response hardwareResponse =
      gopro::parseCommandPayload(message.bytes, message.len);
  TEST_ASSERT_TRUE(hardwareResponse.valid);
  TEST_ASSERT_EQUAL_HEX8(gopro::kGetHardwareInfoCommand,
                         hardwareResponse.command);
  TEST_ASSERT_EQUAL_HEX8(gopro::kSuccessStatus, hardwareResponse.status);

  const uint8_t registeredIdle[] = {0x53, 0x00, 0x0a, 0x01, 0x00};
  const gopro::StatusResponse idle =
      gopro::parseStatusPayload(registeredIdle, sizeof(registeredIdle));
  TEST_ASSERT_TRUE(idle.valid);
  TEST_ASSERT_TRUE(idle.hasEncoding);
  TEST_ASSERT_FALSE(idle.encoding);
  const uint8_t notifiedRecording[] = {0x93, 0x00, 0x0a, 0x01, 0x01};
  const gopro::StatusResponse recording = gopro::parseStatusPayload(
      notifiedRecording, sizeof(notifiedRecording));
  TEST_ASSERT_TRUE(recording.valid);
  TEST_ASSERT_TRUE(recording.hasEncoding);
  TEST_ASSERT_TRUE(recording.encoding);
  const gopro::Packet getEncoding = gopro::buildGetEncoding();
  const uint8_t expectedGetEncoding[] = {0x02, 0x13, 0x0a};
  TEST_ASSERT_EQUAL_UINT8(sizeof(expectedGetEncoding), getEncoding.len);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expectedGetEncoding, getEncoding.bytes,
                                getEncoding.len);
  const uint8_t queriedIdle[] = {0x13, 0x00, 0x0a, 0x01, 0x00};
  const gopro::StatusResponse query =
      gopro::parseStatusPayload(queriedIdle, sizeof(queriedIdle));
  TEST_ASSERT_TRUE(query.valid);
  TEST_ASSERT_TRUE(query.hasEncoding);
  TEST_ASSERT_FALSE(query.encoding);

  gopro::GoProState state;
  gopro::markCommandQueued(state, true);
  TEST_ASSERT_TRUE(state.commandPending);
  gopro::reduceCommandResponse(state, gopro::kSuccessStatus);
  TEST_ASSERT_TRUE(state.commandPending);
  TEST_ASSERT_FALSE(state.recordingConfirmed);
  TEST_ASSERT_FALSE(gopro::reducePendingEncodingStatus(state, false, true));
  TEST_ASSERT_TRUE(state.commandPending);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(gopro::GoProState::Recording::Starting),
                        static_cast<int>(state.recording));
  TEST_ASSERT_TRUE(gopro::reducePendingEncodingStatus(state, true, true));
  TEST_ASSERT_FALSE(state.commandPending);
  TEST_ASSERT_FALSE(state.lastCommandFailed);
  TEST_ASSERT_TRUE(state.recordingConfirmed);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(gopro::GoProState::Recording::Recording),
                        static_cast<int>(state.recording));
  gopro::markCommandQueued(state, false);
  gopro::reduceCommandResponse(state, 0x02);
  TEST_ASSERT_TRUE(state.lastCommandFailed);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(gopro::GoProState::Recording::Unknown),
                        static_cast<int>(state.recording));
}

void test_dji_osmo_protocol_matches_official_connection_vector() {
  const uint8_t address[] = {0x38, 0x34, 0x56, 0x78, 0x9a, 0xbc};
  const dji_osmo::Packet packet = dji_osmo::buildConnectionRequest(
      1, 0x12345678, address, 0, 0x1ffe);
  const uint8_t expected[] = {
      0xaa,0x33,0x00,0x02,0x00,0x00,0x00,0x00,0x01,0x00,0x98,0x2f,0x00,0x19,
      0x78,0x56,0x34,0x12,0x06,0x38,0x34,0x56,0x78,0x9a,0xbc,0x00,0x00,0x00,
      0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
      0xfe,0x1f,0x00,0x00,0x00,0x00,0x79,0x6c,0x66,0x7c};
  TEST_ASSERT_EQUAL_UINT32(sizeof(expected), packet.len);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, packet.bytes, packet.len);
  const auto frame = dji_osmo::parseFrame(packet.bytes, packet.len);
  TEST_ASSERT_TRUE(frame.valid);
  TEST_ASSERT_EQUAL_UINT8(dji_osmo::kCmdConnection, frame.commandId);

  const auto approval = dji_osmo::buildConnectionRequest(
      9, 0x0000ff44, address, 2, 0);
  const auto parsedApproval =
      dji_osmo::parseFrame(approval.bytes, approval.len);
  bool approved = false;
  TEST_ASSERT_TRUE(dji_osmo::parseConnectionApproval(parsedApproval, approved));
  TEST_ASSERT_TRUE(approved);

  const auto firstPair = dji_osmo::buildConnectionRequest(
      2, 0x12345678, address, 1, 42);
  const auto parsedFirstPair =
      dji_osmo::parseFrame(firstPair.bytes, firstPair.len);
  TEST_ASSERT_EQUAL_UINT8(1, parsedFirstPair.payload[26]);
  TEST_ASSERT_EQUAL_UINT8(42, parsedFirstPair.payload[27]);
  TEST_ASSERT_EQUAL_UINT8(0, parsedFirstPair.payload[28]);

  const auto secondCamera = dji_osmo::buildConnectionResponse(
      3, 0x12345678, 2);
  const auto parsedSecondCamera =
      dji_osmo::parseFrame(secondCamera.bytes, secondCamera.len);
  TEST_ASSERT_TRUE(parsedSecondCamera.valid);
  TEST_ASSERT_EQUAL_UINT8(2, parsedSecondCamera.payload[5]);
  TEST_ASSERT_EQUAL_UINT8(0, parsedSecondCamera.payload[6]);
  TEST_ASSERT_EQUAL_UINT8(0, parsedSecondCamera.payload[7]);
  TEST_ASSERT_EQUAL_UINT8(0, parsedSecondCamera.payload[8]);

  bool recording = true;
  TEST_ASSERT_TRUE(dji_osmo::decodeCameraRecordingStatus(0x01, 0x00,
                                                         recording));
  TEST_ASSERT_FALSE(recording);
  TEST_ASSERT_TRUE(dji_osmo::decodeCameraRecordingStatus(0x38, 0x03,
                                                         recording));
  TEST_ASSERT_TRUE(recording);
  TEST_ASSERT_TRUE(dji_osmo::decodeCameraRecordingStatus(0x3f, 0x03,
                                                         recording));
  TEST_ASSERT_FALSE(recording);
  TEST_ASSERT_FALSE(dji_osmo::decodeCameraRecordingStatus(0x01, 0xff,
                                                          recording));

  const auto start = dji_osmo::buildRecordControl(7, true);
  const auto parsedStart = dji_osmo::parseFrame(start.bytes, start.len);
  TEST_ASSERT_TRUE(parsedStart.valid);
  TEST_ASSERT_EQUAL_UINT8(dji_osmo::kCmdRecord, parsedStart.commandId);
  TEST_ASSERT_EQUAL_UINT8(0, parsedStart.payload[4]);
}

void test_insta360_gps_remote_protocol() {
  TEST_ASSERT_TRUE(insta360::matchesCameraName("X5 123456"));
  TEST_ASSERT_TRUE(insta360::matchesCameraName("GO 3 654321"));
  TEST_ASSERT_TRUE(insta360::matchesCameraName("GO Ultra 123456"));
  TEST_ASSERT_TRUE(insta360::isGoUltra("Insta360 GO Ultra"));
  TEST_ASSERT_FALSE(insta360::isGoUltra("GO 3 654321"));
  TEST_ASSERT_EQUAL_STRING(insta360::kNotifyUuid,
                           insta360::kGattCharacteristicOrder[0]);
  TEST_ASSERT_EQUAL_STRING(insta360::kWriteUuid,
                           insta360::kGattCharacteristicOrder[1]);
  TEST_ASSERT_EQUAL_STRING(insta360::kInfoUuid,
                           insta360::kGattCharacteristicOrder[2]);
  TEST_ASSERT_EQUAL_STRING("Insta360 Remote (Bleep)",
                           insta360::kAdvertisedName);
  TEST_ASSERT_EQUAL_UINT16(0x0180, insta360::kAdvertisedAppearance);
  const uint8_t advertisement[] = {
      0x02,0x01,0x06,0x18,0x09,
      'I','n','s','t','a','3','6','0',' ','R','e','m','o','t','e',' ',
      '(','B','l','e','e','p',')'};
  TEST_ASSERT_EQUAL_UINT32(28, sizeof(insta360::kAdvertisementData));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(advertisement,
                                insta360::kAdvertisementData,
                                sizeof(advertisement));
  const uint8_t scanResponse[] = {
      0x03,0x19,0x80,0x01,0x03,0x03,0x80,0xce};
  TEST_ASSERT_EQUAL_UINT32(8, sizeof(insta360::kScanResponseData));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(scanResponse,
                                insta360::kScanResponseData,
                                sizeof(scanResponse));
  uint8_t wakeAdvertisement[insta360::kWakeAdvertisementDataLength] = {};
  uint8_t wakeScanResponse[insta360::kWakeScanResponseDataLength] = {};
  TEST_ASSERT_TRUE(insta360::buildWakeAdvertisementData(
      "X5 1HDKAB", wakeAdvertisement, wakeScanResponse));
  const uint8_t capturedWakeAdvertisement[] = {
      0x02,0x01,0x06,0x1b,0xff,0x4c,0x00,0x02,
      0x15,0x09,'O','R','B','I','T',0x09,
      0xff,0x0f,0x00,'1','H','D','K','A','B',
      0x00,0x00,0x00,0x00,0xe4,0x01};
  const uint8_t capturedWakeScanResponse[] = {0x02,0x0a,0x00};
  TEST_ASSERT_EQUAL_UINT32(sizeof(capturedWakeAdvertisement),
                           insta360::kWakeAdvertisementDataLength);
  TEST_ASSERT_EQUAL_UINT32(sizeof(capturedWakeScanResponse),
                           insta360::kWakeScanResponseDataLength);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(capturedWakeAdvertisement,
                                wakeAdvertisement,
                                sizeof(capturedWakeAdvertisement));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(capturedWakeScanResponse,
                                wakeScanResponse,
                                sizeof(capturedWakeScanResponse));
  TEST_ASSERT_FALSE(insta360::buildWakeAdvertisementData(
      nullptr, wakeAdvertisement, wakeScanResponse));
  TEST_ASSERT_FALSE(insta360::buildWakeAdvertisementData(
      "X5", wakeAdvertisement, wakeScanResponse));
  TEST_ASSERT_FALSE(insta360::buildWakeAdvertisementData(
      "X5 ABC", wakeAdvertisement, wakeScanResponse));
  TEST_ASSERT_FALSE(insta360::buildWakeAdvertisementData(
      "X5 ABCDEFG", wakeAdvertisement, wakeScanResponse));
  TEST_ASSERT_FALSE(insta360::buildWakeAdvertisementData(
      "X5 AB-CD1", wakeAdvertisement, wakeScanResponse));
  TEST_ASSERT_FALSE(insta360::buildWakeAdvertisementData(
      "Camera ABC123", wakeAdvertisement, wakeScanResponse));
  const uint8_t expected[] = {0xfc,0xef,0xfe,0x86,0x00,0x03,0x01,0x02,0x00};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, insta360::kShutterCommand,
                                sizeof(expected));
  const uint8_t powerOff[] = {0xfc,0xef,0xfe,0x86,0x00,0x03,0x01,0x00,0x03};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(powerOff, insta360::kPowerOffCommand,
                                sizeof(powerOff));

  insta360::State provisional;
  insta360::assumeVideoIdle(provisional);
  TEST_ASSERT_TRUE(insta360::canStartRecording(provisional));
  TEST_ASSERT_FALSE(insta360::canStopRecording(provisional));
  TEST_ASSERT_FALSE(provisional.recordingConfirmed);
  provisional.recording = insta360::State::Recording::Recording;
  provisional.recordingConfirmed = true;
  TEST_ASSERT_FALSE(insta360::canStartRecording(provisional));
  TEST_ASSERT_TRUE(insta360::canStopRecording(provisional));

  insta360::CaptureStatus status;
  const uint8_t gpsVideoIdle[] = {
      0xfe,0xef,0xfe,0x10,0x80,0x07,0x01,0x2c,0x46,0x01,'3','5','m'};
  const uint8_t gpsRecording[] = {
      0xfe,0xef,0xfe,0x10,0x80,0x0d,0x01,0x0e,0x46,0x01,'.',
      '0','0',':','0','0',':','0','0'};
  const uint8_t gpsVideoIdleHours[] = {
      0xfe,0xef,0xfe,0x10,0x80,0x07,0x01,0x2c,0x46,0x01,
      '1','h','0','5','m'};
  const uint8_t gpsPhotoIdle[] = {
      0xfe,0xef,0xfe,0x10,0x80,0x09,0x01,0x1e,0x46,0x01,' ',
      '9','9','9','+'};
  const uint8_t gpsPhotoSaving[] = {
      0xfe,0xef,0xfe,0x10,0x80,0x05,0x01,0x34,0x2c,0x02,'5'};
  TEST_ASSERT_TRUE(insta360::decodeCaptureStatus(
      gpsVideoIdle, sizeof(gpsVideoIdle), status));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(insta360::CaptureMode::Video),
                        static_cast<int>(status.mode));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(insta360::CapturePhase::Idle),
                        static_cast<int>(status.phase));
  TEST_ASSERT_TRUE(insta360::decodeCaptureStatus(
      gpsVideoIdleHours, sizeof(gpsVideoIdleHours), status));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(insta360::CaptureMode::Video),
                        static_cast<int>(status.mode));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(insta360::CapturePhase::Idle),
                        static_cast<int>(status.phase));
  uint8_t invalidGpsIdle[sizeof(gpsVideoIdleHours)];
  std::memcpy(invalidGpsIdle, gpsVideoIdleHours, sizeof(invalidGpsIdle));
  invalidGpsIdle[11] = '?';
  TEST_ASSERT_FALSE(insta360::decodeCaptureStatus(
      invalidGpsIdle, sizeof(invalidGpsIdle), status));
  TEST_ASSERT_TRUE(insta360::decodeCaptureStatus(
      gpsRecording, sizeof(gpsRecording), status));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(insta360::CapturePhase::Active),
                        static_cast<int>(status.phase));
  TEST_ASSERT_TRUE(insta360::decodeCaptureStatus(
      gpsPhotoIdle, sizeof(gpsPhotoIdle), status));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(insta360::CaptureMode::Photo),
                        static_cast<int>(status.mode));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(insta360::CapturePhase::Idle),
                        static_cast<int>(status.phase));
  TEST_ASSERT_TRUE(insta360::decodeCaptureStatus(
      gpsPhotoSaving, sizeof(gpsPhotoSaving), status));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(insta360::CapturePhase::Saving),
                        static_cast<int>(status.phase));

  const uint8_t videoStarting[] = {0xfe,0xef,0xfe,0x55,0x00,0x07,0x00,0x01,0,0,0,0,0};
  const uint8_t videoRecording[] = {0xfe,0xef,0xfe,0x55,0x00,0x07,0x00,0x02,0,0,0,0,0};
  const uint8_t videoStopping[] = {0xfe,0xef,0xfe,0x55,0x00,0x07,0x00,0x04,0,0,0,0,0};
  const uint8_t videoStopped[] = {0xfe,0xef,0xfe,0x55,0x00,0x07,0x00,0x00,0,0,0,0,0};
  TEST_ASSERT_TRUE(insta360::decodeCaptureStatus(
      videoStarting, sizeof(videoStarting), status));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(insta360::CaptureMode::Video),
                        static_cast<int>(status.mode));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(insta360::CapturePhase::Starting),
                        static_cast<int>(status.phase));
  TEST_ASSERT_TRUE(insta360::decodeCaptureStatus(
      videoRecording, sizeof(videoRecording), status));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(insta360::CapturePhase::Active),
                        static_cast<int>(status.phase));
  TEST_ASSERT_TRUE(insta360::decodeCaptureStatus(
      videoStopping, sizeof(videoStopping), status));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(insta360::CapturePhase::Stopping),
                        static_cast<int>(status.phase));
  TEST_ASSERT_TRUE(insta360::decodeCaptureStatus(
      videoStopped, sizeof(videoStopped), status));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(insta360::CapturePhase::Idle),
                        static_cast<int>(status.phase));

  const uint8_t photoSaving[] = {0xfe,0xef,0xfe,0x55,0x00,0x07,0x01,0x05,0,0,0,0,0};
  TEST_ASSERT_TRUE(insta360::decodeCaptureStatus(
      photoSaving, sizeof(photoSaving), status));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(insta360::CaptureMode::Photo),
                        static_cast<int>(status.mode));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(insta360::CapturePhase::Saving),
                        static_cast<int>(status.phase));
  TEST_ASSERT_FALSE(insta360::decodeCaptureStatus(
      photoSaving, sizeof(photoSaving) - 1, status));
  uint8_t invalid[sizeof(photoSaving)];
  std::memcpy(invalid, photoSaving, sizeof(invalid));
  invalid[3] = 0x56;
  TEST_ASSERT_FALSE(insta360::decodeCaptureStatus(
      invalid, sizeof(invalid), status));
}
