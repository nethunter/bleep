void test_aputure_light_crypto_and_network_vectors() {
  const uint8_t aesKey[16] = {0x2b,0x7e,0x15,0x16,0x28,0xae,0xd2,0xa6,
      0xab,0xf7,0x15,0x88,0x09,0xcf,0x4f,0x3c};
  const uint8_t aesInput[16] = {0x6b,0xc1,0xbe,0xe2,0x2e,0x40,0x9f,0x96,
      0xe9,0x3d,0x7e,0x11,0x73,0x93,0x17,0x2a};
  const uint8_t aesExpected[16] = {0x3a,0xd7,0x7b,0xb4,0x0d,0x7a,0x36,0x60,
      0xa8,0x9e,0xca,0xf3,0x24,0x66,0xef,0x97};
  uint8_t block[16];
  aputure_light::aes128EncryptBlock(aesKey, aesInput, block);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(aesExpected, block, 16);

  const uint8_t cmacExpected[16] = {0xbb,0x1d,0x69,0x29,0xe9,0x59,0x37,0x28,
      0x7f,0xa3,0x7d,0x12,0x9b,0x75,0x67,0x46};
  uint8_t cmac[16];
  aputure_light::aesCmac(aesKey, nullptr, 0, cmac);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(cmacExpected, cmac, 16);

  const uint8_t networkKey[16] = {0x00,0x11,0x22,0x33,0x44,0x55,0x66,0x77,
      0x88,0x99,0xaa,0xbb,0xcc,0xdd,0xee,0xff};
  const uint8_t appKey[16] = {0xff,0xee,0xdd,0xcc,0xbb,0xaa,0x99,0x88,
      0x77,0x66,0x55,0x44,0x33,0x22,0x11,0x00};
  aputure_light::AccessPayload blue;
  TEST_ASSERT_TRUE(aputure_light::buildRgbAccess(0x0000ff, 0, blue));
  // Lock the exact captured blue payload independently of the brightness-aware path.
  const uint8_t blueAccess[11] = {0x26,0xd3,0xa0,0,0,0,0xa0,0x0f,0,0,0x84};
  aputure_light::NetworkPdu network;
  TEST_ASSERT_TRUE(aputure_light::encodeAccessMessage(
      networkKey, appKey, blueAccess, sizeof(blueAccess), 1, 1, 0xc000, 0,
      network));
  const uint8_t expectedNetwork[] = {0x1e,0x72,0xa3,0xec,0xac,0x74,0x86,0xe5,
      0xfa,0x7f,0xf4,0x21,0x57,0xd4,0x22,0xd0,0xe9,0x15,0x35,0x5a,0x48,0x70,
      0xd9,0xfc,0xce,0xd7,0xc1,0xe3,0xba};
  TEST_ASSERT_EQUAL_UINT32(sizeof(expectedNetwork), network.length);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expectedNetwork, network.bytes, network.length);

  const uint8_t mcOff[] = {0x26,0xe8,0,0,0,0,0x20,0xa4,0x28,0xfa,0x02};
  TEST_ASSERT_TRUE(aputure_light::encodeAccessMessage(
      networkKey, appKey, mcOff, sizeof(mcOff), 0x1234, 2, 1, 0, network));
  uint8_t proxy[70] = {};
  size_t proxyLength = 0;
  TEST_ASSERT_TRUE(aputure_light::wrapProxyPdu(
      network, proxy, sizeof(proxy), proxyLength));
  aputure_light::DecodedAccessMessage decoded;
  TEST_ASSERT_TRUE(aputure_light::decodeProxyAccessMessage(
      networkKey, appKey, proxy, proxyLength, 0, decoded));
  TEST_ASSERT_EQUAL_UINT32(0x1234, decoded.sequence);
  TEST_ASSERT_EQUAL_UINT16(2, decoded.source);
  TEST_ASSERT_EQUAL_UINT16(1, decoded.destination);
  TEST_ASSERT_EQUAL_UINT32(sizeof(mcOff), decoded.accessLength);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(mcOff, decoded.access, decoded.accessLength);

  proxy[proxyLength - 1] ^= 1;
  TEST_ASSERT_FALSE(aputure_light::decodeProxyAccessMessage(
      networkKey, appKey, proxy, proxyLength, 0, decoded));

  const uint8_t deviceKey[16] = {0x10,0x21,0x32,0x43,0x54,0x65,0x76,0x87,
      0x98,0xa9,0xba,0xcb,0xdc,0xed,0xfe,0x0f};
  const uint8_t appKeyStatus[] = {0x80,0x03,0x00,0x00,0x00};
  TEST_ASSERT_TRUE(aputure_light::encodeDeviceMessage(
      networkKey, deviceKey, appKeyStatus, sizeof(appKeyStatus), 0x2345,
      2, 1, 0, network));
  TEST_ASSERT_TRUE(aputure_light::wrapProxyPdu(
      network, proxy, sizeof(proxy), proxyLength));
  TEST_ASSERT_TRUE(aputure_light::decodeProxyDeviceMessage(
      networkKey, deviceKey, proxy, proxyLength, 0, decoded));
  TEST_ASSERT_EQUAL_UINT32(0x2345, decoded.sequence);
  TEST_ASSERT_EQUAL_UINT16(2, decoded.source);
  TEST_ASSERT_EQUAL_UINT16(1, decoded.destination);
  TEST_ASSERT_EQUAL_UINT32(sizeof(appKeyStatus), decoded.accessLength);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(
      appKeyStatus, decoded.access, decoded.accessLength);

  const uint8_t mcComposition[] = {
      0x02,0x00, 0xf6,0x03, 0x00,0x00, 0x00,0x00, 0x14,0x00, 0x0f,0x00,
      0x00,0x00, 0x03,0x01, 0x00,0x00, 0x02,0x00, 0x00,0x10,
      0xf6,0x03, 0x00,0x10};
  aputure_light::CompositionVendorModel composition;
  TEST_ASSERT_TRUE(aputure_light::parseCompositionVendorModel(
      mcComposition, sizeof(mcComposition), composition));
  TEST_ASSERT_EQUAL_HEX16(0x03f6, composition.companyId);
  TEST_ASSERT_EQUAL_HEX16(0x1000, composition.modelId);

  const uint8_t aceComposition[] = {
      0x02,0x00, 0x11,0x02, 0x00,0x00, 0x33,0x33, 0x69,0x00, 0x07,0x00,
      0x00,0x00, 0x0a,0x01,
      0x00,0x00, 0x02,0x00, 0x03,0x00, 0x00,0x10, 0x02,0x10,
      0x04,0x10, 0x06,0x10, 0x07,0x10, 0x00,0x13, 0x01,0x13,
      0x11,0x02, 0x00,0x00};
  TEST_ASSERT_TRUE(aputure_light::parseCompositionVendorModel(
      aceComposition, sizeof(aceComposition), composition));
  TEST_ASSERT_EQUAL_HEX16(0x0211, composition.companyId);
  TEST_ASSERT_EQUAL_HEX16(0x0000, composition.modelId);
  TEST_ASSERT_FALSE(aputure_light::parseCompositionVendorModel(
      aceComposition, sizeof(aceComposition) - 1, composition));

  const uint32_t compositionSequences[] = {0x3450, 0x3451, 0x3452};
  aputure_light::NetworkPduBatch compositionBatch;
  TEST_ASSERT_TRUE(aputure_light::encodeSegmentedDeviceMessage(
      networkKey, deviceKey, mcComposition, sizeof(mcComposition),
      compositionSequences, 3, 2, 1, 0, compositionBatch));
  TEST_ASSERT_EQUAL_UINT8(3, compositionBatch.count);
  aputure_light::DeviceMessageReassembly reassembly;
  const uint8_t order[] = {1, 0, 2};
  for (uint8_t i = 0; i < 3; ++i) {
    const uint8_t segment = order[i];
    TEST_ASSERT_TRUE(aputure_light::wrapProxyPdu(
        compositionBatch.pdus[segment], proxy, sizeof(proxy), proxyLength));
    const aputure_light::DeviceDecodeResult result =
        aputure_light::decodeProxySegmentedDeviceMessage(
            networkKey, deviceKey, proxy, proxyLength, 0, reassembly, decoded);
    TEST_ASSERT_EQUAL_INT(
        static_cast<int>(i == 2
                             ? aputure_light::DeviceDecodeResult::Complete
                             : aputure_light::DeviceDecodeResult::Pending),
        static_cast<int>(result));
  }
  TEST_ASSERT_EQUAL_UINT32(sizeof(mcComposition), decoded.accessLength);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(mcComposition, decoded.access,
                                decoded.accessLength);
}

