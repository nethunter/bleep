void test_ble_central_lazy_lifetime_and_slot_exhaustion() {
  studio::ble::FakeBleBackend backend;
  studio::ble::BleCentral central(backend);
  BleTestDelegate delegates[CONFIG_MAX_ACTIVE_LINKS + 1];
  studio::ble::LinkHandle handles[CONFIG_MAX_ACTIVE_LINKS] = {};

  TEST_ASSERT_FALSE(backend.initialized());
  for (size_t i = 0; i < CONFIG_MAX_ACTIVE_LINKS; ++i) {
    handles[i] = central.acquire(delegates[i], {});
    TEST_ASSERT_NOT_EQUAL(studio::ble::kInvalidLinkHandle, handles[i]);
  }
  TEST_ASSERT_TRUE(backend.initialized());
  TEST_ASSERT_EQUAL_UINT32(1, backend.beginCalls());
  TEST_ASSERT_EQUAL_UINT32(
      studio::ble::kInvalidLinkHandle,
      central.acquire(delegates[CONFIG_MAX_ACTIVE_LINKS], {}));

  for (studio::ble::LinkHandle handle : handles) {
    central.release(handle);
  }
  TEST_ASSERT_FALSE(backend.initialized());
  TEST_ASSERT_EQUAL_UINT32(1, backend.shutdownCalls());
}

void test_ble_central_timing_and_readiness_reset_on_release() {
  studio::ble::FakeBleBackend backend;
  studio::ble::BleCentral central(backend);
  BleTestDelegate delegate;
  studio::ble::LinkHandle link = central.acquire(delegate, {});
  central.loop(25);
  TEST_ASSERT_TRUE(central.requestScan(link));
  TEST_ASSERT_EQUAL_UINT32(25, central.timingStartedAt(link));
  central.markProtocolReady(link);
  TEST_ASSERT_TRUE(central.protocolReady(link));

  central.release(link);
  TEST_ASSERT_FALSE(central.protocolReady(link));
  TEST_ASSERT_EQUAL_UINT32(0, central.timingStartedAt(link));

  link = central.acquire(delegate, {});
  central.loop(100);
  TEST_ASSERT_TRUE(central.requestScan(link));
  TEST_ASSERT_EQUAL_UINT32(100, central.timingStartedAt(link));
  TEST_ASSERT_FALSE(central.protocolReady(link));
  central.release(link);
}

void test_ble_central_pauses_scan_for_peripheral_gatt_mutation() {
  studio::ble::FakeBleBackend backend;
  studio::ble::BleCentral central(backend);
  BleTestDelegate delegate;
  const studio::ble::LinkHandle link = central.acquire(delegate, {});

  TEST_ASSERT_TRUE(central.requestScan(link));
  TEST_ASSERT_TRUE(backend.scanRunning());
  const uint32_t startsBeforePause = backend.scanStarts();
  central.pauseScanForGattMutation();
  TEST_ASSERT_FALSE(backend.scanRunning());
  TEST_ASSERT_EQUAL_UINT32(1, backend.scanStops());
  TEST_ASSERT_TRUE(central.scanning(link));

  central.loop(1);
  TEST_ASSERT_TRUE(backend.scanRunning());
  TEST_ASSERT_EQUAL_UINT32(startsBeforePause + 1, backend.scanStarts());
  central.release(link);
}

void test_ble_central_suspends_shared_scan_during_connect_and_security() {
  studio::ble::FakeBleBackend backend;
  studio::ble::BleCentral central(backend);
  BleTestDelegate connecting;
  BleTestDelegate scanning;
  studio::ble::ConnectPolicy policy;
  policy.security = studio::ble::SecurityPolicy::BondSecure;
  const studio::ble::LinkHandle connectingLink =
      central.acquire(connecting, policy);
  const studio::ble::LinkHandle scanningLink = central.acquire(scanning, {});
  connecting.central = &central;
  connecting.requestSecurityOnConnect = true;

  TEST_ASSERT_TRUE(central.requestScan(scanningLink));
  TEST_ASSERT_TRUE(backend.scanRunning());
  TEST_ASSERT_TRUE(central.requestConnect(
      connectingLink, bleAddress("11:22:33:44:55:66")));
  TEST_ASSERT_FALSE(backend.scanRunning());
  TEST_ASSERT_TRUE(central.scanning(scanningLink));

  studio::ble::Event connected;
  connected.type = studio::ble::EventType::Connected;
  connected.link = connectingLink;
  TEST_ASSERT_TRUE(backend.emit(connected));
  central.loop(100);
  TEST_ASSERT_TRUE(connecting.securityRequested);
  TEST_ASSERT_FALSE(backend.scanRunning());

  studio::ble::Event secured;
  secured.type = studio::ble::EventType::SecurityComplete;
  secured.link = connectingLink;
  secured.succeeded = true;
  TEST_ASSERT_TRUE(backend.emit(secured));
  central.loop(101);
  TEST_ASSERT_TRUE(backend.scanRunning());
  TEST_ASSERT_TRUE(central.scanning(scanningLink));

  central.release(connectingLink);
  central.release(scanningLink);
}

