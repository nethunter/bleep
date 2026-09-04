void test_mixed_scene_activates_physical_transport_before_home_assistant() {
  MemoryBackend deviceBackend;
  MemoryBackend sceneBackend;
  FakeDriver physicalDriver(studio::DriverId::CanonBle);
  FakeDriver homeAssistantDriver(studio::DriverId::HomeAssistant);
  FakeDriver retainedLightDriver(studio::DriverId::AputureLight);
  int activationSequence = 0;
  physicalDriver.activationSequence = &activationSequence;
  homeAssistantDriver.activationSequence = &activationSequence;
  studio::DeviceDriver* drivers[] = {&physicalDriver, &homeAssistantDriver,
                                     &retainedLightDriver};
  studio::DeviceManager devices(deviceBackend, drivers, 3);
  TEST_ASSERT_TRUE(devices.begin());

  studio::InstanceId cameraId = studio::kInvalidInstanceId;
  studio::InstanceId helperId = studio::kInvalidInstanceId;
  studio::InstanceId lightId = studio::kInvalidInstanceId;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(devices.add(studio::DriverId::CanonBle, "Camera",
                                   cameraId)));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(devices.addHomeAssistantEntity(
          studio::HomeAssistantDomain::InputBoolean, "input_boolean.live",
          "Live", helperId)));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(devices.add(studio::DriverId::AputureLight, "Old light",
                                   lightId)));

  studio::SceneService scenes(sceneBackend, devices);
  TEST_ASSERT_TRUE(scenes.begin());
  studio::SceneId sceneId = studio::kInvalidSceneId;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::SceneRegistryStatus::Ok),
      static_cast<int>(scenes.add("Mixed order", sceneId)));
  studio::SceneRecord record = *scenes.find(sceneId);
  record.startCount = 2;
  // Authored HA-first order must not dictate transport initialization order.
  record.startSteps[0] =
      studio::makeActionStep(helperId, studio::CommandType::TurnOn);
  record.startSteps[1] =
      studio::makeActionStep(cameraId, studio::CommandType::RecordStart);
  record.stopCount = 2;
  record.stopSteps[0] =
      studio::makeActionStep(helperId, studio::CommandType::TurnOff);
  record.stopSteps[1] =
      studio::makeActionStep(cameraId, studio::CommandType::RecordStop);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::SceneRegistryStatus::Ok),
      static_cast<int>(scenes.replace(record)));

  // Reproduce navigation from a retained HA entity screen into the sequence.
  // Because HA is a target, it stays pooled while the physical target starts.
  TEST_ASSERT_TRUE(
      devices.acquire(helperId, studio::ConnectionOwner::Foreground));
  devices.release(helperId, studio::ConnectionOwner::Foreground);
  TEST_ASSERT_TRUE(devices.isActive(helperId));
  // Reproduce switching from a protocol-ready lights scene. It is not a target
  // of the mixed studio scene, but capacity remains and the pool retains it.
  TEST_ASSERT_TRUE(
      devices.acquire(lightId, studio::ConnectionOwner::Sequence));
  devices.loop();
  devices.release(lightId, studio::ConnectionOwner::Sequence);
  TEST_ASSERT_TRUE(devices.isRetained(lightId));
  activationSequence = 0;
  physicalDriver.firstActivationOrder = 0;
  homeAssistantDriver.firstActivationOrder = 0;
  physicalDriver.ready = false;

  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::SceneRunStatus::Ok),
                        static_cast<int>(scenes.prepare(sceneId)));
  TEST_ASSERT_EQUAL_INT(0, homeAssistantDriver.deactivationCount);
  TEST_ASSERT_EQUAL_INT(0, retainedLightDriver.deactivationCount);
  TEST_ASSERT_TRUE(devices.isActive(lightId));
  TEST_ASSERT_EQUAL_INT(0, physicalDriver.deactivationCount);
  TEST_ASSERT_EQUAL_INT(1, physicalDriver.firstActivationOrder);
  TEST_ASSERT_EQUAL_INT(0, homeAssistantDriver.firstActivationOrder);
  scenes.loop(1);
  TEST_ASSERT_EQUAL_INT(0, homeAssistantDriver.firstActivationOrder);
  scenes.loop(1000);
  TEST_ASSERT_EQUAL_INT(0, homeAssistantDriver.firstActivationOrder);
  physicalDriver.ready = true;
  scenes.loop(1001);
  TEST_ASSERT_EQUAL_INT(0, homeAssistantDriver.firstActivationOrder);
  TEST_ASSERT_EQUAL_INT(0, homeAssistantDriver.firstActivationOrder);
  TEST_ASSERT_TRUE(
      devices.ownedBy(helperId, studio::ConnectionOwner::Sequence));
  scenes.cancel();
}

void test_scene_switch_retains_old_only_links_when_four_resources_fit() {
  MemoryBackend deviceBackend;
  MemoryBackend sceneBackend;
  FakeDriver cameraDriver(studio::DriverId::CanonBle);
  FakeDriver recorderDriver(studio::DriverId::TascamX8);
  FakeDriver lightDriver(studio::DriverId::ZhiyunLight);
  FakeDriver homeAssistantDriver(studio::DriverId::HomeAssistant);
  studio::DeviceDriver* drivers[] = {&cameraDriver, &recorderDriver,
                                     &lightDriver, &homeAssistantDriver};
  studio::DeviceManager devices(deviceBackend, drivers, 4);
  TEST_ASSERT_TRUE(devices.begin());

  studio::InstanceId cameraId = studio::kInvalidInstanceId;
  studio::InstanceId recorderId = studio::kInvalidInstanceId;
  studio::InstanceId lightId = studio::kInvalidInstanceId;
  studio::InstanceId helperId = studio::kInvalidInstanceId;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(devices.add(studio::DriverId::CanonBle, "Camera",
                                   cameraId)));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(devices.add(studio::DriverId::TascamX8, "Recorder",
                                   recorderId)));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(devices.add(studio::DriverId::ZhiyunLight, "Light",
                                   lightId)));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(devices.addHomeAssistantEntity(
          studio::HomeAssistantDomain::InputBoolean, "input_boolean.live",
          "Live", helperId)));

  studio::SceneService scenes(sceneBackend, devices);
  TEST_ASSERT_TRUE(scenes.begin());
  studio::SceneId firstId = studio::kInvalidSceneId;
  studio::SceneId secondId = studio::kInvalidSceneId;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::SceneRegistryStatus::Ok),
      static_cast<int>(scenes.add("First", firstId)));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::SceneRegistryStatus::Ok),
      static_cast<int>(scenes.add("Second", secondId)));
  studio::SceneRecord first = *scenes.find(firstId);
  first.startCount = 3;
  first.startSteps[0] =
      studio::makeActionStep(cameraId, studio::CommandType::RecordStart);
  first.startSteps[1] =
      studio::makeActionStep(recorderId, studio::CommandType::RecordStart);
  first.startSteps[2] =
      studio::makeActionStep(helperId, studio::CommandType::TurnOn);
  first.stopCount = 3;
  first.stopSteps[0] =
      studio::makeActionStep(cameraId, studio::CommandType::RecordStop);
  first.stopSteps[1] =
      studio::makeActionStep(recorderId, studio::CommandType::RecordStop);
  first.stopSteps[2] =
      studio::makeActionStep(helperId, studio::CommandType::TurnOff);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::SceneRegistryStatus::Ok),
      static_cast<int>(scenes.replace(first)));

  studio::SceneRecord second = *scenes.find(secondId);
  second.startCount = 2;
  second.startSteps[0] =
      studio::makeActionStep(lightId, studio::CommandType::TurnOn);
  second.startSteps[1] =
      studio::makeActionStep(helperId, studio::CommandType::TurnOn);
  second.stopCount = 2;
  second.stopSteps[0] =
      studio::makeActionStep(lightId, studio::CommandType::TurnOff);
  second.stopSteps[1] =
      studio::makeActionStep(helperId, studio::CommandType::TurnOff);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::SceneRegistryStatus::Ok),
      static_cast<int>(scenes.replace(second)));

  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::SceneRunStatus::Ok),
                        static_cast<int>(scenes.prepare(firstId)));
  scenes.loop(1);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::ScenePhase::Ready),
                        static_cast<int>(scenes.progress().phase));
  TEST_ASSERT_EQUAL_INT(1, cameraDriver.activationCount);
  TEST_ASSERT_EQUAL_INT(1, recorderDriver.activationCount);
  TEST_ASSERT_EQUAL_INT(0, lightDriver.activationCount);
  TEST_ASSERT_EQUAL_INT(1, homeAssistantDriver.activationCount);

  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::SceneRunStatus::Ok),
                        static_cast<int>(scenes.prepare(secondId)));
  TEST_ASSERT_EQUAL_INT(0, cameraDriver.deactivationCount);
  TEST_ASSERT_EQUAL_INT(0, recorderDriver.deactivationCount);
  TEST_ASSERT_EQUAL_INT(0, lightDriver.deactivationCount);
  TEST_ASSERT_EQUAL_INT(0, homeAssistantDriver.deactivationCount);
  TEST_ASSERT_EQUAL_INT(1, cameraDriver.activationCount);
  TEST_ASSERT_EQUAL_INT(1, recorderDriver.activationCount);
  TEST_ASSERT_EQUAL_INT(1, lightDriver.activationCount);
  TEST_ASSERT_EQUAL_INT(1, homeAssistantDriver.activationCount);
  scenes.loop(252);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::ScenePhase::Ready),
                        static_cast<int>(scenes.progress().phase));
  TEST_ASSERT_TRUE(
      devices.ownedBy(helperId, studio::ConnectionOwner::Sequence));
  TEST_ASSERT_EQUAL_UINT32(4, devices.activeCount());
  TEST_ASSERT_TRUE(devices.isActive(cameraId));
  TEST_ASSERT_TRUE(devices.isActive(recorderId));
  TEST_ASSERT_TRUE(devices.isActive(lightId));
  scenes.cancel();
}