void test_aputure_light_access_payloads_and_validation() {
  const aputure_light::AputureLightState defaultState;
  TEST_ASSERT_EQUAL_HEX32(0xff0000, defaultState.rgb);

  uint16_t companyId = 0;
  uint16_t modelId = 0;
  TEST_ASSERT_TRUE(aputure_light::inferKnownVendorModel(
      "Aputure MC Pro", "", companyId, modelId));
  TEST_ASSERT_EQUAL_HEX16(0x03f6, companyId);
  TEST_ASSERT_EQUAL_HEX16(0x1000, modelId);
  TEST_ASSERT_EQUAL_STRING(
      "Aputure MC Pro",
      aputure_light::knownVendorModelName(companyId, modelId));
  TEST_ASSERT_TRUE(aputure_light::inferKnownVendorModel(
      "Studio light", "SLCK_BLE", companyId, modelId));
  TEST_ASSERT_EQUAL_HEX16(0x0211, companyId);
  TEST_ASSERT_EQUAL_HEX16(0x0000, modelId);
  TEST_ASSERT_NULL(aputure_light::knownVendorModelName(companyId, modelId));
  TEST_ASSERT_TRUE(aputure_light::inferKnownVendorModel(
      "amaran Pano 60c", "", companyId, modelId));
  TEST_ASSERT_EQUAL_HEX16(0x0211, companyId);
  TEST_ASSERT_EQUAL_HEX16(0x0000, modelId);
  TEST_ASSERT_EQUAL_STRING(
      "amaran Pano 60c", aputure_light::knownProductName("Pavo 60"));
  TEST_ASSERT_EQUAL_STRING(
      "amaran Pano 120c",
      aputure_light::knownProductName("amaran Pano 120c"));
  TEST_ASSERT_FALSE(aputure_light::inferKnownVendorModel(
      "Unknown light", "", companyId, modelId));
  TEST_ASSERT_NULL(aputure_light::knownVendorModelName(0, 0));

  aputure_light::AccessPayload payload;
  TEST_ASSERT_TRUE(aputure_light::buildPowerAccess(true, payload));
  const uint8_t powerOn[] = {0x26,0x8d,0,0,0,0,0,0,0,1,0x8c};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(powerOn, payload.bytes, sizeof(powerOn));
  aputure_light::NetworkPdu unicastPower;
  const uint8_t unicastNetworkKey[16] = {
      0x00,0x11,0x22,0x33,0x44,0x55,0x66,0x77,
      0x88,0x99,0xaa,0xbb,0xcc,0xdd,0xee,0xff};
  const uint8_t unicastAppKey[16] = {
      0xff,0xee,0xdd,0xcc,0xbb,0xaa,0x99,0x88,
      0x77,0x66,0x55,0x44,0x33,0x22,0x11,0x00};
  TEST_ASSERT_TRUE(aputure_light::encodeAccessMessage(
      unicastNetworkKey, unicastAppKey, powerOn, sizeof(powerOn), 1, 1, 2, 0,
      unicastPower));
  const uint8_t expectedUnicastPower[] = {
      0x1e,0x4f,0x47,0x6a,0x22,0xab,0xe7,0x25,0xf8,0x7f,
      0x38,0x12,0x50,0x33,0x85,0x94,0xf7,0x4f,0x6b,0xf5,
      0x06,0xc8,0x8a,0x28,0x15,0xc7,0x6c,0x42,0x29};
  TEST_ASSERT_EQUAL_UINT32(sizeof(expectedUnicastPower), unicastPower.length);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expectedUnicastPower, unicastPower.bytes,
                                unicastPower.length);
  const uint8_t mcOff[] = {0x26,0xe8,0,0,0,0,0x20,0xa4,0x28,0xfa,0x02};
  const uint8_t aceOn[] = {0x26,0xec,1,0,0,0,0x80,0x56,0x1a,0xfa,0x01};
  aputure_light::VendorPowerStatus status;
  TEST_ASSERT_TRUE(
      aputure_light::parseVendorPowerStatus(mcOff, sizeof(mcOff), status));
  TEST_ASSERT_FALSE(status.on);
  TEST_ASSERT_EQUAL_UINT8(250, status.storedIntensity);
  TEST_ASSERT_EQUAL_UINT8(2, status.profile);
  TEST_ASSERT_TRUE(
      aputure_light::parseVendorPowerStatus(aceOn, sizeof(aceOn), status));
  TEST_ASSERT_TRUE(status.on);
  TEST_ASSERT_EQUAL_UINT8(1, status.profile);
  uint8_t corruptStatus[sizeof(aceOn)];
  std::memcpy(corruptStatus, aceOn, sizeof(aceOn));
  corruptStatus[6] ^= 1;
  TEST_ASSERT_FALSE(aputure_light::parseVendorPowerStatus(
      corruptStatus, sizeof(corruptStatus), status));

  aputure_light::ConfigurationStatusExpectation configExpected;
  configExpected.type =
      aputure_light::ConfigurationStatusType::ModelSubscription;
  configExpected.elementAddress = 0x0006;
  configExpected.groupAddress = 0xc005;
  configExpected.companyId = 0x0211;
  configExpected.modelId = 0x0000;
  configExpected.vendorModel = true;
  uint8_t configStatus = 0xff;
  const uint8_t acePrivateSubscription[] = {
      0x80,0x1f,0x00,0x06,0x00,0x05,0xc0,0x11,0x02,0x00,0x00};
  TEST_ASSERT_TRUE(aputure_light::matchConfigurationStatus(
      acePrivateSubscription, sizeof(acePrivateSubscription), configExpected,
      configStatus));
  TEST_ASSERT_EQUAL_UINT8(0, configStatus);
  const uint8_t duplicatedCommonSubscription[] = {
      0x80,0x1f,0x00,0x06,0x00,0x00,0xc0,0x11,0x02,0x00,0x00};
  TEST_ASSERT_FALSE(aputure_light::matchConfigurationStatus(
      duplicatedCommonSubscription, sizeof(duplicatedCommonSubscription),
      configExpected, configStatus));
  uint8_t wrongElementSubscription[sizeof(acePrivateSubscription)];
  std::memcpy(wrongElementSubscription, acePrivateSubscription,
              sizeof(acePrivateSubscription));
  wrongElementSubscription[3] = 0x04;
  TEST_ASSERT_FALSE(aputure_light::matchConfigurationStatus(
      wrongElementSubscription, sizeof(wrongElementSubscription),
      configExpected, configStatus));

  configExpected.type = aputure_light::ConfigurationStatusType::ModelApp;
  const uint8_t aceModelApp[] = {
      0x80,0x3e,0x00,0x06,0x00,0x00,0x00,0x11,0x02,0x00,0x00};
  TEST_ASSERT_TRUE(aputure_light::matchConfigurationStatus(
      aceModelApp, sizeof(aceModelApp), configExpected, configStatus));
  configExpected.type = aputure_light::ConfigurationStatusType::AppKey;
  configExpected.vendorModel = false;
  const uint8_t appKeyStatus[] = {0x80,0x03,0x00,0x00,0x00,0x00};
  TEST_ASSERT_TRUE(aputure_light::matchConfigurationStatus(
      appKeyStatus, sizeof(appKeyStatus), configExpected, configStatus));
  TEST_ASSERT_TRUE(aputure_light::buildCctAccess(5000, 0, 89, payload));
  const uint8_t cct[] = {0x26,0x80,0,0,0,0,0x40,0x41,0x9f,0xde,0x82};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(cct, payload.bytes, sizeof(cct));
  TEST_ASSERT_TRUE(aputure_light::buildRgbAccess(0xff0000, 50, payload));
  const uint8_t rgb[] = {0x26,0xdd,0x40,0x1f,0,0,0,0,0,0xfa,0x84};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(rgb, payload.bytes, sizeof(rgb));
  TEST_ASSERT_TRUE(aputure_light::buildRgbAccess(0xff0000, 5, payload));
  const uint8_t mcRed5[] = {
      0x26,0x87,0x06,0x03,0,0,0,0,0,0xfa,0x84};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(mcRed5, payload.bytes, sizeof(mcRed5));
  TEST_ASSERT_TRUE(aputure_light::buildRgbAccess(0x00ff00, 5, payload));
  const uint8_t aceGreen5[] = {
      0x26,0x4b,0x06,0x03,0,0,0,0x80,0x3e,0,0x84};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(aceGreen5, payload.bytes, sizeof(aceGreen5));
  TEST_ASSERT_TRUE(aputure_light::validCctCommand(2300, 0, -1000));
  TEST_ASSERT_FALSE(aputure_light::validCctCommand(2299, 50, 0));
  TEST_ASSERT_TRUE(aputure_light::validRgbCommand(0xffffff, 100));
  TEST_ASSERT_FALSE(aputure_light::validRgbCommand(0x1000000, 50));

  const uint8_t networkKey[16] = {};
  const uint8_t deviceKey[16] = {1};
  uint8_t appKeyAdd[20] = {};
  const uint32_t sequences[] = {0x123, 0x124};
  aputure_light::NetworkPduBatch segmented;
  TEST_ASSERT_TRUE(aputure_light::encodeSegmentedDeviceMessage(
      networkKey, deviceKey, appKeyAdd, sizeof(appKeyAdd), sequences, 2,
      1, 2, 0, segmented));
  TEST_ASSERT_EQUAL_UINT8(2, segmented.count);
  TEST_ASSERT_TRUE(segmented.pdus[0].length > 0);
  TEST_ASSERT_TRUE(segmented.pdus[1].length > 0);
  TEST_ASSERT_FALSE(aputure_light::encodeDeviceMessage(
      networkKey, deviceKey, appKeyAdd, sizeof(appKeyAdd), sequences[0],
      1, 2, 0, segmented.pdus[0]));
}