void test_ble_central_ignores_queued_event_from_evicted_link_generation() {
  studio::ble::FakeBleBackend backend;
  studio::ble::BleCentral central(backend);
  BleTestDelegate first;
  BleTestDelegate replacement;

  const studio::ble::LinkHandle oldLink = central.acquire(first, {});
  studio::ble::Event staleConnected;
  staleConnected.type = studio::ble::EventType::Connected;
  staleConnected.link = oldLink;
  TEST_ASSERT_TRUE(backend.emit(staleConnected));

  central.release(oldLink);
  const studio::ble::LinkHandle newLink = central.acquire(replacement, {});
  TEST_ASSERT_EQUAL_UINT32(oldLink, newLink);
  central.loop(10);
  TEST_ASSERT_EQUAL_UINT32(0, replacement.events);

  studio::ble::Event currentConnected;
  currentConnected.type = studio::ble::EventType::Connected;
  currentConnected.link = newLink;
  TEST_ASSERT_TRUE(backend.emit(currentConnected));
  central.loop(20);
  TEST_ASSERT_EQUAL_UINT32(1, replacement.events);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::ble::EventType::Connected),
                        static_cast<int>(replacement.lastEvent));
  central.release(newLink);
}

void test_ble_central_shared_scan_claims_and_independent_release() {
  studio::ble::FakeBleBackend backend;
  studio::ble::BleCentral central(backend);
  BleTestDelegate first;
  BleTestDelegate second;
  first.central = &central;
  first.selectAdvertisements = true;
  const studio::ble::LinkHandle firstLink = central.acquire(first, {});
  const studio::ble::LinkHandle secondLink = central.acquire(second, {});
  TEST_ASSERT_TRUE(central.requestScan(firstLink));
  TEST_ASSERT_TRUE(central.requestScan(secondLink));
  TEST_ASSERT_EQUAL_UINT32(1, backend.scanStarts());

  const studio::ble::Advertisement advertisement =
      bleAdvertisement("11:22:33:44:55:66", "Portacapture X8", 0xfff0);
  backend.emitAdvertisement(advertisement);
  central.loop(10);
  TEST_ASSERT_EQUAL_UINT32(1, first.advertisements);
  TEST_ASSERT_EQUAL_UINT32(1, second.advertisements);
  TEST_ASSERT_TRUE(first.selected);
  TEST_ASSERT_FALSE(backend.scanRunning());
  TEST_ASSERT_TRUE(central.scanning(secondLink));
  TEST_ASSERT_FALSE(
      central.selectAdvertisement(secondLink, advertisement));

  studio::ble::Event connected;
  connected.type = studio::ble::EventType::Connected;
  connected.link = firstLink;
  TEST_ASSERT_TRUE(backend.emit(connected));
  central.loop(11);
  TEST_ASSERT_TRUE(backend.scanRunning());

  central.release(secondLink);
  TEST_ASSERT_FALSE(backend.scanRunning());
  TEST_ASSERT_EQUAL_UINT32(1, central.activeCount());
  central.release(firstLink);
}