void test_mixed_scene_gives_physical_and_home_assistant_separate_timeouts() {
  MemoryBackend deviceBackend;
  MemoryBackend sceneBackend;
  FakeDriver physicalDriver(studio::DriverId::CanonBle);
  FakeDriver homeAssistantDriver(studio::DriverId::HomeAssistant);
  physicalDriver.ready = false;
  homeAssistantDriver.ready = false;
  studio::DeviceDriver* drivers[] = {&physicalDriver, &homeAssistantDriver};
  studio::DeviceManager devices(deviceBackend, drivers, 2);
  TEST_ASSERT_TRUE(devices.begin());

  studio::InstanceId cameraId = studio::kInvalidInstanceId;
  studio::InstanceId helperId = studio::kInvalidInstanceId;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(devices.add(studio::DriverId::CanonBle, "Camera",
                                   cameraId)));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(devices.addHomeAssistantEntity(
          studio::HomeAssistantDomain::InputBoolean, "input_boolean.live",
          "Live", helperId)));

  studio::SceneService scenes(sceneBackend, devices);
  TEST_ASSERT_TRUE(scenes.begin());
  studio::SceneId sceneId = studio::kInvalidSceneId;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::SceneRegistryStatus::Ok),
      static_cast<int>(scenes.add("Cold mixed", sceneId)));
  studio::SceneRecord record = *scenes.find(sceneId);
  record.startCount = 2;
  record.startSteps[0] =
      studio::makeActionStep(cameraId, studio::CommandType::RecordStart);
  record.startSteps[1] =
      studio::makeActionStep(helperId, studio::CommandType::TurnOn);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::SceneRegistryStatus::Ok),
      static_cast<int>(scenes.replace(record)));

  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::SceneRunStatus::Ok),
                        static_cast<int>(scenes.prepare(sceneId)));
  scenes.loop(1);
  scenes.loop(CONFIG_SCENE_CONNECT_TIMEOUT_MS + 2);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::ScenePhase::Connecting),
                        static_cast<int>(scenes.progress().phase));

  constexpr uint32_t physicalReadyAt =
      CONFIG_SCENE_PHYSICAL_CONNECT_TIMEOUT_MS - 1000;
  physicalDriver.ready = true;
  scenes.loop(physicalReadyAt);
  scenes.loop(physicalReadyAt + 250);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::ScenePhase::Connecting),
                        static_cast<int>(scenes.progress().phase));
  TEST_ASSERT_TRUE(devices.isActive(helperId));

  scenes.loop(physicalReadyAt + CONFIG_SCENE_CONNECT_TIMEOUT_MS - 1);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::ScenePhase::Connecting),
                        static_cast<int>(scenes.progress().phase));
  scenes.loop(physicalReadyAt + CONFIG_SCENE_CONNECT_TIMEOUT_MS);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::ScenePhase::Failed),
                        static_cast<int>(scenes.progress().phase));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::SceneRunStatus::ConnectTimeout),
                        static_cast<int>(scenes.progress().lastStatus));
  scenes.cancel();
}

void test_prepared_scene_edit_preserves_shared_ha_while_adding_ble_target() {
  MemoryBackend deviceBackend;
  MemoryBackend sceneBackend;
  FakeDriver physicalDriver(studio::DriverId::CanonBle);
  FakeDriver homeAssistantDriver(studio::DriverId::HomeAssistant);
  int activationSequence = 0;
  physicalDriver.activationSequence = &activationSequence;
  homeAssistantDriver.activationSequence = &activationSequence;
  studio::DeviceDriver* drivers[] = {&physicalDriver, &homeAssistantDriver};
  studio::DeviceManager devices(deviceBackend, drivers, 2);
  TEST_ASSERT_TRUE(devices.begin());

  studio::InstanceId cameraId = studio::kInvalidInstanceId;
  studio::InstanceId helperId = studio::kInvalidInstanceId;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(devices.add(studio::DriverId::CanonBle, "Camera",
                                   cameraId)));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(devices.addHomeAssistantEntity(
          studio::HomeAssistantDomain::InputBoolean, "input_boolean.live",
          "Live", helperId)));

  studio::SceneService scenes(sceneBackend, devices);
  TEST_ASSERT_TRUE(scenes.begin());
  studio::SceneId sceneId = studio::kInvalidSceneId;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::SceneRegistryStatus::Ok),
      static_cast<int>(scenes.add("Editable", sceneId)));
  studio::SceneRecord record = *scenes.find(sceneId);
  record.startCount = 1;
  record.startSteps[0] =
      studio::makeActionStep(helperId, studio::CommandType::TurnOn);
  record.stopCount = 1;
  record.stopSteps[0] =
      studio::makeActionStep(helperId, studio::CommandType::TurnOff);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::SceneRegistryStatus::Ok),
      static_cast<int>(scenes.replace(record)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::SceneRunStatus::Ok),
                        static_cast<int>(scenes.prepare(sceneId)));
  scenes.loop(1);
  TEST_ASSERT_TRUE(
      devices.ownedBy(helperId, studio::ConnectionOwner::Sequence));

  activationSequence = 0;
  physicalDriver.firstActivationOrder = 0;
  homeAssistantDriver.firstActivationOrder = 0;
  record.startCount = 2;
  record.startSteps[1] =
      studio::makeActionStep(cameraId, studio::CommandType::RecordStart);
  record.stopCount = 2;
  record.stopSteps[1] =
      studio::makeActionStep(cameraId, studio::CommandType::RecordStop);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::SceneRegistryStatus::Ok),
      static_cast<int>(scenes.replace(record)));

  TEST_ASSERT_EQUAL_INT(0, homeAssistantDriver.deactivationCount);
  TEST_ASSERT_EQUAL_INT(1, physicalDriver.firstActivationOrder);
  TEST_ASSERT_EQUAL_INT(0, homeAssistantDriver.firstActivationOrder);
  TEST_ASSERT_TRUE(
      devices.ownedBy(cameraId, studio::ConnectionOwner::Sequence));
  TEST_ASSERT_TRUE(
      devices.ownedBy(helperId, studio::ConnectionOwner::Sequence));
  scenes.loop(2);
  TEST_ASSERT_EQUAL_INT(0, homeAssistantDriver.firstActivationOrder);
  scenes.loop(252);
  TEST_ASSERT_EQUAL_INT(0, homeAssistantDriver.firstActivationOrder);
  TEST_ASSERT_TRUE(
      devices.ownedBy(helperId, studio::ConnectionOwner::Sequence));
  scenes.cancel();
}

 void test_manager_routes_commands_and_blocks_disabled_device() {
  MemoryBackend backend;
  FakeDriver driver;
  studio::DeviceDriver* drivers[] = {&driver};
  studio::DeviceManager manager(backend, drivers, 1);
  TEST_ASSERT_TRUE(manager.begin());
  studio::InstanceId instanceId = studio::kInvalidInstanceId;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(manager.add(studio::DriverId::SharkNanoII,
                                   "Main Slider", instanceId)));
  TEST_ASSERT_TRUE(manager.acquire(instanceId, studio::ConnectionOwner::Foreground));
  TEST_ASSERT_EQUAL_INT(1, driver.activationCount);

  studio::DeviceCommand command;
  command.instanceId = instanceId;
  command.type = studio::CommandType::Refresh;
  TEST_ASSERT_TRUE(manager.enqueue(command));
  manager.loop();

  studio::CommandResult result;
  TEST_ASSERT_TRUE(manager.popResult(result));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::CommandStatus::Succeeded),
                        static_cast<int>(result.status));
  TEST_ASSERT_EQUAL_INT(1, driver.dispatchCount);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::CommandType::Refresh),
                        static_cast<int>(driver.lastCommand));

  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(manager.setEnabled(instanceId, false)));
  TEST_ASSERT_FALSE(driver.active);
  TEST_ASSERT_TRUE(manager.enqueue(command));
  manager.loop();
  TEST_ASSERT_TRUE(manager.popResult(result));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::CommandStatus::Disabled),
                        static_cast<int>(result.status));
}

void test_manager_uses_per_instance_driver_capabilities() {
  MemoryBackend backend;
  FakeDriver zhiyun(studio::DriverId::ZhiyunLight);
  zhiyun.capabilityClearMask =
      studio::capabilityBit(studio::Capability::SetLightRgb);
  studio::DeviceDriver* drivers[] = {&zhiyun};
  studio::DeviceManager manager(backend, drivers, 1);
  TEST_ASSERT_TRUE(manager.begin());
  studio::InstanceId id = studio::kInvalidInstanceId;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(manager.add(studio::DriverId::ZhiyunLight,
                                   "MOLUS X100", id)));
  const studio::InstanceProfile profile = manager.profile(id);
  TEST_ASSERT_BITS_HIGH(
      studio::capabilityBit(studio::Capability::SetLightCct),
      profile.capabilities);
  TEST_ASSERT_BITS_LOW(
      studio::capabilityBit(studio::Capability::SetLightRgb),
      profile.capabilities);
}

void test_manager_keeps_latest_tracked_result_when_result_queue_is_full() {
  MemoryBackend backend;
  FakeDriver driver;
  studio::DeviceDriver* drivers[] = {&driver};
  studio::DeviceManager manager(backend, drivers, 1);
  TEST_ASSERT_TRUE(manager.begin());
  studio::InstanceId instanceId = studio::kInvalidInstanceId;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(manager.add(studio::DriverId::SharkNanoII,
                                   "Main Slider", instanceId)));
  TEST_ASSERT_TRUE(
      manager.acquire(instanceId, studio::ConnectionOwner::Foreground));

  studio::DeviceCommand command;
  command.instanceId = instanceId;
  command.type = studio::CommandType::Refresh;
  for (size_t i = 0; i < CONFIG_DEVICE_COMMAND_QUEUE_SIZE; ++i) {
    TEST_ASSERT_TRUE(manager.enqueue(command));
    manager.loop();
  }

  uint32_t trackedRequestId = 0;
  TEST_ASSERT_TRUE(manager.enqueue(command, &trackedRequestId));
  manager.loop();
  studio::CommandResult result;
  TEST_ASSERT_TRUE(manager.takeResult(trackedRequestId, result));
  TEST_ASSERT_EQUAL_UINT32(trackedRequestId, result.requestId);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::CommandStatus::Succeeded),
                        static_cast<int>(result.status));
}

