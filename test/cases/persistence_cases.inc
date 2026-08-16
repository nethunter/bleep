void test_persistence_golden_bytes_and_current_schema_boundary() {
  MemoryBackend panelBackend;
  studio::PanelSettingsStore panelStore(panelBackend);
  studio::PanelSettings panel;
  panel.hapticEnabled = true;
  TEST_ASSERT_TRUE(panelStore.save(panel));
  const uint8_t expectedPanel[] = {
      0x50, 0x53, 0x45, 0x54, 0x02, 0x00,
      0x01, 0x00, 0xbe, 0x5a, 0x62, 0x22};
  TEST_ASSERT_EQUAL_UINT32(sizeof(expectedPanel), panelBackend.length());
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expectedPanel, panelBackend.data(),
                                sizeof(expectedPanel));

  // This is a validly checksummed version-1 empty device blob. Version 2 is
  // the deliberate clean-storage boundary, so the old schema must be rejected
  // rather than silently interpreted or rewritten.
  const uint8_t oldDeviceBlob[] = {
      0x53, 0x54, 0x44, 0x56, 0x01, 0x00, 0x01, 0x00,
      0x01, 0x00, 0x00, 0x00, 0xe5, 0x49, 0xbf, 0x8a};
  MemoryBackend deviceBackend;
  deviceBackend.seed(oldDeviceBlob, sizeof(oldDeviceBlob));
  studio::ConfigStore deviceStore(deviceBackend);
  studio::DeviceRegistry registry;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::ConfigLoadStatus::Corrupt),
                        static_cast<int>(deviceStore.load(registry)));
}

void test_registry_crud_and_single_shark_limit() {
  studio::DeviceRegistry registry;
  studio::InstanceId first = studio::kInvalidInstanceId;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(registry.add(studio::DriverId::SharkNanoII, "Main Slider", 1, first)));
  TEST_ASSERT_NOT_EQUAL(studio::kInvalidInstanceId, first);

  studio::InstanceId duplicate = studio::kInvalidInstanceId;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::DuplicateDriver),
      static_cast<int>(
          registry.add(studio::DriverId::SharkNanoII, "Second Slider", 1, duplicate)));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(registry.rename(first, "A-Cam Slider")));
  TEST_ASSERT_EQUAL_STRING("A-Cam Slider", registry.find(first)->displayName);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(registry.setEnabled(first, false)));
  TEST_ASSERT_FALSE(registry.find(first)->enabled);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(registry.remove(first)));
  TEST_ASSERT_EQUAL_UINT32(0, registry.count());
  TEST_ASSERT_TRUE(registry.initialized());
}

void test_transactional_add_commits_only_after_pairing_and_readiness() {
  MemoryBackend backend;
  FakeDriver driver(studio::DriverId::CanonBle);
  studio::DeviceDriver* drivers[] = {&driver};
  studio::DeviceManager devices(backend, drivers, 1);
  TEST_ASSERT_TRUE(devices.begin());
  const size_t initialCount = devices.count();

  studio::InstanceId id = studio::kInvalidInstanceId;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(devices.beginAdd(studio::DriverId::CanonBle,
                                        "Pending Camera", id)));
  TEST_ASSERT_TRUE(devices.isPendingAdd(id));
  TEST_ASSERT_EQUAL_UINT32(initialCount, devices.count());
  TEST_ASSERT_NOT_NULL(devices.find(id));
  TEST_ASSERT_FALSE(backend.containsText("Pending Camera"));
  TEST_ASSERT_TRUE(devices.acquire(id, studio::ConnectionOwner::Foreground));

  driver.ready = false;
  driver.pairingUpdatePending = true;
  devices.loop();
  TEST_ASSERT_TRUE(devices.find(id)->paired);
  TEST_ASSERT_TRUE(devices.isPendingAdd(id));
  TEST_ASSERT_EQUAL_UINT32(initialCount, devices.count());
  TEST_ASSERT_FALSE(backend.containsText("Pending Camera"));

  driver.ready = true;
  devices.loop();
  TEST_ASSERT_FALSE(devices.isPendingAdd(id));
  TEST_ASSERT_EQUAL_UINT32(initialCount + 1, devices.count());
  TEST_ASSERT_TRUE(backend.containsText("Pending Camera"));
  TEST_ASSERT_TRUE(devices.ownedBy(id, studio::ConnectionOwner::Foreground));
}