void test_ble_central_concurrent_links_retry_watchdog_and_security() {
  studio::ble::FakeBleBackend backend;
  studio::ble::BleCentral central(backend);
  BleTestDelegate first;
  BleTestDelegate second;
  studio::ble::ConnectPolicy policy;
  policy.security = studio::ble::SecurityPolicy::BondSecure;
  policy.connectWatchdogMs = 6000;
  const studio::ble::LinkHandle firstLink = central.acquire(first, policy);
  const studio::ble::LinkHandle secondLink = central.acquire(second, {});
  first.central = &central;
  first.requestSecurityOnConnect = true;
  TEST_ASSERT_TRUE(
      central.requestConnect(firstLink, bleAddress("11:22:33:44:55:66")));
  TEST_ASSERT_TRUE(
      central.requestConnect(secondLink, bleAddress("22:33:44:55:66:77")));
  TEST_ASSERT_EQUAL_UINT32(1, backend.connectCalls(firstLink));
  TEST_ASSERT_EQUAL_UINT32(0, backend.connectCalls(secondLink));

  studio::ble::Event connected;
  connected.type = studio::ble::EventType::Connected;
  connected.link = firstLink;
  backend.emit(connected);
  central.loop(100);
  TEST_ASSERT_FALSE(central.protocolReady(firstLink));
  TEST_ASSERT_EQUAL_UINT32(1, backend.parameterUpdateCalls(firstLink));
  TEST_ASSERT_EQUAL_UINT16(6, backend.lastParameters(firstLink).minInterval);
  TEST_ASSERT_TRUE(first.securityRequested);
  TEST_ASSERT_EQUAL_UINT32(1, backend.secureCalls(firstLink));
  TEST_ASSERT_EQUAL_UINT32(0, backend.connectCalls(secondLink));
  studio::ble::Event secured;
  secured.type = studio::ble::EventType::SecurityComplete;
  secured.link = firstLink;
  secured.succeeded = true;
  backend.emit(secured);
  central.loop(200);
  TEST_ASSERT_EQUAL_UINT32(1, backend.connectCalls(secondLink));
  backend.setParameterUpdateResult(false);
  central.markProtocolReady(firstLink);
  TEST_ASSERT_TRUE(central.protocolReady(firstLink));
  TEST_ASSERT_EQUAL_UINT32(2, backend.parameterUpdateCalls(firstLink));
  TEST_ASSERT_EQUAL_UINT16(24, backend.lastParameters(firstLink).minInterval);

  studio::ble::Event failed;
  failed.type = studio::ble::EventType::ConnectFailed;
  failed.link = secondLink;
  backend.emit(failed);
  central.loop(200);
  central.loop(1699);
  TEST_ASSERT_EQUAL_UINT32(1, backend.connectCalls(secondLink));
  central.loop(1700);
  TEST_ASSERT_EQUAL_UINT32(2, backend.connectCalls(secondLink));

  backend.emit(failed);
  central.loop(1701);
  central.loop(4700);
  TEST_ASSERT_EQUAL_UINT32(2, backend.connectCalls(secondLink));
  central.loop(4701);
  TEST_ASSERT_EQUAL_UINT32(3, backend.connectCalls(secondLink));

  // After three failed direct attempts, rediscover the peer rather than
  // retrying a potentially stale saved address forever. Passive scanning
  // resumes immediately; the backoff only settles direct re-attempts.
  backend.emit(failed);
  central.loop(4702);
  TEST_ASSERT_TRUE(backend.scanRunning());
  // A known peer that just failed to establish re-advertises once after a
  // long silence, so the scan stays continuous for 15 s instead of pausing
  // between 4 s bursts; the usual duty cycle resumes afterwards.
  central.loop(8702);
  TEST_ASSERT_TRUE(backend.scanRunning());
  central.loop(12702);
  TEST_ASSERT_TRUE(backend.scanRunning());
  central.loop(16702);
  TEST_ASSERT_TRUE(backend.scanRunning());
  central.loop(20701);
  TEST_ASSERT_TRUE(backend.scanRunning());
  central.loop(20702);
  TEST_ASSERT_FALSE(backend.scanRunning());
  central.loop(22201);
  TEST_ASSERT_FALSE(backend.scanRunning());
  central.loop(22202);
  TEST_ASSERT_TRUE(backend.scanRunning());

  TEST_ASSERT_TRUE(central.requestConnect(
      secondLink, bleAddress("22:33:44:55:66:77")));
  central.loop(28202);
  TEST_ASSERT_TRUE(backend.disconnectCalls(secondLink) > 0);
  central.release(firstLink);
  TEST_ASSERT_FALSE(central.protocolReady(firstLink));
  central.release(secondLink);
}