void test_manager_cancels_only_the_matching_dispatched_request() {
  MemoryBackend backend;
  FakeDriver driver;
  driver.pendingCommand = studio::CommandType::Refresh;
  studio::DeviceDriver* drivers[] = {&driver};
  studio::DeviceManager manager(backend, drivers, 1);
  TEST_ASSERT_TRUE(manager.begin());
  studio::InstanceId instanceId = studio::kInvalidInstanceId;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(manager.add(studio::DriverId::SharkNanoII,
                                   "Main Slider", instanceId)));
  TEST_ASSERT_TRUE(
      manager.acquire(instanceId, studio::ConnectionOwner::Foreground));

  studio::DeviceCommand command;
  command.instanceId = instanceId;
  command.type = studio::CommandType::Refresh;
  uint32_t dispatchedRequest = 0;
  TEST_ASSERT_TRUE(manager.enqueue(command, &dispatchedRequest));
  manager.loop();
  TEST_ASSERT_TRUE(driver.commandPending);

  uint32_t queuedRequest = 0;
  TEST_ASSERT_TRUE(manager.enqueue(command, &queuedRequest));
  TEST_ASSERT_TRUE(manager.cancelCommand(queuedRequest, instanceId));
  TEST_ASSERT_EQUAL_INT(0, driver.cancelPendingCount);
  TEST_ASSERT_TRUE(driver.commandPending);

  TEST_ASSERT_TRUE(manager.cancelCommand(dispatchedRequest, instanceId));
  TEST_ASSERT_EQUAL_INT(1, driver.cancelPendingCount);
  TEST_ASSERT_FALSE(driver.commandPending);
}

void test_manager_routes_to_canon_driver() {
  MemoryBackend backend;
  FakeDriver sharkDriver;
  FakeDriver canonDriver(studio::DriverId::CanonBle);
  FakeDriver triggerDriver(studio::DriverId::CanonTrigger);
  studio::DeviceDriver* drivers[] = {&sharkDriver, &canonDriver, &triggerDriver};
  studio::DeviceManager manager(backend, drivers, 3);
  TEST_ASSERT_TRUE(manager.begin());

  studio::InstanceId canonId = studio::kInvalidInstanceId;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(
          manager.add(studio::DriverId::CanonBle, "R6 Mark III", canonId)));
  TEST_ASSERT_TRUE(manager.acquire(canonId, studio::ConnectionOwner::Foreground));
  TEST_ASSERT_EQUAL_INT(0, sharkDriver.activationCount);
  TEST_ASSERT_EQUAL_INT(1, canonDriver.activationCount);
  TEST_ASSERT_EQUAL_INT(0, triggerDriver.activationCount);

  studio::DeviceCommand command;
  command.instanceId = canonId;
  command.type = studio::CommandType::RecordStart;
  TEST_ASSERT_TRUE(manager.enqueue(command));
  manager.loop();
  studio::CommandResult result;
  TEST_ASSERT_TRUE(manager.popResult(result));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::CommandStatus::Succeeded),
                        static_cast<int>(result.status));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::CommandType::RecordStart),
      static_cast<int>(canonDriver.lastCommand));
  command.type = studio::CommandType::RecordStop;
  TEST_ASSERT_TRUE(manager.enqueue(command));
  manager.loop();
  TEST_ASSERT_TRUE(manager.popResult(result));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::CommandType::RecordStop),
      static_cast<int>(canonDriver.lastCommand));
  command.type = studio::CommandType::CameraPowerOff;
  TEST_ASSERT_TRUE(manager.enqueue(command));
  manager.loop();
  TEST_ASSERT_TRUE(manager.popResult(result));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::CommandType::CameraPowerOff),
      static_cast<int>(canonDriver.lastCommand));
  command.type = studio::CommandType::CameraPowerOn;
  TEST_ASSERT_TRUE(manager.enqueue(command));
  manager.loop();
  TEST_ASSERT_TRUE(manager.popResult(result));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::CommandType::CameraPowerOn),
      static_cast<int>(canonDriver.lastCommand));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(manager.clearPairing(canonId)));
  TEST_ASSERT_EQUAL_INT(1, canonDriver.forgetPairingCount);
  TEST_ASSERT_EQUAL_INT(0, sharkDriver.forgetPairingCount);
  TEST_ASSERT_EQUAL_INT(0, triggerDriver.forgetPairingCount);

  studio::InstanceId triggerId = studio::kInvalidInstanceId;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(manager.add(studio::DriverId::CanonTrigger,
                                   "R6 Trigger", triggerId)));
  manager.release(canonId, studio::ConnectionOwner::Foreground);
  TEST_ASSERT_TRUE(manager.acquire(triggerId, studio::ConnectionOwner::Foreground));
  TEST_ASSERT_EQUAL_INT(1, triggerDriver.activationCount);
  TEST_ASSERT_FALSE(canonDriver.active);
  command.instanceId = triggerId;
  command.type = studio::CommandType::RecordTrigger;
  TEST_ASSERT_TRUE(manager.enqueue(command));
  manager.loop();
  TEST_ASSERT_TRUE(manager.popResult(result));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::CommandStatus::Succeeded),
                        static_cast<int>(result.status));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::CommandType::RecordTrigger),
      static_cast<int>(triggerDriver.lastCommand));
}

void test_manager_routes_explicit_tascam_record_commands() {
  MemoryBackend backend;
  FakeDriver tascamDriver(studio::DriverId::TascamX8);
  studio::DeviceDriver* drivers[] = {&tascamDriver};
  studio::DeviceManager manager(backend, drivers, 1);
  TEST_ASSERT_TRUE(manager.begin());

  studio::InstanceId id = studio::kInvalidInstanceId;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(
          manager.add(studio::DriverId::TascamX8, "Recorder", id)));
  TEST_ASSERT_TRUE(manager.acquire(id, studio::ConnectionOwner::Foreground));

  studio::DeviceCommand command;
  command.instanceId = id;
  command.type = studio::CommandType::RecordStart;
  TEST_ASSERT_TRUE(manager.enqueue(command));
  manager.loop();
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::CommandType::RecordStart),
      static_cast<int>(tascamDriver.lastCommand));

  command.type = studio::CommandType::RecordStop;
  TEST_ASSERT_TRUE(manager.enqueue(command));
  manager.loop();
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::CommandType::RecordStop),
      static_cast<int>(tascamDriver.lastCommand));
}

void test_removed_registry_stays_empty_after_restart() {
  MemoryBackend backend;
  FakeDriver firstDriver;
  studio::DeviceDriver* firstDrivers[] = {&firstDriver};
  studio::DeviceManager first(backend, firstDrivers, 1);
  TEST_ASSERT_TRUE(first.begin());
  studio::InstanceId removedId = studio::kInvalidInstanceId;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(first.add(studio::DriverId::SharkNanoII,
                                 "Main Slider", removedId)));
  TEST_ASSERT_EQUAL_UINT32(1, first.count());
  TEST_ASSERT_TRUE(
      first.acquire(removedId, studio::ConnectionOwner::Foreground));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(first.remove(removedId)));
  TEST_ASSERT_FALSE(first.isActive(removedId));
  TEST_ASSERT_EQUAL_INT(1, firstDriver.deactivationCount);
  TEST_ASSERT_EQUAL_INT(1, firstDriver.forgetPairingCount);

  FakeDriver secondDriver;
  studio::DeviceDriver* secondDrivers[] = {&secondDriver};
  studio::DeviceManager second(backend, secondDrivers, 1);
  TEST_ASSERT_TRUE(second.begin());
  TEST_ASSERT_EQUAL_UINT32(0, second.count());
  TEST_ASSERT_EQUAL_INT(0, secondDriver.activationCount);
}

void test_admin_mutations_roll_back_when_persistence_fails() {
  MemoryBackend deviceBackend;
  MemoryBackend sceneBackend;
  FakeDriver driver(studio::DriverId::CanonBle);
  studio::DeviceDriver* drivers[] = {&driver};
  studio::DeviceManager devices(deviceBackend, drivers, 1);
  TEST_ASSERT_TRUE(devices.begin());

  studio::InstanceId cameraId = studio::kInvalidInstanceId;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(devices.add(studio::DriverId::CanonBle, "Camera",
                                   cameraId)));
  deviceBackend.failWrites = true;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Invalid),
      static_cast<int>(devices.update(cameraId, "Renamed", false)));
  TEST_ASSERT_EQUAL_STRING("Camera", devices.find(cameraId)->displayName);
  TEST_ASSERT_TRUE(devices.find(cameraId)->enabled);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Invalid),
      static_cast<int>(devices.remove(cameraId)));
  TEST_ASSERT_NOT_NULL(devices.find(cameraId));
  TEST_ASSERT_EQUAL_INT(0, driver.forgetPairingCount);

  studio::SceneService scenes(sceneBackend, devices);
  TEST_ASSERT_TRUE(scenes.begin());
  studio::SceneId sceneId = studio::kInvalidSceneId;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::SceneRegistryStatus::Ok),
      static_cast<int>(scenes.add("Take", sceneId)));
  studio::SceneRecord original = *scenes.find(sceneId);
  original.startCount = 1;
  original.startSteps[0] =
      studio::makeActionStep(cameraId, studio::CommandType::RecordStart);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::SceneRegistryStatus::Ok),
      static_cast<int>(scenes.replace(original)));

  sceneBackend.failWrites = true;
  studio::SceneId copyId = studio::kInvalidSceneId;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::SceneRegistryStatus::Invalid),
      static_cast<int>(scenes.duplicate(sceneId, "Take copy", copyId)));
  TEST_ASSERT_EQUAL(studio::kInvalidSceneId, copyId);
  TEST_ASSERT_EQUAL_UINT32(1, scenes.count());
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::SceneRegistryStatus::Invalid),
      static_cast<int>(scenes.rename(sceneId, "Changed")));
  TEST_ASSERT_EQUAL_STRING("Take", scenes.find(sceneId)->name);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::SceneRegistryStatus::Invalid),
      static_cast<int>(scenes.setEnabled(sceneId, false)));
  TEST_ASSERT_TRUE(scenes.find(sceneId)->enabled);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::SceneRegistryStatus::Invalid),
      static_cast<int>(scenes.remove(sceneId)));
  TEST_ASSERT_NOT_NULL(scenes.find(sceneId));
}