void test_aputure_light_store_and_sequence_reservation_survive_restart() {
  MemoryBackend backend;
  studio::mesh::Store store(backend);
  studio::mesh::StoreData data;
  data.network.initialized = true;
  for (uint8_t i = 0; i < 16; ++i) {
    data.network.networkKey[i] = i;
    data.network.applicationKey[i] = static_cast<uint8_t>(0xff - i);
  }
  studio::mesh::NodeRecord node;
  node.instanceId = 42;
  node.model = studio::DriverId::AputureLight;
  node.unicastAddress = 2;
  node.configured = true;
  node.controlGroupAddress = 0xc001;
  node.vendorCompanyId = 0x03f6;
  node.vendorModelId = 0x1000;
  node.configurationVersion = studio::mesh::kCurrentConfigurationVersion;
  TEST_ASSERT_TRUE(studio::mesh::upsertNode(data, node));
  studio::mesh::NodeRecord zhiyun;
  zhiyun.instanceId = 43;
  zhiyun.model = studio::DriverId::ZhiyunLight;
  zhiyun.unicastAddress = 3;
  zhiyun.configured = true;
  zhiyun.routingSelector =
      studio::mesh::nextZhiyunRoutingSelector(data);
  TEST_ASSERT_EQUAL_UINT8(0, zhiyun.routingSelector);
  TEST_ASSERT_TRUE(studio::mesh::upsertNode(data, zhiyun));
  TEST_ASSERT_EQUAL_UINT8(1,
      studio::mesh::nextZhiyunRoutingSelector(data));
  TEST_ASSERT_TRUE(store.save(data));
  studio::mesh::StoreData loaded;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::ConfigLoadStatus::Loaded),
                        static_cast<int>(store.load(loaded)));
  const studio::mesh::NodeRecord* loadedNode =
      studio::mesh::findNode(loaded, 42);
  TEST_ASSERT_NOT_NULL(loadedNode);
  TEST_ASSERT_EQUAL_HEX16(0xc001, loadedNode->controlGroupAddress);
  TEST_ASSERT_EQUAL_HEX16(0x03f6, loadedNode->vendorCompanyId);
  TEST_ASSERT_EQUAL_HEX16(0x1000, loadedNode->vendorModelId);
  TEST_ASSERT_EQUAL_UINT8(studio::mesh::kCurrentConfigurationVersion,
                          loadedNode->configurationVersion);
  TEST_ASSERT_EQUAL_HEX16(
      0xc001, studio::mesh::defaultControlGroupAddress(loaded, *loadedNode));
  const studio::mesh::NodeRecord fallbackGroupNode = {
      44, studio::DriverId::AputureLight, 4};
  TEST_ASSERT_EQUAL_HEX16(
      0xc003,
      studio::mesh::defaultControlGroupAddress(loaded, fallbackGroupNode));
  const studio::mesh::NodeRecord secondGroupNode = {
      45, studio::DriverId::AputureLight, 5};
  TEST_ASSERT_EQUAL_HEX16(
      0xc004,
      studio::mesh::defaultControlGroupAddress(loaded, secondGroupNode));
  TEST_ASSERT_NOT_EQUAL(
      studio::mesh::defaultControlGroupAddress(loaded, fallbackGroupNode),
      studio::mesh::defaultControlGroupAddress(loaded, secondGroupNode));
  TEST_ASSERT_EQUAL_HEX16(
      0xc001, studio::mesh::memberControlGroupAddress(loaded, 42));
  TEST_ASSERT_EQUAL_HEX16(
      0, studio::mesh::memberControlGroupAddress(loaded, 999));
  studio::mesh::NodeRecord pendingMcPro;
  pendingMcPro.instanceId = 44;
  pendingMcPro.model = studio::DriverId::AputureLight;
  pendingMcPro.unicastAddress = 4;
  TEST_ASSERT_TRUE(studio::mesh::upsertNode(loaded, pendingMcPro));
  TEST_ASSERT_TRUE(studio::mesh::assignVendorModel(
      loaded, 44, 0x03f6, 0x1000));
  studio::mesh::NodeRecord* identifiedMcPro =
      studio::mesh::findNode(loaded, 44);
  TEST_ASSERT_NOT_NULL(identifiedMcPro);
  TEST_ASSERT_EQUAL_HEX16(0x03f6, identifiedMcPro->vendorCompanyId);
  TEST_ASSERT_EQUAL_HEX16(0x1000, identifiedMcPro->vendorModelId);
  TEST_ASSERT_EQUAL_HEX16(0xc003, identifiedMcPro->controlGroupAddress);
  TEST_ASSERT_TRUE(studio::mesh::assignVendorModel(
      loaded, 44, 0x0211, 0x0000));
  TEST_ASSERT_EQUAL_HEX16(0x0211, identifiedMcPro->vendorCompanyId);
  TEST_ASSERT_EQUAL_HEX16(0x0000, identifiedMcPro->vendorModelId);
  identifiedMcPro->configured = true;
  TEST_ASSERT_FALSE(studio::mesh::assignVendorModel(
      loaded, 44, 0x0211, 0x0000));
  std::strcpy(loaded.nodes[0].bleAddress, "aa:bb:cc:dd:ee:01");
  std::strcpy(loaded.nodes[1].bleAddress, "aa:bb:cc:dd:ee:02");
  TEST_ASSERT_TRUE(studio::mesh::isKnownMeshProxyAddress(
      loaded, "aa:bb:cc:dd:ee:01"));
  TEST_ASSERT_TRUE(studio::mesh::isKnownMeshProxyAddress(
      loaded, "aa:bb:cc:dd:ee:02"));
  TEST_ASSERT_FALSE(studio::mesh::isKnownMeshProxyAddress(
      loaded, "aa:bb:cc:dd:ee:03"));
  TEST_ASSERT_FALSE(studio::mesh::isKnownMeshProxyAddress(loaded, nullptr));
  const studio::mesh::NodeRecord* loadedZhiyun =
      studio::mesh::findNode(loaded, 43);
  TEST_ASSERT_NOT_NULL(loadedZhiyun);
  TEST_ASSERT_EQUAL_UINT8(0, loadedZhiyun->routingSelector);
  studio::mesh::SequenceAllocator first;
  TEST_ASSERT_TRUE(first.begin(store, loaded));
  uint32_t sequence = 99;
  TEST_ASSERT_TRUE(first.next(sequence));
  TEST_ASSERT_EQUAL_UINT32(0, sequence);
  studio::mesh::StoreData restarted;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::ConfigLoadStatus::Loaded),
                        static_cast<int>(store.load(restarted)));
  studio::mesh::SequenceAllocator second;
  TEST_ASSERT_TRUE(second.begin(store, restarted));
  TEST_ASSERT_TRUE(second.next(sequence));
  TEST_ASSERT_EQUAL_UINT32(studio::mesh::kSequenceBlockSize, sequence);
}