void test_ble_central_scan_first_policy_connects_on_advertisement() {
  studio::ble::FakeBleBackend backend;
  studio::ble::BleCentral central(backend);
  BleTestDelegate delegate;
  studio::ble::ConnectPolicy policy;
  policy.directAttemptsBeforeScan = 0;
  const studio::ble::LinkHandle link = central.acquire(delegate, policy);

  // With no direct attempts the central listens and connects on the next
  // advertisement from the saved address, scanning continuously meanwhile.
  TEST_ASSERT_TRUE(
      central.requestConnect(link, bleAddress("11:22:33:44:55:66")));
  central.loop(1);
  TEST_ASSERT_EQUAL_UINT32(0, backend.connectCalls(link));
  TEST_ASSERT_TRUE(backend.scanRunning());
  central.loop(4001);
  TEST_ASSERT_TRUE(backend.scanRunning());
  central.loop(5000);
  TEST_ASSERT_TRUE(backend.scanRunning());

  studio::ble::Advertisement advertisement;
  advertisement.address = bleAddress("11:22:33:44:55:66");
  TEST_ASSERT_TRUE(central.selectAdvertisement(link, advertisement));
  TEST_ASSERT_EQUAL_UINT32(1, backend.connectCalls(link));

  // A failed establishment goes straight back to listening, no backoff.
  studio::ble::Event failed;
  failed.type = studio::ble::EventType::ConnectFailed;
  failed.link = link;
  backend.emit(failed);
  central.loop(5001);
  TEST_ASSERT_TRUE(backend.scanRunning());
  TEST_ASSERT_EQUAL_UINT32(1, backend.connectCalls(link));
  central.release(link);
}

void test_tascam_reconnect_scans_after_one_direct_failure() {
  studio::ble::FakeBleBackend backend;
  studio::ble::BleCentral central(backend);
  BleTestDelegate delegate;
  studio::ble::ConnectPolicy policy;
  policy.directAttemptsBeforeScan = tascam_x8::kDirectAttemptsBeforeScan;
  const studio::ble::LinkHandle link = central.acquire(delegate, policy);

  TEST_ASSERT_TRUE(
      central.requestConnect(link, bleAddress("11:22:33:44:55:66")));
  TEST_ASSERT_EQUAL_UINT32(1, backend.connectCalls(link));

  studio::ble::Event failed;
  failed.type = studio::ble::EventType::ConnectFailed;
  failed.link = link;
  backend.emit(failed);
  // The single direct attempt is followed by passive scanning immediately:
  // listening does not disturb the peripheral, so no backoff applies, and
  // the scan stays continuous while the wake is pending.
  central.loop(1);
  TEST_ASSERT_TRUE(backend.scanRunning());
  TEST_ASSERT_EQUAL_UINT32(1, backend.connectCalls(link));
  central.loop(1501);
  TEST_ASSERT_EQUAL_UINT32(1, backend.connectCalls(link));
  central.loop(4001);
  TEST_ASSERT_TRUE(backend.scanRunning());
  central.release(link);
}

void test_zhiyun_reconnect_scans_after_one_direct_failure() {
  studio::ble::FakeBleBackend backend;
  studio::ble::BleCentral central(backend);
  BleTestDelegate delegate;
  studio::ble::ConnectPolicy policy;
  policy.directAttemptsBeforeScan = zhiyun_light::kDirectAttemptsBeforeScan;
  const studio::ble::LinkHandle link = central.acquire(delegate, policy);

  TEST_ASSERT_TRUE(
      central.requestConnect(link, bleAddress("11:22:33:44:55:66")));
  TEST_ASSERT_EQUAL_UINT32(1, backend.connectCalls(link));

  studio::ble::Event failed;
  failed.type = studio::ble::EventType::ConnectFailed;
  failed.link = link;
  backend.emit(failed);
  // The single direct attempt is followed by passive scanning immediately:
  // listening does not disturb the peripheral, so no backoff applies.
  central.loop(1);
  TEST_ASSERT_TRUE(backend.scanRunning());
  TEST_ASSERT_EQUAL_UINT32(1, backend.connectCalls(link));
  central.loop(1501);
  TEST_ASSERT_EQUAL_UINT32(1, backend.connectCalls(link));
  central.release(link);
}