void test_transactional_add_cancel_and_failed_save_do_not_register_device() {
  MemoryBackend backend;
  FakeDriver driver(studio::DriverId::CanonBle);
  studio::DeviceDriver* drivers[] = {&driver};
  studio::DeviceManager devices(backend, drivers, 1);
  TEST_ASSERT_TRUE(devices.begin());
  const size_t initialCount = devices.count();

  studio::InstanceId id = studio::kInvalidInstanceId;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(devices.beginAdd(studio::DriverId::CanonBle,
                                        "Canceled Camera", id)));
  TEST_ASSERT_TRUE(devices.acquire(id, studio::ConnectionOwner::Foreground));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Invalid),
      static_cast<int>(devices.beginAdd(studio::DriverId::CanonBle,
                                        "Second Draft", id)));
  const studio::InstanceId canceledId = devices.pendingAdd();
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(devices.cancelPendingAdd(canceledId)));
  TEST_ASSERT_EQUAL_INT(1, driver.cancelOnboardingCount);
  TEST_ASSERT_FALSE(devices.isActive(canceledId));
  TEST_ASSERT_EQUAL_UINT32(initialCount, devices.count());
  TEST_ASSERT_FALSE(backend.containsText("Canceled Camera"));

  studio::DeviceManager restarted(backend, drivers, 1);
  TEST_ASSERT_TRUE(restarted.begin());
  TEST_ASSERT_EQUAL_UINT32(initialCount, restarted.count());

  studio::InstanceId failedId = studio::kInvalidInstanceId;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(devices.beginAdd(studio::DriverId::CanonBle,
                                        "Save Retry Camera", failedId)));
  TEST_ASSERT_TRUE(
      devices.acquire(failedId, studio::ConnectionOwner::Foreground));
  backend.failWrites = true;
  driver.ready = true;
  driver.pairingUpdatePending = true;
  devices.loop();
  TEST_ASSERT_TRUE(devices.pendingAddCommitFailed(failedId));
  TEST_ASSERT_EQUAL_UINT32(initialCount, devices.count());
  TEST_ASSERT_FALSE(backend.containsText("Save Retry Camera"));

  backend.failWrites = false;
  TEST_ASSERT_TRUE(devices.retryPendingAdd(failedId));
  TEST_ASSERT_FALSE(devices.isPendingAdd(failedId));
  TEST_ASSERT_EQUAL_UINT32(initialCount + 1, devices.count());
  TEST_ASSERT_TRUE(backend.containsText("Save Retry Camera"));
}

void test_config_round_trip_preserves_dormant_records_and_detects_corruption() {
  MemoryBackend backend;
  studio::ConfigStore store(backend);
  studio::DeviceRegistry source;
  studio::DeviceRecord dormant;
  dormant.instanceId = 7;
  dormant.driverId = static_cast<studio::DriverId>(77);
  dormant.enabled = false;
  std::strcpy(dormant.displayName, "Future Device");
  TEST_ASSERT_TRUE(source.restore(&dormant, 1, 8, true));
  TEST_ASSERT_TRUE(store.save(source));

  studio::DeviceRegistry restored;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::ConfigLoadStatus::Loaded),
      static_cast<int>(store.load(restored)));
  TEST_ASSERT_EQUAL_UINT32(1, restored.count());
  TEST_ASSERT_EQUAL_INT(77, static_cast<int>(restored.at(0)->driverId));
  TEST_ASSERT_EQUAL_STRING("Future Device", restored.at(0)->displayName);

  backend.corruptLastByte();
  studio::DeviceRegistry rejected;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::ConfigLoadStatus::Corrupt),
      static_cast<int>(store.load(rejected)));
}