struct RandomFillContext {
  uint8_t next = 1;
  int calls = 0;
  int failOnCall = 0;
};

bool deterministicRandomFill(uint8_t* output, size_t length, void* context) {
  auto* state = static_cast<RandomFillContext*>(context);
  ++state->calls;
  if (state->failOnCall == state->calls) return false;
  for (size_t i = 0; i < length; ++i) output[i] = state->next++;
  return true;
}

void test_mesh_initialization_is_identity_independent_and_transactional() {
  MemoryBackend backend;
  studio::mesh::Store store(backend);
  studio::mesh::StoreData data;
  data.network.ivIndex = 0x11223344;
  data.network.sequenceHighWater = 0x556677;
  data.nodeCount = 1;
  data.nodes[0].instanceId = 99;
  const studio::mesh::StoreData original = data;

  RandomFillContext rngFailure;
  rngFailure.failOnCall = 2;
  TEST_ASSERT_FALSE(studio::mesh::initializeNewMesh(
      store, data, deterministicRandomFill, &rngFailure));
  TEST_ASSERT_EQUAL_MEMORY(&original, &data, sizeof(data));
  studio::mesh::StoreData absent;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::ConfigLoadStatus::Missing),
      static_cast<int>(store.load(absent)));

  backend.failWrites = true;
  RandomFillContext saveFailure;
  TEST_ASSERT_FALSE(studio::mesh::initializeNewMesh(
      store, data, deterministicRandomFill, &saveFailure));
  TEST_ASSERT_EQUAL_MEMORY(&original, &data, sizeof(data));

  backend.failWrites = false;
  RandomFillContext success;
  TEST_ASSERT_TRUE(studio::mesh::initializeNewMesh(
      store, data, deterministicRandomFill, &success));
  TEST_ASSERT_TRUE(data.network.initialized);
  TEST_ASSERT_EQUAL_UINT16(2, data.network.nextUnicastAddress);
  TEST_ASSERT_EQUAL_UINT32(0, data.network.sequenceHighWater);
  TEST_ASSERT_EQUAL_UINT8(0, data.nodeCount);
  for (uint8_t i = 0; i < 16; ++i) {
    TEST_ASSERT_EQUAL_UINT8(i + 1, data.network.networkKey[i]);
    TEST_ASSERT_EQUAL_UINT8(i + 17, data.network.applicationKey[i]);
  }
  studio::mesh::StoreData loaded;
  TEST_ASSERT_EQUAL_INT(static_cast<int>(studio::ConfigLoadStatus::Loaded),
                        static_cast<int>(store.load(loaded)));
  TEST_ASSERT_TRUE(loaded.network.initialized);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(data.network.networkKey,
                                loaded.network.networkKey, 16);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(data.network.applicationKey,
                                loaded.network.applicationKey, 16);
  TEST_ASSERT_EQUAL_UINT16(data.network.nextUnicastAddress,
                           loaded.network.nextUnicastAddress);
  TEST_ASSERT_EQUAL_UINT32(data.network.sequenceHighWater,
                           loaded.network.sequenceHighWater);
  TEST_ASSERT_EQUAL_UINT8(0, loaded.nodeCount);

  MemoryBackend secondBackend;
  studio::mesh::Store secondStore(secondBackend);
  studio::mesh::StoreData secondPanel;
  RandomFillContext secondEntropy;
  secondEntropy.next = 91;
  TEST_ASSERT_TRUE(studio::mesh::initializeNewMesh(
      secondStore, secondPanel, deterministicRandomFill, &secondEntropy));
  TEST_ASSERT_NOT_EQUAL(0, std::memcmp(data.network.networkKey,
                                      secondPanel.network.networkKey, 16));
  TEST_ASSERT_NOT_EQUAL(0, std::memcmp(data.network.applicationKey,
                                      secondPanel.network.applicationKey, 16));

  studio::mesh::StoreData replacement = data;
  replacement.nodeCount = 1;
  replacement.nodes[0].instanceId = 73;
  const studio::mesh::StoreData beforeReplacement = data;
  backend.failWrites = true;
  TEST_ASSERT_FALSE(
      studio::mesh::persistReplacement(store, data, replacement));
  TEST_ASSERT_EQUAL_MEMORY(&beforeReplacement, &data, sizeof(data));
  backend.failWrites = false;
  TEST_ASSERT_TRUE(studio::mesh::persistReplacement(store, data, replacement));
  TEST_ASSERT_EQUAL_UINT8(1, data.nodeCount);
  TEST_ASSERT_EQUAL_UINT32(73, data.nodes[0].instanceId);
}