void test_ble_central_protocol_failure_can_stop_retry() {
  studio::ble::FakeBleBackend backend;
  studio::ble::BleCentral central(backend);
  BleTestDelegate delegate;
  const studio::ble::LinkHandle link = central.acquire(delegate, {});
  TEST_ASSERT_TRUE(
      central.requestConnect(link, bleAddress("11:22:33:44:55:66")));
  TEST_ASSERT_EQUAL_UINT32(1, backend.connectCalls(link));

  studio::ble::Event connected;
  connected.type = studio::ble::EventType::Connected;
  connected.link = link;
  backend.emit(connected);
  central.loop(100);
  central.markProtocolFailed(link, false);
  TEST_ASSERT_EQUAL_UINT32(1, backend.disconnectCalls(link));

  studio::ble::Event disconnected;
  disconnected.type = studio::ble::EventType::Disconnected;
  disconnected.link = link;
  backend.emit(disconnected);
  central.loop(101);
  central.loop(10000);
  TEST_ASSERT_EQUAL_UINT32(1, backend.connectCalls(link));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::ble::LinkPhase::Idle),
                        static_cast<int>(central.phase(link)));
  central.release(link);
}

void test_ble_central_cancelled_connect_failure_does_not_retry() {
  studio::ble::FakeBleBackend backend;
  studio::ble::BleCentral central(backend);
  BleTestDelegate delegate;
  const studio::ble::LinkHandle link = central.acquire(delegate, {});
  TEST_ASSERT_TRUE(
      central.requestConnect(link, bleAddress("11:22:33:44:55:66")));
  TEST_ASSERT_EQUAL_UINT32(1, backend.connectCalls(link));

  central.disconnect(link);
  studio::ble::Event failed;
  failed.type = studio::ble::EventType::ConnectFailed;
  failed.link = link;
  TEST_ASSERT_TRUE(backend.emit(failed));
  central.loop(1);
  central.loop(10000);

  TEST_ASSERT_EQUAL_UINT32(1, backend.connectCalls(link));
  TEST_ASSERT_FALSE(backend.scanRunning());
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::ble::LinkPhase::Idle),
                        static_cast<int>(central.phase(link)));
  central.release(link);
}

void test_ble_central_uses_bounded_scan_bursts() {
  studio::ble::FakeBleBackend backend;
  studio::ble::BleCentral central(backend);
  BleTestDelegate delegate;
  const studio::ble::LinkHandle link = central.acquire(delegate, {});
  central.loop(100);
  TEST_ASSERT_TRUE(central.requestScan(link));
  TEST_ASSERT_TRUE(backend.scanRunning());
  TEST_ASSERT_EQUAL_UINT32(1, backend.scanStarts());

  central.loop(4099);
  TEST_ASSERT_TRUE(backend.scanRunning());
  central.loop(4100);
  TEST_ASSERT_FALSE(backend.scanRunning());
  central.loop(5599);
  TEST_ASSERT_FALSE(backend.scanRunning());
  central.loop(5600);
  TEST_ASSERT_TRUE(backend.scanRunning());
  TEST_ASSERT_EQUAL_UINT32(2, backend.scanStarts());
  central.release(link);
}

void test_ble_central_parser_bonds_and_queue_overflow() {
  const studio::ble::Advertisement advertisement =
      bleAdvertisement("11:22:33:44:55:66", "Shark Nano II", 0xfff0);
  TEST_ASSERT_TRUE(
      studio::ble::advertisementNameEquals(advertisement, "Shark Nano II"));
  TEST_ASSERT_TRUE(
      studio::ble::advertisementNameContains(advertisement, "Nano"));
  TEST_ASSERT_TRUE(studio::ble::advertisesService(advertisement, "fff0"));

  studio::ble::FakeBleBackend backend;
  studio::ble::BleCentral central(backend);
  TEST_ASSERT_TRUE(
      central.deleteBond(bleAddress("11:22:33:44:55:66")));
  TEST_ASSERT_EQUAL_UINT32(1, backend.bondDeleteCalls());
  TEST_ASSERT_FALSE(backend.initialized());

  for (size_t i = 0; i < CONFIG_BLE_EVENT_QUEUE_SIZE + 2; ++i) {
    backend.emitAdvertisement(advertisement);
  }
  TEST_ASSERT_EQUAL_UINT32(2, backend.droppedEvents());
}