void test_manager_holds_concurrent_active_links() {
  MemoryBackend backend;
  FakeDriver canonDriver(studio::DriverId::CanonBle);
  FakeDriver tascamDriver(studio::DriverId::TascamX8);
  studio::DeviceDriver* drivers[] = {&canonDriver, &tascamDriver};
  studio::DeviceManager manager(backend, drivers, 2);
  TEST_ASSERT_TRUE(manager.begin());

  studio::InstanceId canonId = studio::kInvalidInstanceId;
  studio::InstanceId tascamId = studio::kInvalidInstanceId;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(
          manager.add(studio::DriverId::CanonBle, "Camera", canonId)));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(
          manager.add(studio::DriverId::TascamX8, "Recorder", tascamId)));
  TEST_ASSERT_TRUE(manager.acquire(canonId, studio::ConnectionOwner::Sequence));
  TEST_ASSERT_TRUE(manager.acquire(tascamId, studio::ConnectionOwner::Sequence));
  TEST_ASSERT_EQUAL_UINT32(2, manager.activeCount());
  TEST_ASSERT_TRUE(manager.isActive(canonId));
  TEST_ASSERT_TRUE(manager.isActive(tascamId));
  TEST_ASSERT_TRUE(canonDriver.active);
  TEST_ASSERT_TRUE(tascamDriver.active);

  studio::DeviceCommand command;
  command.instanceId = canonId;
  command.type = studio::CommandType::RecordStart;
  TEST_ASSERT_TRUE(manager.enqueue(command));
  manager.loop();
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::CommandType::RecordStart),
      static_cast<int>(canonDriver.lastCommand));
  command.instanceId = tascamId;
  command.type = studio::CommandType::RecordStart;
  TEST_ASSERT_TRUE(manager.enqueue(command));
  manager.loop();
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::CommandType::RecordStart),
      static_cast<int>(tascamDriver.lastCommand));

  manager.deactivateAll();
  TEST_ASSERT_EQUAL_UINT32(0, manager.activeCount());
  TEST_ASSERT_FALSE(canonDriver.active);
  TEST_ASSERT_FALSE(tascamDriver.active);
}

void test_manager_retains_ready_sessions_and_evicts_safe_lru() {
  MemoryBackend backend;
  FakeDriver sharkDriver;
  FakeDriver canonDriver(studio::DriverId::CanonBle);
  FakeDriver tascamDriver(studio::DriverId::TascamX8);
  FakeDriver homeAssistantDriver(studio::DriverId::HomeAssistant);
  studio::DeviceDriver* drivers[] = {&sharkDriver, &canonDriver, &tascamDriver,
                                     &homeAssistantDriver};
  studio::DeviceManager manager(backend, drivers, 4);
  TEST_ASSERT_TRUE(manager.begin());
  studio::InstanceId sharkId = studio::kInvalidInstanceId;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(manager.add(studio::DriverId::SharkNanoII,
                                   "Main Slider", sharkId)));
  studio::InstanceId canonIds[3] = {};
  studio::InstanceId tascamId = studio::kInvalidInstanceId;
  for (studio::InstanceId& id : canonIds) {
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(studio::RegistryStatus::Ok),
        static_cast<int>(manager.add(studio::DriverId::CanonBle, "Camera", id)));
  }
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(
          manager.add(studio::DriverId::TascamX8, "Recorder", tascamId)));
  studio::InstanceId haIds[4] = {};
  for (size_t i = 0; i < 4; ++i) {
    char entityId[32];
    std::snprintf(entityId, sizeof(entityId), "switch.test_%u",
                  static_cast<unsigned>(i));
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(studio::RegistryStatus::Ok),
        static_cast<int>(manager.addHomeAssistantEntity(
            studio::HomeAssistantDomain::Switch, entityId, "HA switch",
            haIds[i])));
  }

  const studio::InstanceId initial[] = {
      sharkId, canonIds[0], canonIds[1], tascamId,
      haIds[0], haIds[1], haIds[2], haIds[3]};
  for (studio::InstanceId id : initial) {
    TEST_ASSERT_TRUE(manager.acquire(id, studio::ConnectionOwner::Foreground));
    manager.loop();
    manager.release(id, studio::ConnectionOwner::Foreground);
    TEST_ASSERT_TRUE(manager.isRetained(id));
  }
  TEST_ASSERT_EQUAL_UINT32(CONFIG_MAX_ACTIVE_INSTANCES, manager.activeCount());
  TEST_ASSERT_EQUAL_UINT32(CONFIG_MAX_ACTIVE_LINKS, manager.bleSlotCount());

  // The oldest retained-idle session is evicted for a fifth device, while
  // multiple instances backed by the same driver remain active together.
  TEST_ASSERT_TRUE(
      manager.acquire(canonIds[2], studio::ConnectionOwner::Foreground));
  TEST_ASSERT_FALSE(manager.isActive(sharkId));
  TEST_ASSERT_TRUE(manager.isActive(canonIds[0]));
  TEST_ASSERT_TRUE(manager.isActive(canonIds[1]));
  TEST_ASSERT_TRUE(manager.isActive(canonIds[2]));
  TEST_ASSERT_EQUAL_INT(3, canonDriver.activationCount);
  manager.loop();
  manager.release(canonIds[2], studio::ConnectionOwner::Foreground);

  // Make the recorder oldest, then protect it with confirmed recording. The
  // next acquisition must evict the oldest safe camera instead.
  tascamDriver.recordingConfirmed = true;
  tascamDriver.recording = true;
  for (studio::InstanceId id : canonIds) {
    TEST_ASSERT_TRUE(manager.acquire(id, studio::ConnectionOwner::Foreground));
    manager.release(id, studio::ConnectionOwner::Foreground);
  }
  TEST_ASSERT_TRUE(
      manager.acquire(sharkId, studio::ConnectionOwner::Foreground));
  TEST_ASSERT_TRUE(manager.isActive(tascamId));
  TEST_ASSERT_FALSE(manager.isActive(canonIds[0]));
  TEST_ASSERT_TRUE(manager.isActive(haIds[0]));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::CommandStatus::ConfirmationRequired),
      static_cast<int>(manager.disconnect(tascamId)));
  TEST_ASSERT_TRUE(manager.isActive(tascamId));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::CommandStatus::Succeeded),
                        static_cast<int>(manager.disconnect(tascamId, true)));
  TEST_ASSERT_FALSE(manager.isActive(tascamId));
}

void test_manager_counts_shared_mesh_as_one_ble_slot() {
  MemoryBackend backend;
  FakeDriver sharkDriver;
  FakeDriver canonDriver(studio::DriverId::CanonBle);
  FakeDriver tascamDriver(studio::DriverId::TascamX8);
  FakeDriver meshDriver(studio::DriverId::AputureLight);
  FakeDriver zhiyunDriver(studio::DriverId::ZhiyunLight);
  meshDriver.sharedBleGroup = 1;
  meshDriver.sharedBleFamily = studio::DriverId::PanelOwnedMesh;
  zhiyunDriver.sharedBleGroup = 1;
  zhiyunDriver.sharedBleFamily = studio::DriverId::PanelOwnedMesh;
  studio::DeviceDriver* drivers[] = {
      &sharkDriver, &canonDriver, &tascamDriver, &meshDriver, &zhiyunDriver};
  studio::DeviceManager manager(backend, drivers, 5);
  TEST_ASSERT_TRUE(manager.begin());
  studio::InstanceId sharkId = studio::kInvalidInstanceId;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(manager.add(studio::DriverId::SharkNanoII,
                                   "Main Slider", sharkId)));
  studio::InstanceId canonId = studio::kInvalidInstanceId;
  studio::InstanceId tascamId = studio::kInvalidInstanceId;
  studio::InstanceId meshIds[2] = {};
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(
          manager.add(studio::DriverId::CanonBle, "Camera", canonId)));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(
          manager.add(studio::DriverId::TascamX8, "Recorder", tascamId)));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(manager.add(studio::DriverId::AputureLight,
                                   "Sidus mesh light", meshIds[0])));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(manager.add(studio::DriverId::ZhiyunLight,
                                   "Zhiyun mesh light", meshIds[1])));

  const studio::InstanceId initial[] = {
      meshIds[0], sharkId, canonId, tascamId};
  for (studio::InstanceId id : initial) {
    TEST_ASSERT_TRUE(manager.acquire(id, studio::ConnectionOwner::Foreground));
    manager.loop();
    manager.release(id, studio::ConnectionOwner::Foreground);
  }
  TEST_ASSERT_EQUAL_UINT32(4, manager.activeCount());
  TEST_ASSERT_EQUAL_UINT32(4, manager.bleSlotCount());

  // A second logical member joins the already-counted mesh transport even
  // while all four physical BLE slots are occupied.
  TEST_ASSERT_TRUE(
      manager.acquire(meshIds[1], studio::ConnectionOwner::Foreground));
  manager.loop();
  manager.release(meshIds[1], studio::ConnectionOwner::Foreground);
  TEST_ASSERT_EQUAL_UINT32(5, manager.activeCount());
  TEST_ASSERT_EQUAL_UINT32(4, manager.bleSlotCount());
  TEST_ASSERT_TRUE(manager.isActive(meshIds[0]));
  TEST_ASSERT_TRUE(manager.isActive(meshIds[1]));

  // Make each single-instance group newer than the mesh group so the next
  // distinct connection evicts the complete shared group.
  const studio::InstanceId newer[] = {sharkId, canonId, tascamId};
  for (studio::InstanceId id : newer) {
    TEST_ASSERT_TRUE(manager.acquire(id, studio::ConnectionOwner::Foreground));
    manager.release(id, studio::ConnectionOwner::Foreground);
  }

  studio::InstanceId secondCanon = studio::kInvalidInstanceId;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(manager.add(studio::DriverId::CanonBle, "Camera 2",
                                   secondCanon)));
  TEST_ASSERT_TRUE(
      manager.acquire(secondCanon, studio::ConnectionOwner::Foreground));
  TEST_ASSERT_FALSE(manager.isActive(meshIds[0]));
  TEST_ASSERT_FALSE(manager.isActive(meshIds[1]));
  TEST_ASSERT_EQUAL_UINT32(4, manager.bleSlotCount());
  TEST_ASSERT_EQUAL_INT(1, meshDriver.deactivationCount);
  TEST_ASSERT_EQUAL_INT(1, zhiyunDriver.deactivationCount);
}