studio::ble::Advertisement onboardingAdvertisement(const char* address,
                                                   uint8_t addressType,
                                                   int8_t rssi,
                                                   uint8_t marker) {
  studio::ble::Advertisement advertisement;
  std::strncpy(advertisement.address.value, address,
               sizeof(advertisement.address.value) - 1);
  advertisement.address.type = addressType;
  advertisement.rssi = rssi;
  advertisement.payload[0] = 2;
  advertisement.payload[1] = 0xff;
  advertisement.payload[2] = marker;
  advertisement.payloadLength = 3;
  return advertisement;
}

void test_onboarding_candidates_are_bounded_deduplicated_and_stable() {
  studio::ble::OnboardingCandidates candidates;
  TEST_ASSERT_TRUE(candidates.observe(
      onboardingAdvertisement("aa:00:00:00:00:01", 0, -60, 1)));
  const uint32_t firstToken = candidates.at(0)->token;
  TEST_ASSERT_FALSE(candidates.observe(
      onboardingAdvertisement("aa:00:00:00:00:01", 0, -60, 1)));
  TEST_ASSERT_TRUE(candidates.observe(
      onboardingAdvertisement("aa:00:00:00:00:01", 0, -42, 1)));
  TEST_ASSERT_EQUAL_UINT32(firstToken, candidates.at(0)->token);

  // Address type is part of identity, so a public/random pair is not merged.
  TEST_ASSERT_TRUE(candidates.observe(
      onboardingAdvertisement("aa:00:00:00:00:01", 1, -50, 2)));
  TEST_ASSERT_TRUE(candidates.observe(
      onboardingAdvertisement("aa:00:00:00:00:02", 0, -70, 3)));
  TEST_ASSERT_TRUE(candidates.observe(
      onboardingAdvertisement("aa:00:00:00:00:03", 0, -65, 4)));
  TEST_ASSERT_EQUAL_UINT32(4, candidates.count());

  TEST_ASSERT_FALSE(candidates.observe(
      onboardingAdvertisement("aa:00:00:00:00:04", 0, -90, 5)));
  TEST_ASSERT_NOT_NULL(candidates.find(firstToken));
  const uint32_t weakestToken = candidates.at(2)->token;
  TEST_ASSERT_TRUE(candidates.observe(
      onboardingAdvertisement("aa:00:00:00:00:04", 0, -30, 5)));
  TEST_ASSERT_EQUAL_UINT32(4, candidates.count());
  TEST_ASSERT_NULL(candidates.find(weakestToken));
  TEST_ASSERT_NOT_NULL(candidates.find(firstToken));
}