void test_config_round_trip_at_twenty_four_record_capacity() {
  MemoryBackend backend;
  studio::ConfigStore store(backend);
  studio::DeviceRecord records[CONFIG_MAX_DEVICE_INSTANCES] = {};
  for (size_t i = 0; i < CONFIG_MAX_DEVICE_INSTANCES; ++i) {
    records[i].instanceId = static_cast<studio::InstanceId>(i + 1);
    records[i].driverId = static_cast<studio::DriverId>(100 + i);
    records[i].enabled = (i % 2) == 0;
    records[i].paired = (i % 3) == 0;
    std::strcpy(records[i].displayName, "Dormant device");
  }

  studio::DeviceRegistry source;
  TEST_ASSERT_EQUAL_UINT32(24, source.capacity());
  TEST_ASSERT_TRUE(source.restore(records, CONFIG_MAX_DEVICE_INSTANCES,
                                  CONFIG_MAX_DEVICE_INSTANCES + 1, true));
  TEST_ASSERT_TRUE(store.save(source));

  studio::DeviceRegistry restored;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::ConfigLoadStatus::Loaded),
      static_cast<int>(store.load(restored)));
  TEST_ASSERT_EQUAL_UINT32(CONFIG_MAX_DEVICE_INSTANCES, restored.count());
  TEST_ASSERT_EQUAL_UINT32(CONFIG_MAX_DEVICE_INSTANCES,
                           restored.at(CONFIG_MAX_DEVICE_INSTANCES - 1)
                               ->instanceId);
  TEST_ASSERT_EQUAL_INT(
      100 + CONFIG_MAX_DEVICE_INSTANCES - 1,
      static_cast<int>(
          restored.at(CONFIG_MAX_DEVICE_INSTANCES - 1)->driverId));
}

void test_home_assistant_config_is_separate_checksummed_and_local_only() {
  MemoryBackend deviceBackend;
  MemoryBackend secretBackend;
  studio::ConfigStore deviceStore(deviceBackend);
  studio::HomeAssistantConfigStore secretStore(secretBackend);
  studio::DeviceRegistry registry;
  studio::InstanceId id = studio::kInvalidInstanceId;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(registry.add(studio::DriverId::HomeAssistant,
                                    "Key Light", 4, id)));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(registry.configureHomeAssistant(
          id, studio::HomeAssistantDomain::Light, "light.key_light")));
  TEST_ASSERT_TRUE(deviceStore.save(registry));

  studio::HomeAssistantConfig config;
  config.configured = true;
  std::strcpy(config.wifiSsid, "StudioNet");
  std::strcpy(config.wifiPassword, "wifi-secret");
  std::strcpy(config.baseUrl, "http://homeassistant.local:8123");
  std::strcpy(config.token, "long-lived-secret-token");
  TEST_ASSERT_TRUE(secretStore.save(config));
  TEST_ASSERT_FALSE(deviceBackend.containsText("wifi-secret"));
  TEST_ASSERT_FALSE(deviceBackend.containsText("long-lived-secret-token"));

  studio::HomeAssistantConfig restored;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::ConfigLoadStatus::Loaded),
      static_cast<int>(secretStore.load(restored)));
  TEST_ASSERT_EQUAL_STRING(config.wifiSsid, restored.wifiSsid);
  TEST_ASSERT_EQUAL_STRING(config.baseUrl, restored.baseUrl);
  TEST_ASSERT_EQUAL_STRING(config.token, restored.token);
  secretBackend.corruptLastByte();
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::ConfigLoadStatus::Corrupt),
      static_cast<int>(secretStore.load(restored)));
  TEST_ASSERT_FALSE(studio::validLocalHomeAssistantUrl("https://ha.local"));
  TEST_ASSERT_FALSE(studio::validLocalHomeAssistantUrl("ws://ha.local"));
  TEST_ASSERT_TRUE(studio::validLocalHomeAssistantUrl("http://10.0.0.5:8123"));
}