void test_manager_cancels_unready_release_and_reuses_ready_session() {
  MemoryBackend backend;
  FakeDriver driver(studio::DriverId::CanonBle);
  studio::DeviceDriver* drivers[] = {&driver};
  studio::DeviceManager manager(backend, drivers, 1);
  TEST_ASSERT_TRUE(manager.begin());
  studio::InstanceId id = studio::kInvalidInstanceId;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(manager.add(studio::DriverId::CanonBle, "Camera", id)));

  driver.ready = false;
  TEST_ASSERT_TRUE(manager.acquire(id, studio::ConnectionOwner::Foreground));
  manager.release(id, studio::ConnectionOwner::Foreground);
  TEST_ASSERT_FALSE(manager.isActive(id));
  TEST_ASSERT_EQUAL_INT(1, driver.deactivationCount);

  driver.ready = true;
  TEST_ASSERT_TRUE(manager.acquire(id, studio::ConnectionOwner::Foreground));
  manager.loop();
  manager.release(id, studio::ConnectionOwner::Foreground);
  TEST_ASSERT_TRUE(manager.isRetained(id));
  const int activations = driver.activationCount;
  driver.resumeSucceeds = false;
  TEST_ASSERT_FALSE(manager.acquire(id, studio::ConnectionOwner::Foreground));
  TEST_ASSERT_FALSE(
      manager.ownedBy(id, studio::ConnectionOwner::Foreground));
  TEST_ASSERT_EQUAL_INT(activations, driver.activationCount);
  TEST_ASSERT_EQUAL_INT(1, driver.resumeCount);
  TEST_ASSERT_EQUAL_INT(id, driver.lastResumedInstance);

  driver.resumeSucceeds = true;
  TEST_ASSERT_TRUE(manager.acquire(id, studio::ConnectionOwner::Foreground));
  TEST_ASSERT_EQUAL_INT(activations, driver.activationCount);
  TEST_ASSERT_EQUAL_INT(2, driver.resumeCount);
  TEST_ASSERT_EQUAL_INT(id, driver.lastResumedInstance);
  manager.release(id, studio::ConnectionOwner::Foreground);
}

void test_manager_parks_ownerless_drop_but_keeps_intentional_offline() {
  MemoryBackend backend;
  FakeDriver driver(studio::DriverId::CanonBle);
  studio::DeviceDriver* drivers[] = {&driver};
  studio::DeviceManager manager(backend, drivers, 1);
  TEST_ASSERT_TRUE(manager.begin());
  studio::InstanceId id = studio::kInvalidInstanceId;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(manager.add(studio::DriverId::CanonBle, "Camera", id)));

  TEST_ASSERT_TRUE(manager.acquire(id, studio::ConnectionOwner::Foreground));
  manager.loop();
  manager.release(id, studio::ConnectionOwner::Foreground);
  TEST_ASSERT_TRUE(manager.isRetained(id));

  driver.forceDisconnected = true;
  driver.intentionalOffline = true;
  manager.loop();
  TEST_ASSERT_TRUE(manager.isActive(id));

  driver.intentionalOffline = false;
  manager.loop();
  TEST_ASSERT_FALSE(manager.isActive(id));
  TEST_ASSERT_EQUAL_INT(1, driver.deactivationCount);
}

void test_generated_stop_mapping_order_and_capacity() {
  studio::SceneRecord record;
  record.startCount = 8;
  record.startSteps[0] =
      studio::makeActionStep(1, studio::CommandType::RecordStart);
  record.startSteps[1] = studio::makeWaitStep(750);
  record.startSteps[2] =
      studio::makeActionStep(2, studio::CommandType::TurnOn);
  record.startSteps[3] =
      studio::makeActionStep(3, studio::CommandType::RecordTrigger, 1, 2, 3);
  record.startSteps[4] =
      studio::makeActionStep(4, studio::CommandType::Press);
  record.startSteps[5] =
      studio::makeActionStep(5, studio::CommandType::Activate);
  record.startSteps[6] =
      studio::makeActionStep(6, studio::CommandType::SetLightCctAndOn, 4300, 50);
  record.startSteps[7] =
      studio::makeActionStep(7, studio::CommandType::SetLightRgbAndOn, 0xff00ff, 75);
  studio::generateStopSteps(record);

  TEST_ASSERT_EQUAL_UINT8(8, record.stopCount);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::CommandType::TurnOff),
                        static_cast<int>(record.stopSteps[0].command));
  TEST_ASSERT_EQUAL_UINT32(7, record.stopSteps[0].targetId);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::CommandType::TurnOff),
                        static_cast<int>(record.stopSteps[1].command));
  TEST_ASSERT_EQUAL_UINT32(6, record.stopSteps[1].targetId);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::CommandType::Activate),
                        static_cast<int>(record.stopSteps[2].command));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::CommandType::Press),
                        static_cast<int>(record.stopSteps[3].command));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::CommandType::RecordTrigger),
                        static_cast<int>(record.stopSteps[4].command));
  TEST_ASSERT_EQUAL_INT32(1, record.stopSteps[4].value0);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::CommandType::TurnOff),
                        static_cast<int>(record.stopSteps[5].command));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::SceneStepType::Wait),
                        static_cast<int>(record.stopSteps[6].type));
  TEST_ASSERT_EQUAL_UINT32(750, record.stopSteps[6].waitMs);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::CommandType::RecordStop),
                        static_cast<int>(record.stopSteps[7].command));

  const uint32_t cctAndOn = studio::requiredCapabilities(
      studio::CommandType::SetLightCctAndOn);
  TEST_ASSERT_EQUAL_HEX32(
      studio::capabilityBit(studio::Capability::SetLightCct) |
          studio::capabilityBit(studio::Capability::TurnOn),
      cctAndOn);

  for (uint8_t i = 0; i < CONFIG_MAX_SCENE_STEPS; ++i) {
    record.startSteps[i] =
        studio::makeActionStep(i + 1, studio::CommandType::RecordTrigger);
  }
  record.startCount = CONFIG_MAX_SCENE_STEPS;
  studio::generateStopSteps(record);
  TEST_ASSERT_EQUAL_UINT8(CONFIG_MAX_SCENE_STEPS, record.stopCount);
  TEST_ASSERT_EQUAL_UINT32(CONFIG_MAX_SCENE_STEPS,
                           record.stopSteps[0].targetId);

  record.startCount = 2;
  record.startSteps[0] =
      studio::makeActionStep(1, studio::CommandType::RecordStop);
  record.startSteps[1] =
      studio::makeActionStep(2, studio::CommandType::TurnOff);
  studio::generateStopSteps(record);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::CommandType::TurnOn),
                        static_cast<int>(record.stopSteps[0].command));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::CommandType::RecordStart),
                        static_cast<int>(record.stopSteps[1].command));
}

void test_scene_stop_customization_and_relinking() {
  MemoryBackend deviceBackend;
  MemoryBackend sceneBackend;
  FakeDriver driver(studio::DriverId::CanonBle);
  studio::DeviceDriver* drivers[] = {&driver};
  studio::DeviceManager devices(deviceBackend, drivers, 1);
  TEST_ASSERT_TRUE(devices.begin());
  studio::InstanceId cameraId = studio::kInvalidInstanceId;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(devices.add(studio::DriverId::CanonBle, "Camera",
                                   cameraId)));

  studio::SceneService scenes(sceneBackend, devices);
  TEST_ASSERT_TRUE(scenes.begin());
  studio::SceneId sceneId = studio::kInvalidSceneId;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::SceneRegistryStatus::Ok),
                        static_cast<int>(scenes.add("Modes", sceneId)));
  studio::SceneRecord record = *scenes.find(sceneId);
  record.startCount = 1;
  record.startSteps[0] =
      studio::makeActionStep(cameraId, studio::CommandType::RecordStart);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::SceneRegistryStatus::Ok),
                        static_cast<int>(scenes.replace(record)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::CommandType::RecordStop),
                        static_cast<int>(scenes.find(sceneId)->stopSteps[0].command));

  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::SceneRegistryStatus::Ok),
                        static_cast<int>(scenes.customizeStop(sceneId)));
  record = *scenes.find(sceneId);
  record.stopSteps[0] =
      studio::makeActionStep(cameraId, studio::CommandType::RecordStart);
  record.startSteps[record.startCount++] = studio::makeWaitStep(650);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::SceneRegistryStatus::Ok),
                        static_cast<int>(scenes.replace(record)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::SceneStopMode::Custom),
                        static_cast<int>(scenes.find(sceneId)->stopMode));
  TEST_ASSERT_EQUAL_UINT8(1, scenes.find(sceneId)->stopCount);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::CommandType::RecordStart),
                        static_cast<int>(scenes.find(sceneId)->stopSteps[0].command));

  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::SceneRegistryStatus::Ok),
                        static_cast<int>(scenes.useGeneratedStop(sceneId)));
  const studio::SceneRecord* relinked = scenes.find(sceneId);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::SceneStopMode::Generated),
                        static_cast<int>(relinked->stopMode));
  TEST_ASSERT_EQUAL_UINT8(2, relinked->stopCount);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::SceneStepType::Wait),
                        static_cast<int>(relinked->stopSteps[0].type));
  TEST_ASSERT_EQUAL_UINT32(650, relinked->stopSteps[0].waitMs);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::CommandType::RecordStop),
                        static_cast<int>(relinked->stopSteps[1].command));
}