void test_single_onboarding_candidate_auto_selects_after_settling() {
  studio::ble::OnboardingAutoSelect selector;
  using Decision = studio::ble::OnboardingAutoSelect::Decision;

  TEST_ASSERT_EQUAL_INT(static_cast<int>(Decision::Wait),
                        static_cast<int>(selector.update(0, 0, 100)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Decision::Wait),
                        static_cast<int>(selector.update(1, 42, 200)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Decision::Wait),
                        static_cast<int>(selector.update(1, 42, 949)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Decision::Select),
                        static_cast<int>(selector.update(1, 42, 950)));

  selector.selectionFailed(42);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Decision::ShowPicker),
                        static_cast<int>(selector.update(1, 42, 951)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Decision::ShowPicker),
                        static_cast<int>(selector.update(2, 0, 952)));

  TEST_ASSERT_EQUAL_INT(static_cast<int>(Decision::Wait),
                        static_cast<int>(selector.update(1, 73, 1000)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Decision::Select),
                        static_cast<int>(selector.update(1, 73, 1750)));
}

void test_onboarding_selection_failure_cancel_and_protocol_ready_commit_gate() {
  MemoryBackend backend;
  FakeDriver driver(studio::DriverId::CanonBle);
  driver.onboardingCandidateAvailable = true;
  studio::DeviceDriver* drivers[] = {&driver};
  studio::DeviceManager devices(backend, drivers, 1);
  TEST_ASSERT_TRUE(devices.begin());
  studio::InstanceId id = studio::kInvalidInstanceId;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(devices.beginAdd(studio::DriverId::CanonBle,
                                        "Candidate Light", id)));
  TEST_ASSERT_TRUE(devices.acquire(id, studio::ConnectionOwner::Foreground));
  studio::OnboardingCandidate candidate;
  TEST_ASSERT_EQUAL_UINT32(1, devices.onboardingCandidateCount(id));
  TEST_ASSERT_TRUE(devices.onboardingCandidate(id, 0, candidate));

  driver.onboardingSelectionFails = true;
  TEST_ASSERT_FALSE(devices.selectOnboardingCandidate(id, candidate.token));
  TEST_ASSERT_TRUE(driver.onboardingScanning);
  TEST_ASSERT_FALSE(driver.onboardingSelected);
  TEST_ASSERT_EQUAL_UINT32(0, devices.count());

  driver.onboardingSelectionFails = false;
  TEST_ASSERT_TRUE(devices.selectOnboardingCandidate(id, candidate.token));
  driver.ready = false;
  driver.pairingUpdatePending = true;
  devices.loop();
  TEST_ASSERT_TRUE(devices.isPendingAdd(id));
  TEST_ASSERT_EQUAL_UINT32(0, devices.count());
  TEST_ASSERT_FALSE(backend.containsText("Candidate Light"));

  driver.cancelOnboardingSucceeds = false;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Invalid),
      static_cast<int>(devices.cancelPendingAdd(id)));
  TEST_ASSERT_TRUE(devices.isPendingAdd(id));
  TEST_ASSERT_TRUE(devices.isActive(id));

  driver.cancelOnboardingSucceeds = true;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::RegistryStatus::Ok),
      static_cast<int>(devices.cancelPendingAdd(id)));
  TEST_ASSERT_FALSE(driver.onboardingScanning);
  TEST_ASSERT_EQUAL_UINT32(0, devices.count());
  TEST_ASSERT_FALSE(backend.containsText("Candidate Light"));
}

void test_mesh_store_round_trip_at_device_capacity() {
  MemoryBackend backend;
  studio::mesh::Store store(backend);
  studio::mesh::StoreData data;
  data.network.initialized = true;
  for (size_t i = 0; i < CONFIG_MAX_DEVICE_INSTANCES; ++i) {
    studio::mesh::NodeRecord node;
    node.instanceId = static_cast<studio::InstanceId>(i + 1);
    node.model = studio::DriverId::AputureLight;
    node.unicastAddress = static_cast<uint16_t>(i + 2);
    node.configured = true;
    TEST_ASSERT_TRUE(studio::mesh::upsertNode(data, node));
  }
  TEST_ASSERT_EQUAL_UINT32(CONFIG_MAX_DEVICE_INSTANCES, data.nodeCount);
  TEST_ASSERT_TRUE(store.save(data));

  studio::mesh::StoreData restored;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(studio::ConfigLoadStatus::Loaded),
      static_cast<int>(store.load(restored)));
  TEST_ASSERT_EQUAL_UINT32(CONFIG_MAX_DEVICE_INSTANCES, restored.nodeCount);
  TEST_ASSERT_NOT_NULL(
      studio::mesh::findNode(restored, CONFIG_MAX_DEVICE_INSTANCES));
}

