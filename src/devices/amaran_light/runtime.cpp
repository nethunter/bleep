#include "devices/amaran_light/runtime.h"

#ifdef UI_SIMULATOR

#include <cstring>

namespace amaran_light {
namespace {
AmaranRuntime instance;
}
AmaranRuntime& runtime() { return instance; }
AmaranRuntime::Session* AmaranRuntime::sessionFor(studio::InstanceId id) {
  for (auto& session : sessions_) if (session.instanceId == id) return &session;
  return nullptr;
}
const AmaranRuntime::Session* AmaranRuntime::sessionFor(studio::InstanceId id) const {
  for (const auto& session : sessions_) if (session.instanceId == id) return &session;
  return nullptr;
}
bool AmaranRuntime::activate(const studio::DeviceRecord& record) {
  Session* session = sessionFor(record.instanceId);
  if (session == nullptr) for (auto& candidate : sessions_) if (candidate.instanceId == studio::kInvalidInstanceId) { session = &candidate; break; }
  if (session == nullptr) return false;
  session->instanceId = record.instanceId;
  session->model = record.driverId;
  session->state.phase = record.paired ? AmaranLightState::Phase::Ready
                                       : AmaranLightState::Phase::Scanning;
  session->state.proxyConnected = record.paired;
  return true;
}
void AmaranRuntime::deactivate(studio::InstanceId id) { if (auto* s = sessionFor(id)) *s = Session{}; }
void AmaranRuntime::loop() {}
studio::CommandStatus AmaranRuntime::dispatch(const studio::DeviceCommand& command) {
  Session* s = sessionFor(command.instanceId);
  if (s == nullptr) return studio::CommandStatus::Unavailable;
  if (command.type == studio::CommandType::TurnOn || command.type == studio::CommandType::TurnOff) {
    s->state.on = command.type == studio::CommandType::TurnOn; s->state.optimistic = true; return studio::CommandStatus::Succeeded;
  }
  if (command.type == studio::CommandType::SetLightCct && validCctCommand(command.value0, command.value1, command.value2)) {
    s->state.mode = AmaranLightState::Mode::Cct; s->state.kelvin = command.value0; s->state.cctBrightness = command.value1; s->state.tintPermille = command.value2; s->state.optimistic = true; return studio::CommandStatus::Succeeded;
  }
  if (command.type == studio::CommandType::SetLightRgb && validRgbCommand(command.value0, command.value1)) {
    s->state.mode = AmaranLightState::Mode::Rgb; s->state.rgb = command.value0; s->state.rgbBrightness = command.value1; s->state.optimistic = true; return studio::CommandStatus::Succeeded;
  }
  return studio::CommandStatus::Unsupported;
}
studio::DeviceRuntimeState AmaranRuntime::runtimeState(studio::InstanceId id) const {
  studio::DeviceRuntimeState out; const Session* s = sessionFor(id); if (!s) return out;
  if (s->state.phase == AmaranLightState::Phase::Scanning) {
    out.link = studio::LinkState::Scanning;
  } else if (s->state.phase == AmaranLightState::Phase::Ready) {
    out.link = studio::LinkState::Connected;
    out.protocolReady = true;
  } else {
    out.link = studio::LinkState::Connecting;
  }
  out.quality = s->state.optimistic ? studio::StateQuality::Optimistic
                                    : studio::StateQuality::Unknown;
  return out;
}
const AmaranLightState* AmaranRuntime::state(studio::InstanceId id) const { const Session* s = sessionFor(id); return s ? &s->state : nullptr; }
bool AmaranRuntime::consumePairingUpdate(studio::InstanceId, studio::DeviceRecord&) { return false; }
void AmaranRuntime::forgetLocal(studio::InstanceId id) { if (auto* s = sessionFor(id)) s->state = AmaranLightState{}; }
void AmaranRuntime::onBleAdvertisement(studio::ble::LinkHandle, const studio::ble::Advertisement&) {}
void AmaranRuntime::onBleEvent(studio::ble::LinkHandle, const studio::ble::Event&) {}
void AmaranRuntime::enqueueNotification(const uint8_t*, size_t) {}
void AmaranRuntime::simSetPhase(studio::InstanceId id,
                                AmaranLightState::Phase phase) {
  if (Session* session = sessionFor(id)) {
    session->state.phase = phase;
    session->state.proxyConnected = phase == AmaranLightState::Phase::Ready;
  }
}
bool AmaranRuntime::ensureLoaded() { return true; }
bool AmaranRuntime::beginLink(studio::InstanceId, bool) { return true; }
bool AmaranRuntime::setupProvisioning() { return false; }
bool AmaranRuntime::setupProxy() { return false; }
void AmaranRuntime::processNotification(const Notification&) {}
bool AmaranRuntime::sendProvisioning(const uint8_t*, size_t) { return false; }
bool AmaranRuntime::handleCapabilities(const uint8_t*, size_t) { return false; }
bool AmaranRuntime::handleDevicePublicKey(const uint8_t*, size_t) { return false; }
bool AmaranRuntime::handleDeviceConfirmation(const uint8_t*, size_t) { return false; }
bool AmaranRuntime::handleDeviceRandom(const uint8_t*, size_t) { return false; }
bool AmaranRuntime::completeProvisioning() { return false; }
bool AmaranRuntime::configureNext() { return false; }
bool AmaranRuntime::sendAccess(studio::InstanceId, const uint8_t*, size_t) { return false; }
void AmaranRuntime::fail(Session& s, const char* error) { s.state.phase = AmaranLightState::Phase::Failed; std::strncpy(s.state.error, error, sizeof(s.state.error)-1); }
void AmaranRuntime::updateSharedReady() {}
}  // namespace amaran_light