void test_panel_identity_uses_full_hardware_id_and_short_setup_suffix() {
  char identity[studio::kPanelIdentityCapacity] = "";
  char setupSsid[studio::kPanelSetupSsidCapacity] = "";
  char otherSetupSsid[studio::kPanelSetupSsidCapacity] = "";
  studio::formatPanelIdentity(0x0010A1B2C3D4E5F6ULL, identity);
  studio::formatPanelSetupSsid(0x0010A1B2C3D4E5F6ULL, setupSsid);
  TEST_ASSERT_EQUAL_STRING("BLP-A1B2C3D4E5F6", identity);
  TEST_ASSERT_EQUAL_STRING("Bleep-Setup-4E5F6", setupSsid);
  studio::formatPanelSetupSsid(0x0010A1B2C3D5E6F7ULL, otherSetupSsid);
  TEST_ASSERT_EQUAL_STRING("Bleep-Setup-5E6F7", otherSetupSsid);
  TEST_ASSERT_NOT_EQUAL(0, std::strcmp(setupSsid, otherSetupSsid));

  studio::formatPanelIdentity(0, identity);
  studio::formatPanelSetupSsid(0, setupSsid);
  TEST_ASSERT_EQUAL_STRING("BLP-000000000000", identity);
  TEST_ASSERT_EQUAL_STRING("Bleep-Setup-00000", setupSsid);

  const uint64_t firstPanel =
      studio::canonicalPanelHardwareId(0x00002C19C03B0010ULL);
  const uint64_t secondPanel =
      studio::canonicalPanelHardwareId(0x0000A8DBC23B0010ULL);
  TEST_ASSERT_EQUAL_UINT64(0x000010003BC0192CULL, firstPanel);
  TEST_ASSERT_EQUAL_UINT64(0x000010003BC2DBA8ULL, secondPanel);
  studio::formatPanelIdentity(firstPanel, identity);
  studio::formatPanelSetupSsid(firstPanel, setupSsid);
  studio::formatPanelSetupSsid(secondPanel, otherSetupSsid);
  TEST_ASSERT_EQUAL_STRING("BLP-10003BC0192C", identity);
  TEST_ASSERT_EQUAL_STRING("Bleep-Setup-0192C", setupSsid);
  TEST_ASSERT_EQUAL_STRING("Bleep-Setup-2DBA8", otherSetupSsid);
  TEST_ASSERT_NOT_EQUAL(0, std::strcmp(setupSsid, otherSetupSsid));
}

void test_portal_parses_sequence_action_commands_from_json() {
  const char* payload = R"JSON({"start":[{"kind":"action","target_id":11,"command":"turn_on","value0":0,"value1":0,"value2":0,"label":"Turn on"},{"kind":"wait","wait_ms":200},{"kind":"action","target_id":12,"command":"record_start","value0":0,"value1":0,"value2":0,"label":"Record start"},{"kind":"action","target_id":10,"command":"record_start","value0":0,"value1":0,"value2":0,"label":"Record start"},{"kind":"wait","wait_ms":200},{"kind":"action","target_id":13,"command":"record_start","value0":0,"value1":0,"value2":0,"label":"Record start"}]})JSON";
  JsonDocument request;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(DeserializationError::Ok),
      static_cast<int>(deserializeJson(request, payload).code()));
  studio::SceneStep steps[CONFIG_MAX_SCENE_STEPS] = {};
  uint8_t count = 0;
  const portal::StepParseResult result =
      portal::parseSceneSteps(request["start"], steps, count);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(portal::StepParseStatus::Ok),
                        static_cast<int>(result.status));
  TEST_ASSERT_EQUAL_UINT8(6, count);
  TEST_ASSERT_EQUAL_UINT32(11, steps[0].targetId);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::CommandType::TurnOn),
                        static_cast<int>(steps[0].command));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::SceneStepType::Wait),
                        static_cast<int>(steps[1].type));
  TEST_ASSERT_EQUAL_UINT32(200, steps[1].waitMs);
  TEST_ASSERT_EQUAL_UINT32(12, steps[2].targetId);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::CommandType::RecordStart),
                        static_cast<int>(steps[2].command));
  TEST_ASSERT_EQUAL_UINT32(10, steps[3].targetId);
  TEST_ASSERT_EQUAL_UINT32(13, steps[5].targetId);

  request["start"][2]["command"] = "wrong_params";
  const portal::StepParseResult invalid =
      portal::parseSceneSteps(request["start"], steps, count);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(portal::StepParseStatus::InvalidCommand),
      static_cast<int>(invalid.status));
  TEST_ASSERT_EQUAL_UINT8(2, invalid.index);
}