void test_zhiyun_light_frames_and_confirmed_state_replies() {
  const zhiyun_light::FrameBytes brightnessRead =
      zhiyun_light::buildReadRequest(6, zhiyun_light::kCommandBrightness);
  const uint8_t expectedRead[] = {
      0x24,0x3c,0x0d,0x00,0x00,0x01,0x06,0x00,0x01,0x10,
      0x00,0x80,0x00,0x00,0x00,0x00,0x00,0x36,0xf0};
  TEST_ASSERT_EQUAL_UINT32(sizeof(expectedRead), brightnessRead.length);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expectedRead, brightnessRead.bytes,
                                sizeof(expectedRead));

  const zhiyun_light::FrameBytes powerOff =
      zhiyun_light::buildPowerWrite(0x0172, false);
  const uint8_t expectedPowerOff[] = {
      0x24,0x3c,0x0a,0x00,0x00,0x01,0x72,0x01,0x08,0x10,
      0x00,0x80,0x01,0x00,0x9b,0x6d};
  TEST_ASSERT_EQUAL_UINT32(sizeof(expectedPowerOff), powerOff.length);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expectedPowerOff, powerOff.bytes,
                                sizeof(expectedPowerOff));

  const uint8_t brightnessReply[] = {
      0x24,0x3c,0x0d,0x00,0x01,0x00,0x06,0x00,0x01,0x10,
      0x00,0x80,0x00,0x00,0x00,0x40,0x40,0x28,0xf3};
  zhiyun_light::FrameScanner scanner;
  unsigned callbacks = 0;
  scanner.feed(brightnessReply, 7,
               [&](const zhiyun_light::ParsedFrame&) { ++callbacks; });
  scanner.feed(brightnessReply + 7, sizeof(brightnessReply) - 7,
               [&](const zhiyun_light::ParsedFrame& frame) {
                 ++callbacks;
                 float brightness = -1.0f;
                 TEST_ASSERT_TRUE(zhiyun_light::parseBrightness(frame,
                                                               brightness));
                 TEST_ASSERT_FLOAT_WITHIN(0.001f, 3.0f, brightness);
                 TEST_ASSERT_EQUAL_UINT16(6, frame.sequence);
               });
  TEST_ASSERT_EQUAL_UINT32(1, callbacks);
  TEST_ASSERT_TRUE(zhiyun_light::validCctCommand(2700, 0, 0));
  TEST_ASSERT_TRUE(zhiyun_light::validCctCommand(6500, 100, 0));
  TEST_ASSERT_FALSE(zhiyun_light::validCctCommand(5600, 50, 1));
  TEST_ASSERT_EQUAL_UINT16(4500, zhiyun_light::normalizeCct(4549));
  TEST_ASSERT_EQUAL_UINT16(4600, zhiyun_light::normalizeCct(4550));
  const zhiyun_light::FrameBytes capturedCct =
      zhiyun_light::buildCctWrite(0x000c, 5450);
  const uint8_t expectedCct[] = {
      0x24,0x3c,0x0b,0x00,0x00,0x01,0x0c,0x00,0x02,0x10,
      0x00,0x80,0x01,0x4a,0x15,0xa9,0xea};
  TEST_ASSERT_EQUAL_UINT32(sizeof(expectedCct), capturedCct.length);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expectedCct, capturedCct.bytes,
                                sizeof(expectedCct));
  TEST_ASSERT_EQUAL_UINT32(0,
      zhiyun_light::buildCctWrite(1, 2699).length);

  const zhiyun_light::FrameBytes x60Cct =
      zhiyun_light::buildCctWrite(0x000b, 5100, 1);
  const uint8_t expectedX60Cct[] = {
      0x24,0x3c,0x0b,0x00,0x00,0x01,0x0b,0x00,0x02,0x10,
      0x01,0x80,0x01,0xec,0x13,0x4d,0x26};
  TEST_ASSERT_EQUAL_UINT32(sizeof(expectedX60Cct), x60Cct.length);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expectedX60Cct, x60Cct.bytes,
                                sizeof(expectedX60Cct));
  const zhiyun_light::FrameBytes x60Hue =
      zhiyun_light::buildHueWrite(0x013b, 240.0f);
  const uint8_t expectedX60Hue[] = {
      0x24,0x3c,0x0d,0x00,0x00,0x01,0x3b,0x01,0x04,0x10,
      0x01,0x80,0x01,0x00,0x00,0x70,0x43,0xb6,0x5e};
  TEST_ASSERT_EQUAL_UINT32(sizeof(expectedX60Hue), x60Hue.length);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expectedX60Hue, x60Hue.bytes,
                                sizeof(expectedX60Hue));
  const uint8_t x60SaturationReply[] = {
      0x24,0x3c,0x0d,0x00,0x01,0x00,0x39,0x01,0x05,0x10,
      0x01,0x80,0x01,0x00,0x00,0xc8,0x42,0xb5,0xd4};
  bool parsedSaturation = false;
  scanner.feed(x60SaturationReply, sizeof(x60SaturationReply),
               [&](const zhiyun_light::ParsedFrame& frame) {
                 float saturation = -1.0f;
                 parsedSaturation = zhiyun_light::parseSaturation(
                     frame, saturation, 1, true);
                 TEST_ASSERT_FLOAT_WITHIN(0.001f, 100.0f, saturation);
               });
  TEST_ASSERT_TRUE(parsedSaturation);
  uint16_t hue = 99;
  uint8_t saturation = 99;
  zhiyun_light::rgbToHsv(0x0000ff, hue, saturation);
  TEST_ASSERT_EQUAL_UINT16(240, hue);
  TEST_ASSERT_EQUAL_UINT8(100, saturation);
  struct CapturedSwatch {
    uint32_t rgb;
    uint16_t hue;
  };
  const CapturedSwatch capturedSwatches[] = {
      {0xff0000, 0},   {0x0000ff, 240}, {0xff00ff, 300},
      {0x00ffff, 180}, {0xff8000, 30},  {0x00ff00, 120},
  };
  for (const CapturedSwatch& swatch : capturedSwatches) {
    zhiyun_light::rgbToHsv(swatch.rgb, hue, saturation);
    TEST_ASSERT_EQUAL_UINT16(swatch.hue, hue);
    TEST_ASSERT_EQUAL_UINT8(100, saturation);
  }
}