#else

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <esp_random.h>
#include <mbedtls/ecdh.h>

#include <cstring>

#include "core/ble/ble_runtime.h"
#include "core/preferences_store.h"
#include "devices/amaran_light/crypto.h"
#include "devices/amaran_light/protocol.h"

namespace amaran_light {
namespace {

constexpr const char* kProvisionService = "00001827-0000-1000-8000-00805f9b34fb";
constexpr const char* kProvisionAdvertisedService = "1827";
constexpr const char* kProvisionIn = "00002adb-0000-1000-8000-00805f9b34fb";
constexpr const char* kProvisionOut = "00002adc-0000-1000-8000-00805f9b34fb";
constexpr const char* kProxyService = "00001828-0000-1000-8000-00805f9b34fb";
constexpr const char* kProxyAdvertisedService = "1828";
constexpr const char* kProxyIn = "00002add-0000-1000-8000-00805f9b34fb";
constexpr const char* kProxyOut = "00002ade-0000-1000-8000-00805f9b34fb";

AmaranRuntime instance;
AmaranRuntime* activeRuntime = nullptr;
mbedtls_ecp_group provisionGroup;
mbedtls_mpi provisionPrivate;
mbedtls_ecp_point provisionPublic;
bool provisionKeyActive = false;

int randomCallback(void*, unsigned char* output, size_t length) {
  esp_fill_random(output, length);
  return 0;
}

void notificationCallback(NimBLERemoteCharacteristic*, uint8_t* data,
                          size_t length, bool) {
  if (activeRuntime != nullptr) activeRuntime->enqueueNotification(data, length);
}

void closeProvisionKey() {
  if (!provisionKeyActive) return;
  mbedtls_ecp_point_free(&provisionPublic);
  mbedtls_mpi_free(&provisionPrivate);
  mbedtls_ecp_group_free(&provisionGroup);
  provisionKeyActive = false;
}

studio::ble::Address addressFrom(const char* value, uint8_t type) {
  studio::ble::Address address;
  std::strncpy(address.value, value != nullptr ? value : "", sizeof(address.value)-1);
  address.type = type;
  return address;
}

}  // namespace

AmaranRuntime& runtime() { return instance; }

AmaranRuntime::Session* AmaranRuntime::sessionFor(studio::InstanceId id) {
  for (auto& session : sessions_) if (session.instanceId == id) return &session;
  return nullptr;
}
const AmaranRuntime::Session* AmaranRuntime::sessionFor(studio::InstanceId id) const {
  for (const auto& session : sessions_) if (session.instanceId == id) return &session;
  return nullptr;
}

bool AmaranRuntime::ensureLoaded() {
  if (loaded_) return true;
  static studio::PreferencesAmaranBackend backend;
  static MeshStore store(backend);
  const studio::ConfigLoadStatus status = store.load(storeData_);
  if (status == studio::ConfigLoadStatus::Corrupt) return false;
  if (status == studio::ConfigLoadStatus::Missing) storeData_ = MeshStoreData{};
  if (!storeData_.network.initialized) {
    esp_fill_random(storeData_.network.networkKey, 16);
    esp_fill_random(storeData_.network.applicationKey, 16);
    storeData_.network.initialized = true;
    if (!store.save(storeData_)) return false;
  }
  sequences_.begin(store, storeData_);
  loaded_ = true;
  return true;
}

bool AmaranRuntime::activate(const studio::DeviceRecord& record) {
  if (!ensureLoaded()) return false;
  Session* session = sessionFor(record.instanceId);
  if (session == nullptr) {
    for (auto& candidate : sessions_) if (candidate.instanceId == studio::kInvalidInstanceId) { session = &candidate; break; }
  }
  if (session == nullptr) return false;
  session->instanceId = record.instanceId;
  session->model = record.driverId;
  const MeshNodeRecord* node = findNode(storeData_, record.instanceId);
  session->state.phase = node == nullptr ? AmaranLightState::Phase::Scanning
                                         : (node->configured ? AmaranLightState::Phase::ConnectingProxy
                                                             : AmaranLightState::Phase::PendingConfig);
  if (link_ == studio::ble::kInvalidLinkHandle) return beginLink(record.instanceId, node == nullptr);
  if (node == nullptr) {
    linkInstance_ = record.instanceId;
    provisioningLink_ = true;
    if (connected_) studio::ble::bleCentral().disconnect(link_, false);
    else studio::ble::bleCentral().requestScan(link_, true);
  } else if (!provisioningLink_) {
    linkInstance_ = record.instanceId;
    if (connected_ && !node->configured) {
      configStep_ = 0;
      nextConfigAt_ = millis();
    } else if (connected_) {
      updateSharedReady();
    } else {
      studio::ble::bleCentral().requestScan(link_, true);
    }
  }
  return true;
}

void AmaranRuntime::deactivate(studio::InstanceId id) {
  if (Session* session = sessionFor(id)) *session = Session{};
  bool any = false;
  for (const auto& session : sessions_) any = any || session.instanceId != studio::kInvalidInstanceId;
  if (!any && link_ != studio::ble::kInvalidLinkHandle) {
    studio::ble::bleCentral().release(link_);
    link_ = studio::ble::kInvalidLinkHandle;
    connected_ = false;
    dataIn_ = nullptr;
    activeRuntime = nullptr;
  }
}

bool AmaranRuntime::beginLink(studio::InstanceId id, bool provisioning) {
  studio::ble::ConnectPolicy policy;
  policy.security = studio::ble::SecurityPolicy::None;
  policy.diagnosticTag = "amaran_mesh";
  if (link_ == studio::ble::kInvalidLinkHandle) {
    link_ = studio::ble::bleCentral().acquire(*this, policy);
    if (link_ == studio::ble::kInvalidLinkHandle) return false;
  }
  linkInstance_ = id;
  provisioningLink_ = provisioning;
  activeRuntime = this;
  const MeshNodeRecord* node = findNode(storeData_, id);
  if (!provisioning && node != nullptr && node->bleAddress[0] != '\0') {
    return studio::ble::bleCentral().requestConnect(
        link_, addressFrom(node->bleAddress, node->bleAddressType));
  }
  return studio::ble::bleCentral().requestScan(link_, true);
}

void AmaranRuntime::onBleAdvertisement(
    studio::ble::LinkHandle link,
    const studio::ble::Advertisement& advertisement) {
  if (link != link_) return;
  const char* wanted = provisioningLink_ ? kProvisionAdvertisedService
                                         : kProxyAdvertisedService;
  if (studio::ble::advertisesService(advertisement, wanted)) {
    if (provisioningLink_) {
      std::strncpy(provisioningAddress_, advertisement.address.value,
                   sizeof(provisioningAddress_) - 1);
      provisioningAddressType_ = advertisement.address.type;
    }
    if (Session* session = sessionFor(linkInstance_)) {
      session->state.phase = provisioningLink_
          ? AmaranLightState::Phase::Provisioning
          : AmaranLightState::Phase::ConnectingProxy;
    }
    studio::ble::bleCentral().selectAdvertisement(link_, advertisement);
  }
}

void AmaranRuntime::onBleEvent(studio::ble::LinkHandle link,
                               const studio::ble::Event& event) {
  if (link != link_) return;
  if (event.type == studio::ble::EventType::Connected) {
    connected_ = true;
    const bool ok = provisioningLink_ ? setupProvisioning() : setupProxy();
    if (!ok) {
      if (Session* session = sessionFor(linkInstance_)) fail(*session, "GATT setup failed");
      studio::ble::bleCentral().markProtocolFailed(link_);
    }
  } else if (event.type == studio::ble::EventType::Disconnected) {
    connected_ = false;
    dataIn_ = nullptr;
    if (link_ != studio::ble::kInvalidLinkHandle &&
        linkInstance_ != studio::kInvalidInstanceId) {
      studio::ble::bleCentral().requestScan(link_, true);
    }
  } else if (event.type == studio::ble::EventType::ConnectFailed) {
    if (Session* session = sessionFor(linkInstance_)) {
      session->state.phase = AmaranLightState::Phase::Scanning;
    }
  }
}

bool AmaranRuntime::setupProvisioning() {
  NimBLEClient* client = static_cast<NimBLEClient*>(studio::ble::bleCentral().nativeClient(link_));
  if (client == nullptr) return false;
  NimBLERemoteService* service = client->getService(NimBLEUUID(kProvisionService));
  if (service == nullptr) return false;
  dataIn_ = service->getCharacteristic(NimBLEUUID(kProvisionIn));
  NimBLERemoteCharacteristic* out = service->getCharacteristic(NimBLEUUID(kProvisionOut));
  if (dataIn_ == nullptr || out == nullptr || !out->subscribe(true, notificationCallback, true)) return false;
  provisioningStep_ = 1;
  if (Session* session = sessionFor(linkInstance_)) session->state.phase = AmaranLightState::Phase::Provisioning;
  const uint8_t invite[] = {0x00,0x00};
  return sendProvisioning(invite, sizeof(invite));
}

bool AmaranRuntime::setupProxy() {
  NimBLEClient* client = static_cast<NimBLEClient*>(studio::ble::bleCentral().nativeClient(link_));
  if (client == nullptr) return false;
  NimBLERemoteService* service = client->getService(NimBLEUUID(kProxyService));
  if (service == nullptr) return false;
  dataIn_ = service->getCharacteristic(NimBLEUUID(kProxyIn));
  NimBLERemoteCharacteristic* out = service->getCharacteristic(NimBLEUUID(kProxyOut));
  if (dataIn_ == nullptr || out == nullptr || !out->subscribe(true, notificationCallback, true)) return false;
  const MeshNodeRecord* node = findNode(storeData_, linkInstance_);
  if (node != nullptr && !node->configured) {
    configStep_ = 0;
    nextConfigAt_ = millis();
  } else {
    studio::ble::bleCentral().markProtocolReady(link_);
    updateSharedReady();
  }
  return true;
}

void AmaranRuntime::enqueueNotification(const uint8_t* data, size_t length) {
  if (data == nullptr || length == 0 || length > sizeof(notifications_[0].bytes)) return;
  const uint8_t next = static_cast<uint8_t>((notifyHead_ + 1) % 8);
  if (next == notifyTail_) return;
  notifications_[notifyHead_].length = static_cast<uint8_t>(length);
  std::memcpy(notifications_[notifyHead_].bytes, data, length);
  notifyHead_ = next;
}

void AmaranRuntime::loop() {
  const uint32_t now = millis();
  if (lastLoopMs_ == now) return;
  lastLoopMs_ = now;
  while (notifyTail_ != notifyHead_) {
    const Notification notification = notifications_[notifyTail_];
    notifyTail_ = static_cast<uint8_t>((notifyTail_ + 1) % 8);
    processNotification(notification);
  }
  if (connected_ && !provisioningLink_ &&
      findNode(storeData_, linkInstance_) != nullptr &&
      !findNode(storeData_, linkInstance_)->configured &&
      static_cast<int32_t>(now - nextConfigAt_) >= 0) {
    configureNext();
  }
}

bool AmaranRuntime::sendProvisioning(const uint8_t* pdu, size_t length) {
  if (dataIn_ == nullptr || length + 1 > 80) return false;
  uint8_t wrapped[80] = {0x03};
  std::memcpy(wrapped + 1, pdu, length);
  return dataIn_->writeValue(wrapped, length + 1, false);
}

void AmaranRuntime::processNotification(const Notification& notification) {
  if (!provisioningLink_ || notification.length < 2 || notification.bytes[0] != 0x03) return;
  const uint8_t* pdu = notification.bytes + 1;
  const size_t length = notification.length - 1;
  bool ok = true;
  if (pdu[0] == 0x09) ok = false;
  else if (pdu[0] == 0x01 && provisioningStep_ == 1) ok = handleCapabilities(pdu, length);
  else if (pdu[0] == 0x03 && provisioningStep_ == 2) ok = handleDevicePublicKey(pdu, length);
  else if (pdu[0] == 0x05 && provisioningStep_ == 3) ok = handleDeviceConfirmation(pdu, length);
  else if (pdu[0] == 0x06 && provisioningStep_ == 4) ok = handleDeviceRandom(pdu, length);
  else if (pdu[0] == 0x08 && provisioningStep_ == 5) ok = completeProvisioning();
  if (!ok) if (Session* session = sessionFor(linkInstance_)) fail(*session, "Provisioning failed");
}

bool AmaranRuntime::handleCapabilities(const uint8_t* pdu, size_t length) {
  if (length != 12 || (pdu[2] != 0 || (pdu[3] & 1) == 0) || pdu[4] || pdu[5] || pdu[6] || pdu[9]) return false;
  std::memcpy(capabilities_, pdu, 12);
  const uint8_t start[] = {0x02,0,0,0,0,0};
  if (!sendProvisioning(start, sizeof(start))) return false;
  closeProvisionKey();
  mbedtls_ecp_group_init(&provisionGroup);
  mbedtls_mpi_init(&provisionPrivate);
  mbedtls_ecp_point_init(&provisionPublic);
  provisionKeyActive = true;
  if (mbedtls_ecp_group_load(&provisionGroup, MBEDTLS_ECP_DP_SECP256R1) != 0 ||
      mbedtls_ecp_gen_keypair(&provisionGroup, &provisionPrivate, &provisionPublic,
                              randomCallback, nullptr) != 0) return false;
  uint8_t encoded[65]; size_t encodedLength = 0;
  if (mbedtls_ecp_point_write_binary(&provisionGroup, &provisionPublic,
      MBEDTLS_ECP_PF_UNCOMPRESSED, &encodedLength, encoded, sizeof(encoded)) != 0 || encodedLength != 65) return false;
  std::memcpy(localPublic_, encoded + 1, 64);
  uint8_t publicPdu[65] = {0x03}; std::memcpy(publicPdu + 1, localPublic_, 64);
  provisioningStep_ = 2;
  return sendProvisioning(publicPdu, sizeof(publicPdu));
}

bool AmaranRuntime::handleDevicePublicKey(const uint8_t* pdu, size_t length) {
  if (length != 65 || !provisionKeyActive) return false;
  std::memcpy(remotePublic_, pdu + 1, 64);
  uint8_t encoded[65] = {0x04}; std::memcpy(encoded + 1, remotePublic_, 64);
  mbedtls_ecp_point remote; mbedtls_ecp_point_init(&remote);
  mbedtls_mpi secret; mbedtls_mpi_init(&secret);
  const bool ok = mbedtls_ecp_point_read_binary(&provisionGroup, &remote, encoded, sizeof(encoded)) == 0 &&
      mbedtls_ecdh_compute_shared(&provisionGroup, &secret, &remote, &provisionPrivate,
                                  randomCallback, nullptr) == 0 &&
      mbedtls_mpi_write_binary(&secret, ecdhSecret_, sizeof(ecdhSecret_)) == 0;
  mbedtls_mpi_free(&secret); mbedtls_ecp_point_free(&remote);
  if (!ok) return false;
  uint8_t inputs[145]; size_t offset = 0;
  inputs[offset++] = 0;
  std::memcpy(inputs + offset, capabilities_ + 1, 11); offset += 11;
  const uint8_t startParameters[5] = {}; std::memcpy(inputs + offset, startParameters, 5); offset += 5;
  std::memcpy(inputs + offset, localPublic_, 64); offset += 64;
  std::memcpy(inputs + offset, remotePublic_, 64);
  meshS1(inputs, sizeof(inputs), confirmationSalt_);
  const uint8_t prck[] = {'p','r','c','k'};
  meshK1(ecdhSecret_, sizeof(ecdhSecret_), confirmationSalt_, prck, sizeof(prck), confirmationKey_);
  esp_fill_random(localRandom_, sizeof(localRandom_));
  uint8_t material[32]; std::memcpy(material, localRandom_, 16); std::memset(material + 16, 0, 16);
  uint8_t confirmation[16]; aesCmac(confirmationKey_, material, sizeof(material), confirmation);
  uint8_t confirmationPdu[17] = {0x05}; std::memcpy(confirmationPdu + 1, confirmation, 16);
  provisioningStep_ = 3;
  return sendProvisioning(confirmationPdu, sizeof(confirmationPdu));
}

bool AmaranRuntime::handleDeviceConfirmation(const uint8_t* pdu, size_t length) {
  if (length != 17) return false;
  std::memcpy(remoteConfirmation_, pdu + 1, 16);
  uint8_t randomPdu[17] = {0x06}; std::memcpy(randomPdu + 1, localRandom_, 16);
  provisioningStep_ = 4;
  return sendProvisioning(randomPdu, sizeof(randomPdu));
}

bool AmaranRuntime::handleDeviceRandom(const uint8_t* pdu, size_t length) {
  if (length != 17) return false;
  std::memcpy(remoteRandom_, pdu + 1, 16);
  uint8_t material[32]; std::memcpy(material, remoteRandom_, 16); std::memset(material + 16, 0, 16);
  uint8_t expected[16]; aesCmac(confirmationKey_, material, sizeof(material), expected);
  if (std::memcmp(expected, remoteConfirmation_, 16) != 0) return false;
  uint8_t saltMaterial[48]; std::memcpy(saltMaterial, confirmationSalt_, 16);
  std::memcpy(saltMaterial + 16, localRandom_, 16); std::memcpy(saltMaterial + 32, remoteRandom_, 16);
  uint8_t provisioningSalt[16], sessionKey[16], nonceFull[16];
  meshS1(saltMaterial, sizeof(saltMaterial), provisioningSalt);
  const uint8_t prsk[] = {'p','r','s','k'}, prsn[] = {'p','r','s','n'}, prdk[] = {'p','r','d','k'};
  meshK1(ecdhSecret_, 32, provisioningSalt, prsk, 4, sessionKey);
  meshK1(ecdhSecret_, 32, provisioningSalt, prsn, 4, nonceFull);
  meshK1(ecdhSecret_, 32, provisioningSalt, prdk, 4, deviceKey_);
  uint8_t provisioningData[25]; std::memcpy(provisioningData, storeData_.network.networkKey, 16);
  provisioningData[16] = provisioningData[17] = provisioningData[18] = 0;
  provisioningData[19] = static_cast<uint8_t>(storeData_.network.ivIndex >> 24);
  provisioningData[20] = static_cast<uint8_t>(storeData_.network.ivIndex >> 16);
  provisioningData[21] = static_cast<uint8_t>(storeData_.network.ivIndex >> 8);
  provisioningData[22] = static_cast<uint8_t>(storeData_.network.ivIndex);
  provisioningData[23] = static_cast<uint8_t>(storeData_.network.nextUnicastAddress >> 8);
  provisioningData[24] = static_cast<uint8_t>(storeData_.network.nextUnicastAddress);
  uint8_t encrypted[25], tag[8];
  if (!aesCcmEncrypt(sessionKey, nonceFull + 3, 13, provisioningData, sizeof(provisioningData), 8, encrypted, tag)) return false;
  uint8_t dataPdu[34] = {0x07}; std::memcpy(dataPdu + 1, encrypted, 25); std::memcpy(dataPdu + 26, tag, 8);
  provisioningStep_ = 5;
  return sendProvisioning(dataPdu, sizeof(dataPdu));
}

bool AmaranRuntime::completeProvisioning() {
  Session* session = sessionFor(linkInstance_); if (session == nullptr) return false;
  static studio::PreferencesAmaranBackend backend; MeshStore store(backend);
  MeshNodeRecord node; node.instanceId = session->instanceId; node.model = session->model;
  node.unicastAddress = storeData_.network.nextUnicastAddress;
  node.elementCount = capabilities_[1] == 0 ? 1 : capabilities_[1];
  std::memcpy(node.deviceKey, deviceKey_, 16);
  std::strncpy(node.bleAddress, provisioningAddress_, sizeof(node.bleAddress)-1);
  node.bleAddressType = provisioningAddressType_;
  storeData_.network.nextUnicastAddress = static_cast<uint16_t>(node.unicastAddress + node.elementCount);
  const studio::ble::Address target = addressFrom("", 0);
  (void)target;
  upsertNode(storeData_, node);
  if (!store.save(storeData_)) return false;
  session->state.phase = AmaranLightState::Phase::PendingConfig;
  session->pairingDirty = true;
  provisioningLink_ = false;
  closeProvisionKey();
  studio::ble::bleCentral().disconnect(link_, false);
  return true;
}

bool AmaranRuntime::configureNext() {
  MeshNodeRecord* node = findNode(storeData_, linkInstance_);
  if (node == nullptr || dataIn_ == nullptr) return false;
  uint8_t access[24] = {}; size_t length = 0;
  switch (configStep_) {
    case 0: access[0]=0x80; access[1]=0x08; access[2]=0; length=3; break;
    case 1: access[0]=0; access[1]=access[2]=access[3]=0; std::memcpy(access+4, storeData_.network.applicationKey, 16); length=20; break;
    case 2: access[0]=0x80; access[1]=0x3d; access[2]=static_cast<uint8_t>(node->unicastAddress); access[3]=static_cast<uint8_t>(node->unicastAddress>>8); access[4]=access[5]=0; access[6]=0x11; access[7]=0x02; access[8]=access[9]=0; length=10; break;
    case 3: access[0]=0x80; access[1]=0x1b; access[2]=static_cast<uint8_t>(node->unicastAddress); access[3]=static_cast<uint8_t>(node->unicastAddress>>8); access[4]=static_cast<uint8_t>(storeData_.network.groupAddress); access[5]=static_cast<uint8_t>(storeData_.network.groupAddress>>8); access[6]=0x11; access[7]=0x02; access[8]=access[9]=0; length=10; break;
    default: {
      static studio::PreferencesAmaranBackend backend; MeshStore store(backend);
      node->configured = true; if (!store.save(storeData_)) return false;
      studio::ble::bleCentral().markProtocolReady(link_); updateSharedReady(); return true;
    }
  }
  const size_t pduCount = length <= 11 ? 1 : (length + 4 + 11) / 12;
  uint32_t sequences[4] = {};
  for (size_t i = 0; i < pduCount; ++i) if (!sequences_.next(sequences[i])) return false;
  NetworkPduBatch batch;
  if (pduCount == 1) {
    batch.count = 1;
    if (!encodeDeviceMessage(storeData_.network.networkKey, node->deviceKey,
        access, length, sequences[0], storeData_.network.provisionerAddress,
        node->unicastAddress, storeData_.network.ivIndex, batch.pdus[0])) return false;
  } else if (!encodeSegmentedDeviceMessage(storeData_.network.networkKey,
      node->deviceKey, access, length, sequences, pduCount,
      storeData_.network.provisionerAddress, node->unicastAddress,
      storeData_.network.ivIndex, batch)) return false;
  for (uint8_t i = 0; i < batch.count; ++i) {
    uint8_t proxy[70]; size_t proxyLength=0;
    if (!wrapProxyPdu(batch.pdus[i], proxy, sizeof(proxy), proxyLength) ||
        !dataIn_->writeValue(proxy, proxyLength, false)) return false;
  }
  ++configStep_; nextConfigAt_ = millis() + 400; return true;
}

bool AmaranRuntime::sendAccess(studio::InstanceId id, const uint8_t* access, size_t length) {
  const MeshNodeRecord* node = findNode(storeData_, id);
  if (node == nullptr || !node->configured || dataIn_ == nullptr || !connected_) return false;
  uint32_t sequence; if (!sequences_.next(sequence)) return false;
  NetworkPdu network;
  if (!encodeAccessMessage(storeData_.network.networkKey, storeData_.network.applicationKey,
      access, length, sequence, storeData_.network.provisionerAddress,
      node->unicastAddress, storeData_.network.ivIndex, network)) return false;
  uint8_t proxy[70]; size_t proxyLength = 0;
  return wrapProxyPdu(network, proxy, sizeof(proxy), proxyLength) &&
         dataIn_->writeValue(proxy, proxyLength, false);
}

studio::CommandStatus AmaranRuntime::dispatch(const studio::DeviceCommand& command) {
  Session* session = sessionFor(command.instanceId);
  if (session == nullptr) return studio::CommandStatus::Unavailable;
  AccessPayload payload;
  bool valid = false;
  if (command.type == studio::CommandType::TurnOn || command.type == studio::CommandType::TurnOff) valid = buildPowerAccess(command.type == studio::CommandType::TurnOn, payload);
  else if (command.type == studio::CommandType::SetLightCct && validCctCommand(command.value0, command.value1, command.value2)) valid = buildCctAccess(command.value0, command.value2, command.value1, payload);
  else if (command.type == studio::CommandType::SetLightRgb && validRgbCommand(command.value0, command.value1)) valid = buildRgbAccess(command.value0, command.value1, payload);
  else if (command.type == studio::CommandType::Connect) { session->state.phase=AmaranLightState::Phase::Scanning;session->state.error[0]='\0';return beginLink(command.instanceId, findNode(storeData_, command.instanceId) == nullptr) ? studio::CommandStatus::Succeeded : studio::CommandStatus::Unavailable; }
  else return studio::CommandStatus::Unsupported;
  if (!valid) return studio::CommandStatus::InvalidArgument;
  session->state.commandPending = true;
  const bool sent = sendAccess(command.instanceId, payload.bytes, payload.length);
  session->state.commandPending = false; session->state.lastCommandFailed = !sent;
  if (!sent) return studio::CommandStatus::Unavailable;
  session->state.optimistic = true;
  if (command.type == studio::CommandType::TurnOn || command.type == studio::CommandType::TurnOff) session->state.on = command.type == studio::CommandType::TurnOn;
  else if (command.type == studio::CommandType::SetLightCct) { session->state.mode=AmaranLightState::Mode::Cct; session->state.kelvin=command.value0; session->state.cctBrightness=command.value1; session->state.tintPermille=command.value2; }
  else { session->state.mode=AmaranLightState::Mode::Rgb; session->state.rgb=command.value0; session->state.rgbBrightness=command.value1; }
  return studio::CommandStatus::Succeeded;
}

studio::DeviceRuntimeState AmaranRuntime::runtimeState(studio::InstanceId id) const {
  studio::DeviceRuntimeState out; const Session* session = sessionFor(id); if (!session) return out;
  if (session->state.phase == AmaranLightState::Phase::Scanning) out.link = studio::LinkState::Scanning;
  else if (session->state.phase == AmaranLightState::Phase::Provisioning || session->state.phase == AmaranLightState::Phase::ConnectingProxy || session->state.phase == AmaranLightState::Phase::PendingConfig) out.link = studio::LinkState::Connecting;
  else if (session->state.phase == AmaranLightState::Phase::Ready) out.link = studio::LinkState::Connected;
  out.protocolReady = session->state.phase == AmaranLightState::Phase::Ready;
  out.quality = session->state.optimistic ? studio::StateQuality::Optimistic : studio::StateQuality::Unknown;
  out.commandPending = session->state.commandPending; out.commandFailed = session->state.lastCommandFailed;
  return out;
}
const AmaranLightState* AmaranRuntime::state(studio::InstanceId id) const { const Session* s=sessionFor(id); return s ? &s->state : nullptr; }
bool AmaranRuntime::consumePairingUpdate(studio::InstanceId id, studio::DeviceRecord& record) {
  Session* session=sessionFor(id); const MeshNodeRecord* node=findNode(storeData_,id);
  if (!session || !node || !session->pairingDirty) return false;
  session->pairingDirty=false; record.paired=node->configured; return true;
}
void AmaranRuntime::forgetLocal(studio::InstanceId id) {
  if (!loaded_) return; static studio::PreferencesAmaranBackend backend; MeshStore store(backend);
  if (removeNode(storeData_, id)) store.save(storeData_);
}
void AmaranRuntime::fail(Session& session, const char* error) { session.state.phase=AmaranLightState::Phase::Failed; session.state.lastCommandFailed=true; std::strncpy(session.state.error,error,sizeof(session.state.error)-1); }
void AmaranRuntime::updateSharedReady() {
  for (auto& session : sessions_) {
    if (session.instanceId == studio::kInvalidInstanceId) continue;
    const MeshNodeRecord* node=findNode(storeData_,session.instanceId);
    if (node && node->configured) { session.state.phase=AmaranLightState::Phase::Ready; session.state.proxyConnected=true; session.pairingDirty=true; }
  }
}

}  // namespace amaran_light

#endif