void test_scene_store_round_trip_and_corruption() {
  MemoryBackend backend;
  studio::SceneStore store(backend);
  studio::SceneRegistry source;
  studio::SceneId id = studio::kInvalidSceneId;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::SceneRegistryStatus::Ok),
      static_cast<int>(source.add("Press Record", id)));
  studio::SceneRecord* record = source.find(id);
  TEST_ASSERT_NOT_NULL(record);
  record->startCount = 4;
  record->startSteps[0] =
      studio::makeActionStep(2, studio::CommandType::SetLightCctAndOn, 5600, 72, -125);
  record->startSteps[1] = studio::makeWaitStep(500);
  record->startSteps[2] =
      studio::makeActionStep(3, studio::CommandType::RecordStart);
  record->startSteps[3] = studio::makeActionStep(
      4, studio::CommandType::SetLightRgbAndOn, 0x0000ff, 42, 0);
  record->stopMode = studio::SceneStopMode::Custom;
  record->stopCount = 2;
  record->stopSteps[0] =
      studio::makeActionStep(2, studio::CommandType::RecordStop);
  record->stopSteps[1] =
      studio::makeActionStep(3, studio::CommandType::RecordStop);
  TEST_ASSERT_TRUE(store.save(source));

  studio::SceneRegistry restored;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::ConfigLoadStatus::Loaded),
      static_cast<int>(store.load(restored)));
  TEST_ASSERT_EQUAL_UINT32(1, restored.count());
  TEST_ASSERT_EQUAL_STRING("Press Record", restored.at(0)->name);
  TEST_ASSERT_EQUAL_UINT8(4, restored.at(0)->startCount);
  TEST_ASSERT_EQUAL_UINT8(2, restored.at(0)->stopCount);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::SceneStopMode::Custom),
                        static_cast<int>(restored.at(0)->stopMode));
  TEST_ASSERT_EQUAL_UINT32(500, restored.at(0)->startSteps[1].waitMs);
  TEST_ASSERT_EQUAL_INT32(5600, restored.at(0)->startSteps[0].value0);
  TEST_ASSERT_EQUAL_INT32(72, restored.at(0)->startSteps[0].value1);
  TEST_ASSERT_EQUAL_INT32(-125, restored.at(0)->startSteps[0].value2);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::CommandType::SetLightCctAndOn),
      static_cast<int>(restored.at(0)->startSteps[0].command));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::CommandType::SetLightRgbAndOn),
      static_cast<int>(restored.at(0)->startSteps[3].command));
  TEST_ASSERT_EQUAL_INT32(0x0000ff, restored.at(0)->startSteps[3].value0);
  TEST_ASSERT_EQUAL_INT32(42, restored.at(0)->startSteps[3].value1);

  backend.corruptLastByte();
  studio::SceneRegistry rejected;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::ConfigLoadStatus::Corrupt),
      static_cast<int>(store.load(rejected)));
}

void test_scene_registry_and_store_grow_beyond_legacy_limit() {
  MemoryBackend backend;
  studio::SceneStore store(backend);
  studio::SceneRegistry source;
  for (size_t i = 0; i < 12; ++i) {
    char name[studio::kDeviceNameCapacity];
    std::snprintf(name, sizeof(name), "Sequence %u",
                  static_cast<unsigned>(i + 1));
    studio::SceneId id = studio::kInvalidSceneId;
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(studio::SceneRegistryStatus::Ok),
        static_cast<int>(source.add(name, id)));
  }
  TEST_ASSERT_EQUAL_UINT32(12, source.count());
  TEST_ASSERT_TRUE(store.save(source));

  studio::SceneRegistry restored;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::ConfigLoadStatus::Loaded),
      static_cast<int>(store.load(restored)));
  TEST_ASSERT_EQUAL_UINT32(12, restored.count());
  TEST_ASSERT_EQUAL_STRING("Sequence 12", restored.at(11)->name);
}

 void test_orphaned_scene_steps_can_be_removed_one_at_a_time() {
  MemoryBackend deviceBackend;
  MemoryBackend sceneBackend;
  FakeDriver driver(studio::DriverId::CanonBle);
  studio::DeviceDriver* drivers[] = {&driver};
  studio::DeviceManager devices(deviceBackend, drivers, 1);
  TEST_ASSERT_TRUE(devices.begin());

  studio::InstanceId oldCamera = studio::kInvalidInstanceId;
  studio::InstanceId newCamera = studio::kInvalidInstanceId;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(devices.add(studio::DriverId::CanonBle, "Old camera",
                                   oldCamera)));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(devices.add(studio::DriverId::CanonBle, "New camera",
                                   newCamera)));

  studio::SceneService scenes(sceneBackend, devices);
  TEST_ASSERT_TRUE(scenes.begin());
  studio::SceneId sceneId = studio::kInvalidSceneId;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::SceneRegistryStatus::Ok),
      static_cast<int>(scenes.add("Repair", sceneId)));
  studio::SceneRecord record = *scenes.find(sceneId);
  record.startCount = 1;
  record.startSteps[0] =
      studio::makeActionStep(oldCamera, studio::CommandType::RecordStart);
  record.stopMode = studio::SceneStopMode::Custom;
  record.stopCount = 1;
  record.stopSteps[0] =
      studio::makeActionStep(oldCamera, studio::CommandType::RecordStop);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::SceneRegistryStatus::Ok),
      static_cast<int>(scenes.replace(record)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::RegistryStatus::Ok),
                        static_cast<int>(devices.remove(oldCamera)));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::SceneValidationStatus::MissingTarget),
      static_cast<int>(scenes.validate(sceneId)));

  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::SceneRegistryStatus::Ok),
      static_cast<int>(scenes.removeStep(sceneId, true, 0)));
  TEST_ASSERT_EQUAL_UINT8(0, scenes.find(sceneId)->startCount);
  TEST_ASSERT_EQUAL_UINT8(1, scenes.find(sceneId)->stopCount);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::SceneValidationStatus::MissingTarget),
      static_cast<int>(scenes.validate(sceneId)));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::SceneRegistryStatus::Ok),
      static_cast<int>(scenes.removeStep(sceneId, false, 0)));
  TEST_ASSERT_EQUAL_UINT8(0, scenes.find(sceneId)->stopCount);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::SceneValidationStatus::Empty),
      static_cast<int>(scenes.validate(sceneId)));

  record = *scenes.find(sceneId);
  record.startCount = 1;
  record.startSteps[0] =
      studio::makeActionStep(newCamera, studio::CommandType::RecordStart);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::SceneRegistryStatus::Ok),
      static_cast<int>(scenes.replace(record)));
}

void test_press_record_start_and_generated_stop() {
  MemoryBackend deviceBackend;
  MemoryBackend sceneBackend;
  FakeDriver canonDriver(studio::DriverId::CanonBle);
  FakeDriver tascamDriver(studio::DriverId::TascamX8);
  studio::DeviceDriver* drivers[] = {&canonDriver, &tascamDriver};
  studio::DeviceManager devices(deviceBackend, drivers, 2);
  TEST_ASSERT_TRUE(devices.begin());
  // Drop the seeded Shark-less registry noise: begin may seed only Shark when
  // compiled; with no Shark driver the registry starts empty/missing.
  studio::InstanceId canonId = studio::kInvalidInstanceId;
  studio::InstanceId tascamId = studio::kInvalidInstanceId;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(
          devices.add(studio::DriverId::CanonBle, "R6 II", canonId)));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(
          devices.add(studio::DriverId::TascamX8, "X8", tascamId)));

  studio::SceneService scenes(sceneBackend, devices);
  TEST_ASSERT_TRUE(scenes.begin());
  studio::SceneId sceneId = studio::kInvalidSceneId;
  TEST_ASSERT_TRUE(scenes.seedPressRecord(sceneId));
  const studio::SceneRecord* seeded = scenes.find(sceneId);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::SceneStopMode::Generated),
                        static_cast<int>(seeded->stopMode));
  TEST_ASSERT_EQUAL_UINT8(3, seeded->stopCount);
  TEST_ASSERT_EQUAL_UINT32(tascamId, seeded->stopSteps[0].targetId);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::CommandType::RecordStop),
                        static_cast<int>(seeded->stopSteps[0].command));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::SceneStepType::Wait),
                        static_cast<int>(seeded->stopSteps[1].type));
  TEST_ASSERT_EQUAL_UINT32(500, seeded->stopSteps[1].waitMs);
  TEST_ASSERT_EQUAL_UINT32(canonId, seeded->stopSteps[2].targetId);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::SceneValidationStatus::Ok),
      static_cast<int>(scenes.validate(sceneId)));

  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::SceneRunStatus::Ok),
                        static_cast<int>(scenes.start(sceneId)));
  // Fake drivers report Connected as soon as activated.
  scenes.loop(0);
  devices.loop();
  scenes.loop(1);
  devices.loop();
  scenes.loop(2);
  // After connect + first action result processing.
  for (uint32_t t = 3; t < 20; ++t) {
    devices.loop();
    scenes.loop(t);
  }
  // Wait 500 ms from when wait began; advance far enough.
  for (uint32_t t = 20; t < 600; ++t) {
    devices.loop();
    scenes.loop(t);
  }
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::ScenePhase::IdleArmed),
      static_cast<int>(scenes.progress().phase));
  TEST_ASSERT_TRUE(devices.isActive(canonId));
  TEST_ASSERT_TRUE(devices.isActive(tascamId));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::CommandType::RecordStart),
      static_cast<int>(canonDriver.lastCommand));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::CommandType::RecordStart),
      static_cast<int>(tascamDriver.lastCommand));

  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::SceneRunStatus::Ok),
                        static_cast<int>(scenes.stop()));
  for (uint32_t t = 600; t < 1150; ++t) {
    devices.loop();
    scenes.loop(t);
  }
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::ScenePhase::Completed),
      static_cast<int>(scenes.progress().phase));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::CommandType::RecordStop),
      static_cast<int>(canonDriver.lastCommand));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::CommandType::RecordStop),
      static_cast<int>(tascamDriver.lastCommand));
  TEST_ASSERT_TRUE(scenes.holdsLinks());
  TEST_ASSERT_TRUE(devices.isActive(canonId));
  TEST_ASSERT_TRUE(devices.isActive(tascamId));
  scenes.cancel();
  TEST_ASSERT_EQUAL_UINT32(2, devices.activeCount());
  TEST_ASSERT_FALSE(devices.ownedBy(canonId, studio::ConnectionOwner::Sequence));
  TEST_ASSERT_FALSE(devices.ownedBy(tascamId, studio::ConnectionOwner::Sequence));
}