void test_panel_settings_default_round_trip_corruption_and_rollback() {
  MemoryBackend backend;
  studio::PanelSettingsStore store(backend);
  studio::PanelSettings settings;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::ConfigLoadStatus::Missing),
      static_cast<int>(store.load(settings)));
  TEST_ASSERT_TRUE(settings.hapticEnabled);

  const uint8_t v1Panel[] = {
      0x50, 0x53, 0x45, 0x54, 0x01, 0x00, 0x00, 0xbc, 0x50, 0xbb, 0xd7};
  MemoryBackend v1Backend;
  v1Backend.seed(v1Panel, sizeof(v1Panel));
  studio::PanelSettingsStore v1Store(v1Backend);
  studio::PanelSettings migrated;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::ConfigLoadStatus::Loaded),
      static_cast<int>(v1Store.load(migrated)));
  TEST_ASSERT_FALSE(migrated.hapticEnabled);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::FirmwareUpdateChannel::Stable),
                        static_cast<int>(migrated.firmwareUpdateChannel));

  studio::PanelSettingsService service(backend);
  TEST_ASSERT_TRUE(service.begin());
  TEST_ASSERT_TRUE(service.get().hapticEnabled);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::FirmwareUpdateChannel::Stable),
      static_cast<int>(service.get().firmwareUpdateChannel));
  TEST_ASSERT_TRUE(service.setHapticEnabled(false));
  TEST_ASSERT_FALSE(service.get().hapticEnabled);
  TEST_ASSERT_TRUE(service.setFirmwareUpdateChannel(
      studio::FirmwareUpdateChannel::Development));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::FirmwareUpdateChannel::Development),
      static_cast<int>(service.get().firmwareUpdateChannel));

  studio::PanelSettings restored;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::ConfigLoadStatus::Loaded),
      static_cast<int>(store.load(restored)));
  TEST_ASSERT_FALSE(restored.hapticEnabled);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::FirmwareUpdateChannel::Development),
      static_cast<int>(restored.firmwareUpdateChannel));

  backend.failWrites = true;
  TEST_ASSERT_FALSE(service.setHapticEnabled(true));
  TEST_ASSERT_FALSE(service.get().hapticEnabled);
  backend.failWrites = false;
  backend.corruptLastByte();
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::ConfigLoadStatus::Corrupt),
      static_cast<int>(store.load(restored)));
  TEST_ASSERT_TRUE(restored.hapticEnabled);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::FirmwareUpdateChannel::Stable),
                        static_cast<int>(restored.firmwareUpdateChannel));
}

 void test_home_assistant_profiles_protocol_capacity_and_scene_validation() {
  MemoryBackend deviceBackend;
  MemoryBackend sceneBackend;
  FakeDriver haDriver(studio::DriverId::HomeAssistant);
  studio::DeviceDriver* drivers[] = {&haDriver};
  studio::DeviceManager devices(deviceBackend, drivers, 1);
  TEST_ASSERT_TRUE(devices.begin());

  const studio::HomeAssistantDomain domains[] = {
      studio::HomeAssistantDomain::Light,
      studio::HomeAssistantDomain::InputBoolean,
      studio::HomeAssistantDomain::Button,
      studio::HomeAssistantDomain::Scene};
  const char* ids[] = {"light.key", "input_boolean.live", "button.slate",
                       "scene.ready"};
  studio::InstanceId entityIds[4] = {};
  for (size_t i = 0; i < 4; ++i) {
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(studio::RegistryStatus::Ok),
        static_cast<int>(devices.addHomeAssistantEntity(
            domains[i], ids[i], ids[i], entityIds[i])));
  }
  studio::InstanceId overflow = studio::kInvalidInstanceId;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::DuplicateDriver),
      static_cast<int>(devices.addHomeAssistantEntity(
          studio::HomeAssistantDomain::Script, "script.extra", "Extra",
          overflow)));

  const studio::InstanceProfile light = devices.profile(entityIds[0]);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::DeviceType::Light),
                        static_cast<int>(light.type));
  TEST_ASSERT_BITS_HIGH(
      studio::capabilityBit(studio::Capability::TurnOn) |
          studio::capabilityBit(studio::Capability::TurnOff),
      light.capabilities);
  const studio::InstanceProfile inputBoolean = devices.profile(entityIds[1]);
  TEST_ASSERT_BITS_HIGH(
      studio::capabilityBit(studio::Capability::TurnOn) |
          studio::capabilityBit(studio::Capability::TurnOff),
      inputBoolean.capabilities);
  const studio::InstanceProfile button = devices.profile(entityIds[2]);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::DeviceType::Action),
                        static_cast<int>(button.type));
  TEST_ASSERT_BITS_HIGH(studio::capabilityBit(studio::Capability::Press),
                        button.capabilities);
  TEST_ASSERT_BITS_LOW(studio::capabilityBit(studio::Capability::Activate),
                       button.capabilities);

  TEST_ASSERT_TRUE(home_assistant::supportedEntityId("input_boolean.live"));
  TEST_ASSERT_FALSE(home_assistant::supportedEntityId("sensor.temperature"));
  TEST_ASSERT_TRUE(home_assistant::matchesEntitySearch(
      "light.key_light", "Key Light", "KEY LIGHT"));
  TEST_ASSERT_TRUE(home_assistant::matchesEntitySearch(
      "light.key_light", "Key Light", "LIGHT.KEY"));
  TEST_ASSERT_TRUE(home_assistant::matchesEntitySearch(
      "light.key_light", "KEY LIGHT", "key light"));
  TEST_ASSERT_TRUE(home_assistant::matchesEntitySearch(
      "light.key_light", nullptr, ""));
  TEST_ASSERT_FALSE(home_assistant::matchesEntitySearch(
      "light.key_light", "Key Light", "camera"));
  TEST_ASSERT_TRUE(home_assistant::commandSupported(
      studio::HomeAssistantDomain::Scene, studio::CommandType::Activate));
  TEST_ASSERT_FALSE(home_assistant::commandSupported(
      studio::HomeAssistantDomain::Scene, studio::CommandType::TurnOn));
  char payload[256];
  TEST_ASSERT_TRUE(home_assistant::buildAuth(payload, sizeof(payload), "token"));
  TEST_ASSERT_EQUAL_STRING(
      "{\"type\":\"auth\",\"access_token\":\"token\"}", payload);
  TEST_ASSERT_TRUE(home_assistant::buildServiceCall(
      payload, sizeof(payload), 17, studio::HomeAssistantDomain::Button,
      studio::CommandType::Press, "button.slate"));
  TEST_ASSERT_NOT_NULL(std::strstr(payload, "\"service\":\"press\""));
  TEST_ASSERT_NOT_NULL(std::strstr(payload, "\"entity_id\":\"button.slate\""));
  TEST_ASSERT_TRUE(home_assistant::buildServiceCall(
      payload, sizeof(payload), 18, studio::HomeAssistantDomain::InputBoolean,
      studio::CommandType::TurnOn, "input_boolean.live"));
  TEST_ASSERT_NOT_NULL(std::strstr(payload, "\"domain\":\"input_boolean\""));
  TEST_ASSERT_NOT_NULL(std::strstr(payload, "\"service\":\"turn_on\""));
  TEST_ASSERT_TRUE(home_assistant::buildServiceCall(
      payload, sizeof(payload), 19, studio::HomeAssistantDomain::InputBoolean,
      studio::CommandType::TurnOff, "input_boolean.live"));
  TEST_ASSERT_NOT_NULL(std::strstr(payload, "\"service\":\"turn_off\""));

  studio::SceneService scenes(sceneBackend, devices);
  TEST_ASSERT_TRUE(scenes.begin());
  studio::SceneRecord scene;
  std::strcpy(scene.name, "HA actions");
  scene.startCount = 3;
  scene.startSteps[0] =
      studio::makeActionStep(entityIds[0], studio::CommandType::TurnOn);
  scene.startSteps[1] =
      studio::makeActionStep(entityIds[2], studio::CommandType::Press);
  scene.startSteps[2] =
      studio::makeActionStep(entityIds[1], studio::CommandType::TurnOn);
  scene.stopCount = 2;
  scene.stopSteps[0] =
      studio::makeActionStep(entityIds[0], studio::CommandType::TurnOff);
  scene.stopSteps[1] =
      studio::makeActionStep(entityIds[1], studio::CommandType::TurnOff);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::SceneValidationStatus::Ok),
                        static_cast<int>(scenes.validate(scene)));
  scene.startSteps[1] =
      studio::makeActionStep(entityIds[3], studio::CommandType::TurnOn);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::SceneValidationStatus::MissingCapability),
      static_cast<int>(scenes.validate(scene)));
}