void test_ble_device_advertisement_matchers() {
  const studio::ble::Advertisement sharkAdvertisement =
      bleAdvertisement("11:22:33:44:55:66", "Shark Nano II", 0xfff0);
  TEST_ASSERT_TRUE(shark::matchesAdvertisement(sharkAdvertisement));
  const studio::ble::Advertisement tascamAdvertisement =
      bleAdvertisement("22:33:44:55:66:77", tascam_x8::kDeviceName, 0x1800);
  TEST_ASSERT_TRUE(tascam_x8::matchesAdvertisement(tascamAdvertisement));
  TEST_ASSERT_TRUE(tascam_x8::matchesSavedAdvertisement(
      tascamAdvertisement, "22:33:44:55:66:77", 0));
  TEST_ASSERT_FALSE(tascam_x8::matchesSavedAdvertisement(
      tascamAdvertisement, "33:44:55:66:77:88", 0));
  TEST_ASSERT_FALSE(tascam_x8::matchesSavedAdvertisement(
      tascamAdvertisement, "22:33:44:55:66:77", 1));
  const uint8_t tascamUuid[16] = {
      0x24, 0x56, 0xe1, 0xb9, 0x26, 0xe2, 0x8f, 0x83,
      0xe7, 0x44, 0xf3, 0x4f, 0x01, 0xe9, 0xd7, 0x01};
  TEST_ASSERT_TRUE(
      tascam_x8::matchesAdvertisement(bleAdvertisement128(tascamUuid)));
  TEST_ASSERT_FALSE(tascam_x8::matchesAdvertisement(
      bleAdvertisement("22:33:44:55:66:77", "ANNA-B1-BC5A07", 0x1800)));
  const studio::ble::Advertisement unprovisionedAmaran =
      bleAdvertisement("44:55:66:77:88:99", "Amaran", 0x1827);
  TEST_ASSERT_TRUE(studio::ble::advertisesService(unprovisionedAmaran, "1827"));

  studio::ble::Advertisement provisioningIdentity = unprovisionedAmaran;
  const uint8_t provisioningData[] = {
      21, 0x16, 0x27, 0x18,
      0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
      0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x40, 0x00};
  std::memcpy(provisioningIdentity.payload + provisioningIdentity.payloadLength,
              provisioningData, sizeof(provisioningData));
  provisioningIdentity.payloadLength += sizeof(provisioningData);
  uint8_t deviceUuid[16] = {};
  uint8_t oobInformation[2] = {};
  TEST_ASSERT_TRUE(studio::ble::meshProvisioningIdentity(
      provisioningIdentity, deviceUuid, oobInformation));
  const uint8_t expectedUuid[] = {
      0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
      0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expectedUuid, deviceUuid,
                                sizeof(expectedUuid));
  TEST_ASSERT_EQUAL_HEX8(0x40, oobInformation[0]);
  TEST_ASSERT_EQUAL_HEX8(0x00, oobInformation[1]);
  provisioningIdentity.payload[provisioningIdentity.payloadLength - 20] = 0x28;
  TEST_ASSERT_FALSE(studio::ble::meshProvisioningIdentity(
      provisioningIdentity, deviceUuid, oobInformation));

  const uint8_t triggerUuid[16] = {
      0x00, 0x05, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00,
      0x00, 0x00, 0xd8, 0x49, 0x2f, 0xff, 0xa8, 0x21};
  TEST_ASSERT_TRUE(canon_trigger::matchesAdvertisement(
      bleAdvertisement128(triggerUuid)));

  studio::ble::Advertisement canonAdvertisement;
  canonAdvertisement.address = bleAddress("33:44:55:66:77:88");
  canonAdvertisement.payload[0] = 3;
  canonAdvertisement.payload[1] = 0xff;
  canonAdvertisement.payload[2] = 0xa9;
  canonAdvertisement.payload[3] = 0x01;
  canonAdvertisement.payloadLength = 4;
  TEST_ASSERT_TRUE(
      canon_ble::matchesAdvertisement(canonAdvertisement));
}