void test_partial_start_failure_can_stop_and_restart() {
  MemoryBackend deviceBackend;
  MemoryBackend sceneBackend;
  FakeDriver canonDriver(studio::DriverId::CanonBle);
  FakeDriver tascamDriver(studio::DriverId::TascamX8);
  studio::DeviceDriver* drivers[] = {&canonDriver, &tascamDriver};
  studio::DeviceManager devices(deviceBackend, drivers, 2);
  TEST_ASSERT_TRUE(devices.begin());

  studio::InstanceId canonId = studio::kInvalidInstanceId;
  studio::InstanceId tascamId = studio::kInvalidInstanceId;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(
          devices.add(studio::DriverId::CanonBle, "R6 II", canonId)));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(
          devices.add(studio::DriverId::TascamX8, "X8", tascamId)));

  studio::SceneService scenes(sceneBackend, devices);
  TEST_ASSERT_TRUE(scenes.begin());
  studio::SceneId sceneId = studio::kInvalidSceneId;
  TEST_ASSERT_TRUE(scenes.seedPressRecord(sceneId));

  tascamDriver.failCommand = studio::CommandType::RecordStart;
  tascamDriver.failCommandCount = 1;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::SceneRunStatus::Ok),
                        static_cast<int>(scenes.start(sceneId)));
  for (uint32_t t = 1; t < 700; ++t) {
    devices.loop();
    scenes.loop(t);
  }
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::ScenePhase::Failed),
                        static_cast<int>(scenes.progress().phase));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::SceneRunStatus::ActionFailed),
                        static_cast<int>(scenes.progress().lastStatus));

  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::SceneRunStatus::Ok),
                        static_cast<int>(scenes.stop()));
  for (uint32_t t = 700; t < 1250; ++t) {
    devices.loop();
    scenes.loop(t);
  }
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::ScenePhase::Completed),
                        static_cast<int>(scenes.progress().phase));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::CommandType::RecordStop),
                        static_cast<int>(canonDriver.lastCommand));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::CommandType::RecordStop),
                        static_cast<int>(tascamDriver.lastCommand));

  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::SceneRunStatus::Ok),
                        static_cast<int>(scenes.start(sceneId)));
  for (uint32_t t = 1250; t < 2000; ++t) {
    devices.loop();
    scenes.loop(t);
  }
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::ScenePhase::IdleArmed),
                        static_cast<int>(scenes.progress().phase));
}

void test_scene_dispatches_persisted_rgb_look_without_cct_fallback() {
  MemoryBackend deviceBackend;
  MemoryBackend sceneBackend;
  FakeDriver lightDriver(studio::DriverId::AputureLight);
  studio::DeviceDriver* drivers[] = {&lightDriver};
  studio::DeviceManager devices(deviceBackend, drivers, 1);
  TEST_ASSERT_TRUE(devices.begin());

  studio::InstanceId lightId = studio::kInvalidInstanceId;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(devices.add(studio::DriverId::AputureLight,
                                   "Aputure MC Pro", lightId)));
  studio::SceneService scenes(sceneBackend, devices);
  TEST_ASSERT_TRUE(scenes.begin());
  studio::SceneId sceneId = studio::kInvalidSceneId;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::SceneRegistryStatus::Ok),
      static_cast<int>(scenes.add("Blue", sceneId)));
  studio::SceneRecord record = *scenes.find(sceneId);
  record.startCount = 1;
  record.startSteps[0] = studio::makeActionStep(
      lightId, studio::CommandType::SetLightRgbAndOn, 0x0000ff, 42, 0);
  studio::generateStopSteps(record);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::SceneRegistryStatus::Ok),
      static_cast<int>(scenes.replace(record)));

  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::SceneRunStatus::Ok),
                        static_cast<int>(scenes.start(sceneId)));
  for (uint32_t now = 0; now < 20; ++now) {
    devices.loop();
    scenes.loop(now);
  }
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::CommandType::SetLightRgbAndOn),
      static_cast<int>(lightDriver.lastCommand));
  TEST_ASSERT_EQUAL_INT32(0x0000ff, lightDriver.lastValue0);
  TEST_ASSERT_EQUAL_INT32(42, lightDriver.lastValue1);
}

void test_stop_cancels_inflight_compound_light_action() {
  MemoryBackend deviceBackend;
  MemoryBackend sceneBackend;
  FakeDriver lightDriver(studio::DriverId::ZhiyunLight);
  lightDriver.pendingCommand = studio::CommandType::SetLightCctAndOn;
  studio::DeviceDriver* drivers[] = {&lightDriver};
  studio::DeviceManager devices(deviceBackend, drivers, 1);
  TEST_ASSERT_TRUE(devices.begin());

  studio::InstanceId lightId = studio::kInvalidInstanceId;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(
          devices.add(studio::DriverId::ZhiyunLight, "Light", lightId)));

  studio::SceneService scenes(sceneBackend, devices);
  TEST_ASSERT_TRUE(scenes.begin());
  studio::SceneId sceneId = studio::kInvalidSceneId;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::SceneRegistryStatus::Ok),
      static_cast<int>(scenes.add("Light", sceneId)));
  studio::SceneRecord record = *scenes.find(sceneId);
  record.startCount = 1;
  record.startSteps[0] = studio::makeActionStep(
      lightId, studio::CommandType::SetLightCctAndOn, 5600, 50, 0);
  studio::generateStopSteps(record);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::SceneRegistryStatus::Ok),
      static_cast<int>(scenes.replace(record)));

  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::SceneRunStatus::Ok),
                        static_cast<int>(scenes.start(sceneId)));
  scenes.loop(0);
  devices.loop();
  scenes.loop(1);
  TEST_ASSERT_TRUE(lightDriver.commandPending);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::ScenePhase::RunningStart),
                        static_cast<int>(scenes.progress().phase));

  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::SceneRunStatus::Ok),
                        static_cast<int>(scenes.stop()));
  TEST_ASSERT_EQUAL_INT(1, lightDriver.cancelPendingCount);
  TEST_ASSERT_FALSE(lightDriver.commandPending);

  devices.loop();
  scenes.loop(2);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::CommandType::TurnOff),
                        static_cast<int>(lightDriver.lastCommand));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::ScenePhase::Completed),
                        static_cast<int>(scenes.progress().phase));
}

void test_prepare_ready_then_start_from_held_links() {
  MemoryBackend deviceBackend;
  MemoryBackend sceneBackend;
  FakeDriver canonDriver(studio::DriverId::CanonBle);
  FakeDriver tascamDriver(studio::DriverId::TascamX8);
  studio::DeviceDriver* drivers[] = {&canonDriver, &tascamDriver};
  studio::DeviceManager devices(deviceBackend, drivers, 2);
  TEST_ASSERT_TRUE(devices.begin());
  studio::InstanceId canonId = studio::kInvalidInstanceId;
  studio::InstanceId tascamId = studio::kInvalidInstanceId;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(
          devices.add(studio::DriverId::CanonBle, "R6 II", canonId)));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(
          devices.add(studio::DriverId::TascamX8, "X8", tascamId)));

  studio::SceneService scenes(sceneBackend, devices);
  TEST_ASSERT_TRUE(scenes.begin());
  studio::SceneId sceneId = studio::kInvalidSceneId;
  TEST_ASSERT_TRUE(scenes.seedPressRecord(sceneId));

  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::SceneRunStatus::Ok),
                        static_cast<int>(scenes.prepare(sceneId)));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::ScenePhase::Connecting),
      static_cast<int>(scenes.progress().phase));
  tascamDriver.ready = false;
  devices.loop();
  scenes.loop(1);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::ScenePhase::Connecting),
                        static_cast<int>(scenes.progress().phase));
  // A recovering link may legitimately pass the old 20-second boundary;
  // preparation remains active until the bounded 30-second terminal timeout.
  scenes.loop(20001);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::ScenePhase::Connecting),
                        static_cast<int>(scenes.progress().phase));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::SceneRunStatus::Busy),
                        static_cast<int>(scenes.start(sceneId)));
  tascamDriver.ready = true;
  scenes.loop(20002);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::ScenePhase::Ready),
                        static_cast<int>(scenes.progress().phase));
  TEST_ASSERT_TRUE(scenes.holdsLinks());
  TEST_ASSERT_TRUE(devices.isActive(canonId));
  TEST_ASSERT_TRUE(devices.isActive(tascamId));
  TEST_ASSERT_FALSE(scenes.busy());

  const studio::SceneRecord original = *scenes.find(sceneId);
  studio::SceneRecord edited = original;
  edited.startSteps[1] = studio::makeWaitStep(650);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::SceneRegistryStatus::Ok),
      static_cast<int>(scenes.replace(edited)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::ScenePhase::Ready),
                        static_cast<int>(scenes.progress().phase));
  TEST_ASSERT_TRUE(devices.isActive(canonId));
  TEST_ASSERT_TRUE(devices.isActive(tascamId));

  studio::SceneRecord cameraOnly = edited;
  cameraOnly.startCount = 1;
  cameraOnly.startSteps[0] =
      studio::makeActionStep(canonId, studio::CommandType::RecordStart);
  cameraOnly.stopCount = 1;
  cameraOnly.stopSteps[0] =
      studio::makeActionStep(canonId, studio::CommandType::RecordStop);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::SceneRegistryStatus::Ok),
      static_cast<int>(scenes.replace(cameraOnly)));
  TEST_ASSERT_TRUE(devices.isActive(canonId));
  TEST_ASSERT_TRUE(devices.isActive(tascamId));
  TEST_ASSERT_FALSE(
      devices.ownedBy(tascamId, studio::ConnectionOwner::Sequence));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::SceneRegistryStatus::Ok),
      static_cast<int>(scenes.replace(original)));
  TEST_ASSERT_TRUE(devices.isActive(canonId));
  TEST_ASSERT_TRUE(devices.isActive(tascamId));

  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::SceneRunStatus::Ok),
                        static_cast<int>(scenes.start(sceneId)));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::ScenePhase::RunningStart),
      static_cast<int>(scenes.progress().phase));
  for (uint32_t t = 3; t < 601; ++t) {
    devices.loop();
    scenes.loop(t);
  }
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::ScenePhase::IdleArmed),
      static_cast<int>(scenes.progress().phase));
  TEST_ASSERT_TRUE(devices.isActive(canonId));
  TEST_ASSERT_TRUE(devices.isActive(tascamId));

  scenes.cancel();
  TEST_ASSERT_FALSE(scenes.holdsLinks());
  TEST_ASSERT_EQUAL_UINT32(2, devices.activeCount());
}

class BleTestDelegate : public studio::ble::BleCentralDelegate {
 public:
  void onBleAdvertisement(
      studio::ble::LinkHandle link,
      const studio::ble::Advertisement& advertisement) override {
    ++advertisements;
    lastAdvertisement = advertisement;
    if (central != nullptr && selectAdvertisements) {
      selected = central->selectAdvertisement(link, advertisement);
    }
  }

  void onBleEvent(studio::ble::LinkHandle link,
                  const studio::ble::Event& event) override {
    ++events;
    lastEvent = event.type;
    if (central != nullptr && requestSecurityOnConnect &&
        event.type == studio::ble::EventType::Connected) {
      securityRequested = central->requestSecurity(link);
    }
  }

  studio::ble::BleCentral* central = nullptr;
  studio::ble::Advertisement lastAdvertisement;
  studio::ble::EventType lastEvent = studio::ble::EventType::ScanEnded;
  uint32_t advertisements = 0;
  uint32_t events = 0;
  bool selectAdvertisements = false;
  bool requestSecurityOnConnect = false;
  bool selected = false;
  bool securityRequested = false;
};