void test_active_instance_pool_rejects_invalid_instance_id() {
  studio::ActiveInstancePool pool;
  TEST_ASSERT_FALSE(
      pool.add(studio::kInvalidInstanceId, studio::ConnectionOwner::Foreground));
  TEST_ASSERT_EQUAL_UINT32(0, pool.count());

  pool.remove(studio::kInvalidInstanceId);
  TEST_ASSERT_EQUAL_UINT32(0, pool.count());

  TEST_ASSERT_TRUE(pool.add(7, studio::ConnectionOwner::Foreground));
  TEST_ASSERT_EQUAL_UINT32(1, pool.count());
  pool.remove(studio::kInvalidInstanceId);
  TEST_ASSERT_EQUAL_UINT32(1, pool.count());
  TEST_ASSERT_TRUE(pool.contains(7));
}

void test_transact_rolls_back_registry_on_mutation_failure() {
  MemoryBackend backend;
  studio::DeviceConfiguration configuration(backend);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::ConfigLoadStatus::Missing),
                        static_cast<int>(configuration.load()));
  TEST_ASSERT_TRUE(configuration.initializeEmpty());

  studio::InstanceId id = studio::kInvalidInstanceId;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(configuration.registry().add(
          studio::DriverId::CanonBle, "Camera", 1, id)));
  TEST_ASSERT_TRUE(configuration.save());

  // A failing mutation must leave the in-memory registry untouched so it stays
  // byte-identical to the durable blob.
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Invalid),
      static_cast<int>(configuration.transact([id](studio::DeviceRegistry& reg) {
        reg.rename(id, "Renamed In Failure");
        return studio::RegistryStatus::Invalid;
      })));
  const studio::DeviceRecord* record = configuration.registry().find(id);
  TEST_ASSERT_NOT_NULL(record);
  TEST_ASSERT_EQUAL_STRING("Camera", record->displayName);
}