void test_zhiyun_light_identity_and_advertisement_match() {
  const uint8_t identityReply[] = {
      0x24,0x3c,0x22,0x00,0x01,0x00,0x02,0x00,0x03,0x20,
      0x30,0x39,0x30,0x32,0x30,0x37,0x65,0x30,0x63,0x33,
      0x31,0x32,0x30,0x32,0x35,0x30,0x00,0x70,0x6c,0x31,
      0x30,0x35,0x00,0x00,0x00,0x00,0x00,0x00,
      0xf3,0xc6};
  zhiyun_light::FrameScanner scanner;
  bool identified = false;
  scanner.feed(identityReply, sizeof(identityReply),
               [&](const zhiyun_light::ParsedFrame& frame) {
                 identified = zhiyun_light::identityIsX100(frame);
               });
  TEST_ASSERT_TRUE(identified);
  const uint8_t x60IdentityReply[] = {
      0x24,0x3c,0x22,0x00,0x01,0x00,0x02,0x00,0x03,0x20,
      0x30,0x39,0x63,0x37,0x30,0x63,0x34,0x30,0x65,0x35,
      0x31,0x32,0x30,0x30,0x37,0x31,0x00,0x70,0x6c,0x78,
      0x31,0x30,0x34,0x00,0x00,0x00,0x00,0x00,0xce,0xd6};
  bool identifiedX60 = false;
  scanner.feed(x60IdentityReply, sizeof(x60IdentityReply),
               [&](const zhiyun_light::ParsedFrame& frame) {
                 identifiedX60 =
                     zhiyun_light::identityContains(frame, "plx104");
               });
  TEST_ASSERT_TRUE(identifiedX60);
  const studio::ble::Advertisement x100 =
      bleAdvertisement("44:55:66:77:88:99", "PL105_4BF3", 0x1828);
  TEST_ASSERT_TRUE(zhiyun_light::matchesAdvertisement(x100));
  const studio::ble::Advertisement unprovisioned =
      bleAdvertisement("44:55:66:77:88:99", "PL105_4BF3", 0x1827);
  TEST_ASSERT_FALSE(zhiyun_light::matchesAdvertisement(unprovisioned));
  TEST_ASSERT_TRUE(
      zhiyun_light::matchesUnprovisionedAdvertisement(unprovisioned));
  const studio::ble::Advertisement x60 =
      bleAdvertisement("55:66:77:88:99:aa", "X104_C957", 0x1828);
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(zhiyun_light::MolusModel::X60Rgb),
      static_cast<int>(zhiyun_light::advertisementModel(x60)));
  TEST_ASSERT_TRUE(zhiyun_light::matchesMolusAdvertisement(x60, true));

  uint8_t networkKey[16];
  for (uint8_t i = 0; i < sizeof(networkKey); ++i) networkKey[i] = i + 1;
  TEST_ASSERT_TRUE(zhiyun_light::matchesSelectedProvisionedAdvertisement(
      x100, x100.address, networkKey));
  studio::ble::Address selectedElsewhere =
      bleAddress("66:77:88:99:aa:bb", x100.address.type);
  TEST_ASSERT_FALSE(zhiyun_light::matchesSelectedProvisionedAdvertisement(
      x100, selectedElsewhere, networkKey));
  studio::ble::Advertisement matchingMesh = x100;
  matchingMesh.address = bleAddress("77:88:99:aa:bb:cc");
  uint8_t networkId[8];
  aputure_light::meshK3(networkKey, networkId);
  const uint8_t serviceDataHeader[] = {12, 0x16, 0x28, 0x18, 0x00};
  std::memcpy(matchingMesh.payload + matchingMesh.payloadLength,
              serviceDataHeader, sizeof(serviceDataHeader));
  matchingMesh.payloadLength += sizeof(serviceDataHeader);
  std::memcpy(matchingMesh.payload + matchingMesh.payloadLength, networkId,
              sizeof(networkId));
  matchingMesh.payloadLength += sizeof(networkId);
  TEST_ASSERT_TRUE(zhiyun_light::matchesSelectedProvisionedAdvertisement(
      matchingMesh, selectedElsewhere, networkKey));
  TEST_ASSERT_TRUE(
      aputure_light::matchesMeshProxyNetwork(matchingMesh, networkKey));
  matchingMesh.payload[matchingMesh.payloadLength - 1] ^= 1;
  TEST_ASSERT_FALSE(zhiyun_light::matchesSelectedProvisionedAdvertisement(
      matchingMesh, selectedElsewhere, networkKey));
  TEST_ASSERT_FALSE(
      aputure_light::matchesMeshProxyNetwork(matchingMesh, networkKey));
  TEST_ASSERT_FALSE(aputure_light::matchesMeshProxyNetwork(x100, networkKey));
  studio::ble::Advertisement manufacturerOnly =
      bleAdvertisement("44:55:66:77:88:99", "Other", 0x1827);
  const uint8_t manufacturer[] = {
      8, 0xff, 0x05, 0x09, 'p', 'l', '1', '0', '5'};
  std::memcpy(manufacturerOnly.payload + manufacturerOnly.payloadLength,
              manufacturer, sizeof(manufacturer));
  manufacturerOnly.payloadLength += sizeof(manufacturer);
  TEST_ASSERT_TRUE(zhiyun_light::matchesUnprovisionedAdvertisement(
      manufacturerOnly));
  manufacturerOnly.payload[manufacturerOnly.payloadLength - 1] = '6';
  TEST_ASSERT_FALSE(zhiyun_light::matchesUnprovisionedAdvertisement(
      manufacturerOnly));
}

void test_mesh_no_oob_policy_accepts_x100_static_oob_capability() {
  const uint8_t x100Capabilities[] = {
      0x01, 0x01, 0x00, 0x01, 0x00, 0x01,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  TEST_ASSERT_TRUE(studio::mesh::supportsNoOobProvisioning(
      x100Capabilities, sizeof(x100Capabilities)));
  uint8_t unsupported[sizeof(x100Capabilities)];
  std::memcpy(unsupported, x100Capabilities, sizeof(unsupported));
  unsupported[4] = 1;
  TEST_ASSERT_FALSE(studio::mesh::supportsNoOobProvisioning(
      unsupported, sizeof(unsupported)));
}

void test_proxy_transport_prefers_a_zhiyun_qualified_gateway() {
  studio::mesh::StoreData store;
  store.nodeCount = 2;
  store.nodes[0].instanceId = 41;
  store.nodes[0].model = studio::DriverId::AputureLight;
  store.nodes[1].instanceId = 42;
  store.nodes[1].model = studio::DriverId::ZhiyunLight;

  studio::mesh::ProxyTransport transport;
  TEST_ASSERT_TRUE(transport.acquire(41));
  TEST_ASSERT_EQUAL_UINT32(41, transport.preferred(store));
  TEST_ASSERT_FALSE(transport.requiresZhiyunService(store));
  TEST_ASSERT_TRUE(transport.acquire(42));
  TEST_ASSERT_EQUAL_UINT32(42, transport.preferred(store));
  TEST_ASSERT_TRUE(transport.requiresZhiyunService(store));
  transport.release(42);
  TEST_ASSERT_EQUAL_UINT32(41, transport.preferred(store));
  TEST_ASSERT_FALSE(transport.empty());
  transport.release(41);
  TEST_ASSERT_TRUE(transport.empty());
}

}  // namespace