studio::ble::Address bleAddress(const char* value, uint8_t type = 0) {
  studio::ble::Address address;
  std::strncpy(address.value, value, sizeof(address.value) - 1);
  address.type = type;
  return address;
}

studio::ble::Advertisement bleAdvertisement(const char* address,
                                             const char* name,
                                             uint16_t service) {
  studio::ble::Advertisement advertisement;
  advertisement.address = bleAddress(address);
  size_t offset = 0;
  const size_t nameLength = std::strlen(name);
  advertisement.payload[offset++] = static_cast<uint8_t>(nameLength + 1);
  advertisement.payload[offset++] = 0x09;
  std::memcpy(advertisement.payload + offset, name, nameLength);
  offset += nameLength;
  advertisement.payload[offset++] = 3;
  advertisement.payload[offset++] = 0x03;
  advertisement.payload[offset++] = static_cast<uint8_t>(service & 0xff);
  advertisement.payload[offset++] = static_cast<uint8_t>(service >> 8);
  advertisement.payloadLength = static_cast<uint8_t>(offset);
  return advertisement;
}

studio::ble::Advertisement bleAdvertisement128(
    const uint8_t canonicalUuid[16]) {
  studio::ble::Advertisement advertisement;
  advertisement.address = bleAddress("11:22:33:44:55:66");
  advertisement.payload[0] = 17;
  advertisement.payload[1] = 0x07;
  for (size_t i = 0; i < 16; ++i) {
    advertisement.payload[2 + i] = canonicalUuid[15 - i];
  }
  advertisement.payloadLength = 18;
  return advertisement;
}

void test_scene_fails_fast_when_physical_target_rejects_pairing() {
  MemoryBackend deviceBackend;
  MemoryBackend sceneBackend;
  FakeDriver cameraDriver(studio::DriverId::CanonBle);
  FakeDriver homeAssistantDriver(studio::DriverId::HomeAssistant);
  cameraDriver.ready = false;
  studio::DeviceDriver* drivers[] = {&cameraDriver, &homeAssistantDriver};
  studio::DeviceManager devices(deviceBackend, drivers, 2);
  TEST_ASSERT_TRUE(devices.begin());

  studio::InstanceId cameraId = studio::kInvalidInstanceId;
  studio::InstanceId helperId = studio::kInvalidInstanceId;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(devices.add(studio::DriverId::CanonBle, "R6 Mark II",
                                   cameraId)));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(devices.addHomeAssistantEntity(
          studio::HomeAssistantDomain::InputBoolean, "input_boolean.live",
          "Live", helperId)));

  studio::SceneService scenes(sceneBackend, devices);
  TEST_ASSERT_TRUE(scenes.begin());
  studio::SceneId sceneId = studio::kInvalidSceneId;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::SceneRegistryStatus::Ok),
      static_cast<int>(scenes.add("Rejected", sceneId)));
  studio::SceneRecord record = *scenes.find(sceneId);
  record.startCount = 2;
  record.startSteps[0] =
      studio::makeActionStep(helperId, studio::CommandType::TurnOn);
  record.startSteps[1] =
      studio::makeActionStep(cameraId, studio::CommandType::RecordStart);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::SceneRegistryStatus::Ok),
      static_cast<int>(scenes.replace(record)));

  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::SceneRunStatus::Ok),
                        static_cast<int>(scenes.prepare(sceneId)));
  scenes.loop(1);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::ScenePhase::Connecting),
                        static_cast<int>(scenes.progress().phase));

  // The camera accepts each link and hangs up before bonding: waiting out the
  // 60 s physical budget cannot help, so the run fails now and names the
  // device that needs a new pairing.
  cameraDriver.pairingRejected = true;
  scenes.loop(2);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::ScenePhase::Failed),
                        static_cast<int>(scenes.progress().phase));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::SceneRunStatus::ActionFailed),
                        static_cast<int>(scenes.progress().lastStatus));
  TEST_ASSERT_EQUAL_STRING("Re-pair R6 Mark II", scenes.progress().detail);
  TEST_ASSERT_EQUAL_UINT32(cameraId, scenes.progress().connectTarget);
  // HA was never started for a run that cannot complete.
  TEST_ASSERT_EQUAL_INT(0, homeAssistantDriver.activationCount);
  scenes.cancel();
}

namespace {
bool gEarlyNetworkAllowed = false;
bool earlyNetworkAllowed() { return gEarlyNetworkAllowed; }
}  // namespace

void test_early_network_policy_starts_home_assistant_while_ble_wakes() {
  MemoryBackend deviceBackend;
  MemoryBackend sceneBackend;
  FakeDriver physicalDriver(studio::DriverId::CanonBle);
  FakeDriver homeAssistantDriver(studio::DriverId::HomeAssistant);
  physicalDriver.ready = false;
  homeAssistantDriver.ready = false;
  studio::DeviceDriver* drivers[] = {&physicalDriver, &homeAssistantDriver};
  studio::DeviceManager devices(deviceBackend, drivers, 2);
  TEST_ASSERT_TRUE(devices.begin());

  studio::InstanceId cameraId = studio::kInvalidInstanceId;
  studio::InstanceId helperId = studio::kInvalidInstanceId;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(devices.add(studio::DriverId::CanonBle, "Camera",
                                   cameraId)));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(devices.addHomeAssistantEntity(
          studio::HomeAssistantDomain::InputBoolean, "input_boolean.live",
          "Live", helperId)));

  studio::SceneService scenes(sceneBackend, devices);
  TEST_ASSERT_TRUE(scenes.begin());
  scenes.setEarlyNetworkPolicy(earlyNetworkAllowed);
  studio::SceneId sceneId = studio::kInvalidSceneId;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::SceneRegistryStatus::Ok),
      static_cast<int>(scenes.add("Early HA", sceneId)));
  studio::SceneRecord record = *scenes.find(sceneId);
  record.startCount = 2;
  record.startSteps[0] =
      studio::makeActionStep(cameraId, studio::CommandType::RecordStart);
  record.startSteps[1] =
      studio::makeActionStep(helperId, studio::CommandType::TurnOn);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::SceneRegistryStatus::Ok),
      static_cast<int>(scenes.replace(record)));

  // Policy denies: HA stays deferred exactly as before.
  gEarlyNetworkAllowed = false;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::SceneRunStatus::Ok),
                        static_cast<int>(scenes.prepare(sceneId)));
  scenes.loop(1);
  scenes.loop(500);
  TEST_ASSERT_EQUAL_INT(0, homeAssistantDriver.activationCount);
  TEST_ASSERT_EQUAL_INT(1, physicalDriver.activationCount);

  // Policy allows: HA starts while the camera is still waking, and the
  // physical wait keeps its 60 s budget rather than HA's 30 s one.
  gEarlyNetworkAllowed = true;
  scenes.loop(1000);
  TEST_ASSERT_EQUAL_INT(1, homeAssistantDriver.activationCount);
  TEST_ASSERT_TRUE(devices.ownedBy(helperId, studio::ConnectionOwner::Sequence));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::ScenePhase::Connecting),
                        static_cast<int>(scenes.progress().phase));
  scenes.loop(CONFIG_SCENE_CONNECT_TIMEOUT_MS + 1500);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::ScenePhase::Connecting),
                        static_cast<int>(scenes.progress().phase));

  // Camera ready first: HA gets a fresh network budget from that moment.
  constexpr uint32_t physicalReadyAt = CONFIG_SCENE_PHYSICAL_CONNECT_TIMEOUT_MS - 1000;
  physicalDriver.ready = true;
  scenes.loop(physicalReadyAt);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::ScenePhase::Connecting),
                        static_cast<int>(scenes.progress().phase));
  scenes.loop(physicalReadyAt + CONFIG_SCENE_CONNECT_TIMEOUT_MS - 1);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::ScenePhase::Connecting),
                        static_cast<int>(scenes.progress().phase));
  homeAssistantDriver.ready = true;
  scenes.loop(physicalReadyAt + CONFIG_SCENE_CONNECT_TIMEOUT_MS);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::ScenePhase::Ready),
                        static_cast<int>(scenes.progress().phase));
  scenes.cancel();
  gEarlyNetworkAllowed = false;
}

void test_scene_acquires_recorders_before_cameras() {
  MemoryBackend deviceBackend;
  MemoryBackend sceneBackend;
  FakeDriver cameraDriver(studio::DriverId::CanonBle);
  FakeDriver recorderDriver(studio::DriverId::TascamX8);
  int activationSequence = 0;
  cameraDriver.activationSequence = &activationSequence;
  recorderDriver.activationSequence = &activationSequence;
  studio::DeviceDriver* drivers[] = {&cameraDriver, &recorderDriver};
  studio::DeviceManager devices(deviceBackend, drivers, 2);
  TEST_ASSERT_TRUE(devices.begin());

  studio::InstanceId cameraId = studio::kInvalidInstanceId;
  studio::InstanceId recorderId = studio::kInvalidInstanceId;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(devices.add(studio::DriverId::CanonBle, "Camera",
                                   cameraId)));
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(devices.add(studio::DriverId::TascamX8, "Recorder",
                                   recorderId)));

  studio::SceneService scenes(sceneBackend, devices);
  TEST_ASSERT_TRUE(scenes.begin());
  studio::SceneId sceneId = studio::kInvalidSceneId;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::SceneRegistryStatus::Ok),
      static_cast<int>(scenes.add("Camera first", sceneId)));
  studio::SceneRecord record = *scenes.find(sceneId);
  // Authored camera-first order must not put the camera's wake pokes ahead
  // of the recorder's sub-second connect in the controller queue.
  record.startCount = 2;
  record.startSteps[0] =
      studio::makeActionStep(cameraId, studio::CommandType::RecordStart);
  record.startSteps[1] =
      studio::makeActionStep(recorderId, studio::CommandType::RecordStart);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::SceneRegistryStatus::Ok),
      static_cast<int>(scenes.replace(record)));

  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::SceneRunStatus::Ok),
                        static_cast<int>(scenes.prepare(sceneId)));
  TEST_ASSERT_EQUAL_INT(1, recorderDriver.firstActivationOrder);
  TEST_ASSERT_EQUAL_INT(2, cameraDriver.firstActivationOrder);
  scenes.cancel();
}
