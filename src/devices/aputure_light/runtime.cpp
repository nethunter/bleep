#include "devices/aputure_light/runtime.h"

#ifdef UI_SIMULATOR

#include <cstring>
#include <new>

namespace aputure_light {
namespace {
AputureLightRuntime* instance = nullptr;
}
AputureLightRuntime* runtime() {
  if (instance == nullptr) {
    instance = new (std::nothrow) AputureLightRuntime;
  }
  return instance;
}
AputureLightRuntime* runtimeIfActive() { return instance; }
void releaseRuntimeIfIdle() {
  if (instance != nullptr && instance->idle()) {
    delete instance;
    instance = nullptr;
  }
}
AputureLightRuntime::Session* AputureLightRuntime::sessionFor(studio::InstanceId id) {
  for (auto& session : sessions_) if (session.instanceId == id) return &session;
  return nullptr;
}
const AputureLightRuntime::Session* AputureLightRuntime::sessionFor(studio::InstanceId id) const {
  for (const auto& session : sessions_) if (session.instanceId == id) return &session;
  return nullptr;
}
bool AputureLightRuntime::activate(const studio::DeviceRecord& record) {
  Session* session = sessionFor(record.instanceId);
  if (session == nullptr) for (auto& candidate : sessions_) if (candidate.instanceId == studio::kInvalidInstanceId) { session = &candidate; break; }
  if (session == nullptr) return false;
  session->instanceId = record.instanceId;
  session->model = record.driverId;
  session->state.phase = record.paired ? AputureLightState::Phase::Ready
                                       : AputureLightState::Phase::Scanning;
  session->state.proxyConnected = record.paired;
  return true;
}
void AputureLightRuntime::deactivate(studio::InstanceId id) { if (auto* s = sessionFor(id)) *s = Session{}; }
void AputureLightRuntime::loop() {}
studio::CommandStatus AputureLightRuntime::dispatch(const studio::DeviceCommand& command) {
  Session* s = sessionFor(command.instanceId);
  if (s == nullptr) return studio::CommandStatus::Unavailable;
  if (command.type == studio::CommandType::Refresh) {
    return studio::CommandStatus::Succeeded;
  }
  if (command.type == studio::CommandType::TurnOn || command.type == studio::CommandType::TurnOff) {
    for (auto& member : sessions_) {
      if (member.instanceId == studio::kInvalidInstanceId) continue;
      member.state.on = command.type == studio::CommandType::TurnOn;
      member.state.optimistic = true;
      member.state.powerOptimistic = true;
    }
    return studio::CommandStatus::Succeeded;
  }
  if (command.type == studio::CommandType::SetLightCct && validCctCommand(command.value0, command.value1, command.value2)) {
    s->state.mode = AputureLightState::Mode::Cct; s->state.kelvin = command.value0; s->state.cctBrightness = command.value1; s->state.tintPermille = command.value2; s->state.optimistic = true; s->state.powerOptimistic = false; return studio::CommandStatus::Succeeded;
  }
  if (command.type == studio::CommandType::SetLightRgb && validRgbCommand(command.value0, command.value1)) {
    s->state.mode = AputureLightState::Mode::Rgb; s->state.rgb = command.value0; s->state.rgbBrightness = command.value1; s->state.optimistic = true; s->state.powerOptimistic = false; return studio::CommandStatus::Succeeded;
  }
  return studio::CommandStatus::Unsupported;
}
studio::DeviceRuntimeState AputureLightRuntime::runtimeState(studio::InstanceId id) const {
  studio::DeviceRuntimeState out; const Session* s = sessionFor(id); if (!s) return out;
  if (s->state.phase == AputureLightState::Phase::Scanning) {
    out.link = studio::LinkState::Scanning;
  } else if (s->state.phase == AputureLightState::Phase::Ready) {
    out.link = studio::LinkState::Connected;
    out.protocolReady = true;
  } else {
    out.link = studio::LinkState::Connecting;
  }
  out.quality = s->state.optimistic ? studio::StateQuality::Optimistic
                                    : studio::StateQuality::Unknown;
  return out;
}
const AputureLightState* AputureLightRuntime::state(studio::InstanceId id) const { const Session* s = sessionFor(id); return s ? &s->state : nullptr; }
bool AputureLightRuntime::consumePairingUpdate(studio::InstanceId, studio::DeviceRecord&) { return false; }
void AputureLightRuntime::forgetLocal(studio::InstanceId id) { if (auto* s = sessionFor(id)) s->state = AputureLightState{}; }
bool AputureLightRuntime::acquireGateway(const studio::DeviceRecord&) { return true; }
void AputureLightRuntime::releaseGateway(studio::InstanceId) {}
void* AputureLightRuntime::gatewayClient() const { return nullptr; }
void AputureLightRuntime::onBleAdvertisement(studio::ble::LinkHandle, const studio::ble::Advertisement&) {}
void AputureLightRuntime::onBleEvent(studio::ble::LinkHandle, const studio::ble::Event&) {}
void AputureLightRuntime::enqueueNotification(const uint8_t*, size_t) {}
void AputureLightRuntime::simSetPhase(studio::InstanceId id,
                                AputureLightState::Phase phase) {
  if (Session* session = sessionFor(id)) {
    session->state.phase = phase;
    session->state.proxyConnected = phase == AputureLightState::Phase::Ready;
  }
}
bool AputureLightRuntime::ensureLoaded() { return true; }
bool AputureLightRuntime::beginLink(studio::InstanceId, bool) { return true; }
bool AputureLightRuntime::setupProvisioning() { return false; }
bool AputureLightRuntime::setupProxy() { return false; }
void AputureLightRuntime::processNotification(const Notification&) {}
bool AputureLightRuntime::sendProvisioning(const uint8_t*, size_t) { return false; }
bool AputureLightRuntime::sendProvisioningPdu(const uint8_t*, size_t) { return false; }
bool AputureLightRuntime::completeProvisioning() { return false; }
bool AputureLightRuntime::configureNext() { return false; }
bool AputureLightRuntime::sendAccess(studio::InstanceId, const uint8_t*, size_t) { return false; }
bool AputureLightRuntime::sendAccessTo(uint16_t, const uint8_t*, size_t) { return false; }
uint16_t AputureLightRuntime::controlGroupFor(studio::InstanceId) const { return 0; }
bool AputureLightRuntime::refreshGroupPower() { return false; }
void AputureLightRuntime::fail(Session& s, const char* error) { s.state.phase = AputureLightState::Phase::Failed; std::strncpy(s.state.error, error, sizeof(s.state.error)-1); }
void AputureLightRuntime::updateSharedReady() {}
studio::InstanceId AputureLightRuntime::preferredGatewayInstance() const { return studio::kInvalidInstanceId; }
bool AputureLightRuntime::hasActiveUsers() const { return false; }
bool AputureLightRuntime::isPreferredGatewayAddress(const char*) const { return true; }
}  // namespace aputure_light

#else

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <new>

#include <cstring>

#include "core/ble/ble_runtime.h"
#include "core/mesh/mesh_repository.h"
#include "core/preferences_store.h"
#include "devices/aputure_light/protocol.h"

#if ARDUINO_USB_CDC_ON_BOOT
#define APUTURE_LIGHT_LOG Serial0
#else
#define APUTURE_LIGHT_LOG Serial
#endif

namespace aputure_light {
namespace {

constexpr const char* kProvisionService = "00001827-0000-1000-8000-00805f9b34fb";
constexpr const char* kProvisionAdvertisedService = "1827";
constexpr const char* kProvisionIn = "00002adb-0000-1000-8000-00805f9b34fb";
constexpr const char* kProvisionOut = "00002adc-0000-1000-8000-00805f9b34fb";
constexpr const char* kProxyService = "00001828-0000-1000-8000-00805f9b34fb";
constexpr const char* kProxyAdvertisedService = "1828";
constexpr const char* kProxyIn = "00002add-0000-1000-8000-00805f9b34fb";
constexpr const char* kProxyOut = "00002ade-0000-1000-8000-00805f9b34fb";
constexpr uint32_t kPowerPollIntervalMs = 5000;
constexpr uint32_t kNodeFreshnessMs = 15000;

AputureLightRuntime* runtimeInstance = nullptr;
AputureLightRuntime* activeRuntime = nullptr;

void notificationCallback(NimBLERemoteCharacteristic*, uint8_t* data,
                          size_t length, bool) {
  if (activeRuntime != nullptr) activeRuntime->enqueueNotification(data, length);
}

studio::ble::Address addressFrom(const char* value, uint8_t type) {
  studio::ble::Address address;
  std::strncpy(address.value, value != nullptr ? value : "", sizeof(address.value)-1);
  address.type = type;
  return address;
}

}  // namespace

AputureLightRuntime* runtime() {
  if (runtimeInstance == nullptr) {
    if (!studio::mesh::retainRepository()) return nullptr;
    runtimeInstance = new (std::nothrow) AputureLightRuntime;
    if (runtimeInstance == nullptr) studio::mesh::releaseRepository();
  }
  return runtimeInstance;
}
AputureLightRuntime* runtimeIfActive() { return runtimeInstance; }
void releaseRuntimeIfIdle() {
  if (runtimeInstance != nullptr && runtimeInstance->idle()) {
    if (activeRuntime == runtimeInstance) activeRuntime = nullptr;
    delete runtimeInstance;
    runtimeInstance = nullptr;
    studio::mesh::releaseRepository();
  }
}

AputureLightRuntime::Session* AputureLightRuntime::sessionFor(studio::InstanceId id) {
  for (auto& session : sessions_) if (session.instanceId == id) return &session;
  return nullptr;
}
const AputureLightRuntime::Session* AputureLightRuntime::sessionFor(studio::InstanceId id) const {
  for (const auto& session : sessions_) if (session.instanceId == id) return &session;
  return nullptr;
}

bool AputureLightRuntime::ensureLoaded() {
  return studio::mesh::repository().begin();
}

bool AputureLightRuntime::activate(const studio::DeviceRecord& record) {
  if (!ensureLoaded()) return false;
  Session* session = sessionFor(record.instanceId);
  if (session == nullptr) {
    for (auto& candidate : sessions_) if (candidate.instanceId == studio::kInvalidInstanceId) { session = &candidate; break; }
  }
  if (session == nullptr) return false;
  session->instanceId = record.instanceId;
  session->model = record.driverId;
  MeshStoreData& meshData = studio::mesh::repository().data();
  MeshNodeRecord* node = findNode(meshData, record.instanceId);
  bool nodeChanged = false;
  MeshNodeRecord previousNode;
  if (node != nullptr) previousNode = *node;
  if (node != nullptr && node->vendorCompanyId == 0) {
    uint16_t companyId = 0;
    uint16_t modelId = 0;
    if (inferKnownVendorModel(record.displayName, record.bleName, companyId,
                              modelId)) {
      node->vendorCompanyId = companyId;
      node->vendorModelId = modelId;
      node->controlGroupAddress = defaultControlGroupAddress(meshData, *node);
      nodeChanged = true;
      APUTURE_LIGHT_LOG.printf(
          "aputure_light event=legacy_vendor_repaired company=0x%04x model=0x%04x\n",
          node->vendorCompanyId, node->vendorModelId);
    }
  }
  if (node != nullptr && node->vendorCompanyId != 0 &&
      node->configurationVersion < kCurrentConfigurationVersion) {
    node->configured = false;
    nodeChanged = true;
  }
  if (nodeChanged && !studio::mesh::repository().save()) {
    *node = previousNode;
    deactivate(record.instanceId);
    return false;
  }
  session->state.phase = node == nullptr ? AputureLightState::Phase::Scanning
                                         : (node->configured ? AputureLightState::Phase::ConnectingProxy
                                                             : AputureLightState::Phase::PendingConfig);
  if (link_ == studio::ble::kInvalidLinkHandle) {
    if (!beginLink(record.instanceId, node == nullptr)) {
      // beginLink may have acquired a logical BLE slot before its scan/connect
      // request failed. Roll the session and shared transport back together so
      // the driver's failed activation does not keep this runtime alive.
      deactivate(record.instanceId);
      return false;
    }
    return true;
  }
  if (node == nullptr) {
    linkInstance_ = record.instanceId;
    provisioningLink_ = true;
    if (connected_) studio::ble::bleCentral().disconnect(link_, false);
    else studio::ble::bleCentral().requestScan(link_, true);
  } else if (!provisioningLink_) {
    if (connected_ && !node->configured) {
      linkInstance_ = record.instanceId;
      configStep_ = 1;
      configRetryCount_ = 0;
      configAwaitingStatus_ = false;
      configBatch_ = NetworkPduBatch{};
      configBatchIndex_ = 0;
      nextConfigAt_ = millis();
    } else if (connected_) {
      updateSharedReady();
    } else {
      beginLink(preferredGatewayInstance(), false);
    }
  }
  return true;
}

studio::InstanceId AputureLightRuntime::preferredGatewayInstance() const {
  for (studio::InstanceId id : gatewayUsers_) {
    if (id != studio::kInvalidInstanceId) return id;
  }
  for (const Session& session : sessions_) {
    if (session.instanceId != studio::kInvalidInstanceId) {
      return session.instanceId;
    }
  }
  return studio::kInvalidInstanceId;
}

bool AputureLightRuntime::hasActiveUsers() const {
  return preferredGatewayInstance() != studio::kInvalidInstanceId;
}

bool AputureLightRuntime::isPreferredGatewayAddress(const char* address) const {
  bool hasGatewayUsers = false;
  const MeshStoreData& meshData = studio::mesh::repository().data();
  for (studio::InstanceId id : gatewayUsers_) {
    if (id == studio::kInvalidInstanceId) continue;
    hasGatewayUsers = true;
    const MeshNodeRecord* node = findNode(meshData, id);
    if (node != nullptr && node->bleAddress[0] != '\0' &&
        address != nullptr && std::strcmp(node->bleAddress, address) == 0) {
      return true;
    }
  }
  return !hasGatewayUsers;
}

bool AputureLightRuntime::acquireGateway(const studio::DeviceRecord& record) {
  if (!ensureLoaded() ||
      findNode(studio::mesh::repository().data(), record.instanceId) == nullptr) {
    return false;
  }
  bool registered = false;
  bool added = false;
  for (studio::InstanceId id : gatewayUsers_) {
    registered = registered || id == record.instanceId;
  }
  if (!registered) {
    for (studio::InstanceId& id : gatewayUsers_) {
      if (id != studio::kInvalidInstanceId) continue;
      id = record.instanceId;
      registered = true;
      added = true;
      break;
    }
  }
  if (!registered) return false;
  const studio::InstanceId preferred = preferredGatewayInstance();
  if (link_ == studio::ble::kInvalidLinkHandle) {
    if (beginLink(preferred, false)) return true;
    if (added) {
      for (studio::InstanceId& id : gatewayUsers_) {
        if (id == record.instanceId) id = studio::kInvalidInstanceId;
      }
    }
    return false;
  }
  if (linkInstance_ != preferred) {
    linkInstance_ = preferred;
    provisioningLink_ = false;
    connected_ = false;
    ++gatewayGeneration_;
    studio::ble::bleCentral().disconnect(link_, false);
  } else if (!connected_) {
    beginLink(preferred, false);
  }
  return true;
}

void AputureLightRuntime::releaseGateway(studio::InstanceId instanceId) {
  for (studio::InstanceId& id : gatewayUsers_) {
    if (id == instanceId) id = studio::kInvalidInstanceId;
  }
  if (!hasActiveUsers() && link_ != studio::ble::kInvalidLinkHandle) {
    studio::ble::bleCentral().release(link_);
    link_ = studio::ble::kInvalidLinkHandle;
    connected_ = false;
    dataIn_ = nullptr;
    configAwaitingStatus_ = false;
    configBatch_ = NetworkPduBatch{};
    configBatchIndex_ = 0;
    activeRuntime = nullptr;
    ++gatewayGeneration_;
    return;
  }
  const studio::InstanceId preferred = preferredGatewayInstance();
  if (preferred != studio::kInvalidInstanceId && preferred != linkInstance_) {
    linkInstance_ = preferred;
    connected_ = false;
    ++gatewayGeneration_;
    studio::ble::bleCentral().disconnect(link_, false);
  }
}

void* AputureLightRuntime::gatewayClient() const {
  return gatewayConnected()
             ? studio::ble::bleCentral().nativeClient(link_)
             : nullptr;
}

void AputureLightRuntime::deactivate(studio::InstanceId id) {
  if (Session* session = sessionFor(id)) *session = Session{};
  if (!hasActiveUsers() && link_ != studio::ble::kInvalidLinkHandle) {
    provisioner_.cancel();
    studio::ble::bleCentral().release(link_);
    link_ = studio::ble::kInvalidLinkHandle;
    connected_ = false;
    dataIn_ = nullptr;
    activeRuntime = nullptr;
    ++gatewayGeneration_;
  } else if (id == linkInstance_) {
    const studio::InstanceId preferred = preferredGatewayInstance();
    if (preferred != studio::kInvalidInstanceId) {
      linkInstance_ = preferred;
      connected_ = false;
      ++gatewayGeneration_;
      studio::ble::bleCentral().disconnect(link_, false);
    }
  }
}

bool AputureLightRuntime::beginLink(studio::InstanceId id, bool provisioning) {
  studio::ble::ConnectPolicy policy;
  policy.security = studio::ble::SecurityPolicy::None;
  policy.diagnosticTag = "aputure_mesh";
  if (link_ == studio::ble::kInvalidLinkHandle) {
    link_ = studio::ble::bleCentral().acquire(*this, policy);
    if (link_ == studio::ble::kInvalidLinkHandle) return false;
  }
  linkInstance_ = id;
  provisioningLink_ = provisioning;
  activeRuntime = this;
  const MeshNodeRecord* node = findNode(studio::mesh::repository().data(), id);
  if (!provisioning && node != nullptr && node->bleAddress[0] != '\0') {
    return studio::ble::bleCentral().requestConnect(
        link_, addressFrom(node->bleAddress, node->bleAddressType));
  }
  return studio::ble::bleCentral().requestScan(link_, true);
}

void AputureLightRuntime::onBleAdvertisement(
    studio::ble::LinkHandle link,
    const studio::ble::Advertisement& advertisement) {
  if (link != link_) return;
  const char* wanted = provisioningLink_ ? kProvisionAdvertisedService
                                         : kProxyAdvertisedService;
  if (studio::ble::advertisesService(advertisement, wanted) &&
      (provisioningLink_ ||
       isPreferredGatewayAddress(advertisement.address.value))) {
    if (provisioningLink_) {
      std::strncpy(provisioningAddress_, advertisement.address.value,
                   sizeof(provisioningAddress_) - 1);
      provisioningAddressType_ = advertisement.address.type;
      studio::ble::advertisementName(advertisement, provisioningName_,
                                     sizeof(provisioningName_));
    }
    if (Session* session = sessionFor(linkInstance_)) {
      session->state.phase = provisioningLink_
          ? AputureLightState::Phase::Provisioning
          : AputureLightState::Phase::ConnectingProxy;
    }
    studio::ble::bleCentral().selectAdvertisement(link_, advertisement);
  }
}

void AputureLightRuntime::onBleEvent(studio::ble::LinkHandle link,
                               const studio::ble::Event& event) {
  if (link != link_) {
    APUTURE_LIGHT_LOG.printf(
        "aputure_light event=ble_ignored type=%u event_link=%u runtime_link=%u\n",
        static_cast<unsigned>(event.type), static_cast<unsigned>(link),
        static_cast<unsigned>(link_));
    return;
  }
  APUTURE_LIGHT_LOG.printf("aputure_light event=ble type=%u link=%u instance=%lu\n",
                    static_cast<unsigned>(event.type),
                    static_cast<unsigned>(link),
                    static_cast<unsigned long>(linkInstance_));
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
    configAwaitingStatus_ = false;
    configBatch_ = NetworkPduBatch{};
    configBatchIndex_ = 0;
    ++gatewayGeneration_;
    for (auto& session : sessions_) {
      if (session.instanceId == studio::kInvalidInstanceId) continue;
      session.state.proxyConnected = false;
      session.state.nodeReachable = false;
      const MeshNodeRecord* node =
          findNode(studio::mesh::repository().data(), session.instanceId);
      if (node != nullptr && node->configured) {
        session.state.phase = AputureLightState::Phase::ConnectingProxy;
      }
    }
    const studio::InstanceId preferred = preferredGatewayInstance();
    if (link_ != studio::ble::kInvalidLinkHandle &&
        preferred != studio::kInvalidInstanceId) {
      linkInstance_ = preferred;
      const MeshNodeRecord* node =
          findNode(studio::mesh::repository().data(), preferred);
      if (node != nullptr && node->bleAddress[0] != '\0') {
        studio::ble::bleCentral().requestConnect(
            link_, addressFrom(node->bleAddress, node->bleAddressType));
      } else {
        studio::ble::bleCentral().requestScan(link_, true);
      }
    }
  } else if (event.type == studio::ble::EventType::ConnectFailed) {
    if (Session* session = sessionFor(linkInstance_)) {
      session->state.phase = AputureLightState::Phase::Scanning;
    }
  }
}

bool AputureLightRuntime::setupProvisioning() {
  NimBLEClient* client = static_cast<NimBLEClient*>(studio::ble::bleCentral().nativeClient(link_));
  if (client == nullptr) return false;
  NimBLERemoteService* service = client->getService(NimBLEUUID(kProvisionService));
  if (service == nullptr) return false;
  dataIn_ = service->getCharacteristic(NimBLEUUID(kProvisionIn));
  NimBLERemoteCharacteristic* out = service->getCharacteristic(NimBLEUUID(kProvisionOut));
  if (dataIn_ == nullptr || out == nullptr || !out->subscribe(true, notificationCallback, true)) return false;
  if (Session* session = sessionFor(linkInstance_)) session->state.phase = AputureLightState::Phase::Provisioning;
  return provisioner_.begin(studio::mesh::repository().data().network.networkKey,
                            studio::mesh::repository().data().network.ivIndex,
                            studio::mesh::repository().data().network.nextUnicastAddress, *this);
}

bool AputureLightRuntime::setupProxy() {
  NimBLEClient* client = static_cast<NimBLEClient*>(studio::ble::bleCentral().nativeClient(link_));
  if (client == nullptr) return false;
  const uint32_t startedAt = millis();
  APUTURE_LIGHT_LOG.printf(
      "aputure_light event=proxy_service_begin free_heap=%lu max_alloc=%lu\n",
      static_cast<unsigned long>(ESP.getFreeHeap()),
      static_cast<unsigned long>(ESP.getMaxAllocHeap()));
  NimBLERemoteService* service = client->getService(NimBLEUUID(kProxyService));
  APUTURE_LIGHT_LOG.printf(
      "aputure_light event=proxy_service_end elapsed_ms=%lu result=%s\n",
      static_cast<unsigned long>(millis() - startedAt),
      service != nullptr ? "ok" : "missing");
  if (service == nullptr) return false;
  dataIn_ = service->getCharacteristic(NimBLEUUID(kProxyIn));
  NimBLERemoteCharacteristic* out = service->getCharacteristic(NimBLEUUID(kProxyOut));
  if (dataIn_ == nullptr || out == nullptr) {
    APUTURE_LIGHT_LOG.printf(
        "aputure_light event=proxy_characteristics result=missing in=%u out=%u\n",
        dataIn_ != nullptr ? 1u : 0u, out != nullptr ? 1u : 0u);
    return false;
  }
  const uint32_t subscribeStartedAt = millis();
  const bool subscribed = out->subscribe(true, notificationCallback, true);
  APUTURE_LIGHT_LOG.printf(
      "aputure_light event=proxy_subscribe elapsed_ms=%lu result=%s\n",
      static_cast<unsigned long>(millis() - subscribeStartedAt),
      subscribed ? "ok" : "failed");
  if (!subscribed) return false;
  ++gatewayGeneration_;
  const MeshNodeRecord* node = findNode(studio::mesh::repository().data(), linkInstance_);
  if (node != nullptr && !node->configured) {
    configStep_ = 1;
    configRetryCount_ = 0;
    configAwaitingStatus_ = false;
    configBatch_ = NetworkPduBatch{};
    configBatchIndex_ = 0;
    nextConfigAt_ = millis();
  } else {
    studio::ble::bleCentral().markProtocolReady(link_);
    updateSharedReady();
    refreshGroupPower();
  }
  return true;
}

void AputureLightRuntime::enqueueNotification(const uint8_t* data, size_t length) {
  if (data == nullptr || length == 0 || length > sizeof(notifications_[0].bytes)) return;
  const uint8_t next = static_cast<uint8_t>((notifyHead_ + 1) % 8);
  if (next == notifyTail_) return;
  notifications_[notifyHead_].length = static_cast<uint8_t>(length);
  std::memcpy(notifications_[notifyHead_].bytes, data, length);
  notifyHead_ = next;
}

void AputureLightRuntime::loop() {
  const uint32_t now = millis();
  if (lastLoopMs_ == now) return;
  lastLoopMs_ = now;
  while (notifyTail_ != notifyHead_) {
    const Notification notification = notifications_[notifyTail_];
    notifyTail_ = static_cast<uint8_t>((notifyTail_ + 1) % 8);
    processNotification(notification);
  }
  Session* configuringSession = sessionFor(linkInstance_);
  if (connected_ && !provisioningLink_ && configuringSession != nullptr &&
      configuringSession->state.phase != AputureLightState::Phase::Failed &&
      findNode(studio::mesh::repository().data(), linkInstance_) != nullptr &&
      !findNode(studio::mesh::repository().data(), linkInstance_)->configured &&
      static_cast<int32_t>(now - nextConfigAt_) >= 0) {
    configureNext();
  }
  if (connected_ && !provisioningLink_ && dataIn_ != nullptr &&
      static_cast<uint32_t>(now - lastPowerPollMs_) >= kPowerPollIntervalMs) {
    lastPowerPollMs_ = now;
    refreshGroupPower();
  }
  for (auto& session : sessions_) {
    if (session.state.nodeReachable &&
        static_cast<uint32_t>(now - session.state.lastSeenMs) >
            kNodeFreshnessMs) {
      session.state.nodeReachable = false;
    }
  }
}

bool AputureLightRuntime::sendProvisioning(const uint8_t* pdu, size_t length) {
  if (dataIn_ == nullptr || length + 1 > 80) return false;
  uint8_t wrapped[80] = {0x03};
  std::memcpy(wrapped + 1, pdu, length);
  return dataIn_->writeValue(wrapped, length + 1, false);
}

bool AputureLightRuntime::sendProvisioningPdu(const uint8_t* pdu, size_t length) {
  return sendProvisioning(pdu, length);
}

void AputureLightRuntime::processNotification(const Notification& notification) {
  if (provisioningLink_) {
    if (notification.length < 2 || notification.bytes[0] != 0x03) return;
    const uint8_t* pdu = notification.bytes + 1;
    const size_t length = notification.length - 1;
    bool ok = provisioner_.handle(pdu, length);
    if (ok && provisioner_.complete()) ok = completeProvisioning();
    if (!ok) {
      if (Session* session = sessionFor(linkInstance_)) {
        fail(*session, "Provisioning failed");
      }
    }
    return;
  }

  const MeshStoreData& meshData = studio::mesh::repository().data();
  const MeshNodeRecord* configuringNode = findNode(meshData, linkInstance_);
  DecodedAccessMessage configuration;
  if (configuringNode != nullptr && configAwaitingStatus_ &&
      decodeProxyDeviceMessage(meshData.network.networkKey,
                               configuringNode->deviceKey,
                               notification.bytes, notification.length,
                               meshData.network.ivIndex, configuration) &&
      handleConfigurationStatus(configuration)) {
    return;
  }
  DecodedAccessMessage decoded;
  if (!decodeProxyAccessMessage(meshData.network.networkKey,
                                meshData.network.applicationKey,
                                notification.bytes, notification.length,
                                meshData.network.ivIndex, decoded)) {
    return;
  }
  VendorPowerStatus status;
  if (!parseVendorPowerStatus(decoded.access, decoded.accessLength, status)) {
    return;
  }
  for (uint8_t i = 0; i < meshData.nodeCount; ++i) {
    const MeshNodeRecord& node = meshData.nodes[i];
    if (node.unicastAddress != decoded.source) continue;
    Session* session = sessionFor(node.instanceId);
    if (session == nullptr) return;
    if (session->receiveSequenceKnown &&
        decoded.sequence <= session->receiveSequence) {
      return;
    }
    session->receiveSequenceKnown = true;
    session->receiveSequence = decoded.sequence;
    session->state.on = status.on;
    if (session->state.powerOptimistic) {
      session->state.optimistic = false;
      session->state.powerOptimistic = false;
    }
    session->state.nodeReachable = true;
    session->state.powerConfirmed = true;
    session->state.lastSeenMs = millis();
    session->state.lastCommandFailed = false;
    return;
  }
}

bool AputureLightRuntime::handleConfigurationStatus(
    const DecodedAccessMessage& decoded) {
  const MeshNodeRecord* node =
      findNode(studio::mesh::repository().data(), linkInstance_);
  if (node == nullptr || decoded.source != node->unicastAddress ||
      decoded.destination !=
          studio::mesh::repository().data().network.provisionerAddress ||
      decoded.accessLength < 3) {
    return false;
  }
  const uint16_t opcode = static_cast<uint16_t>(decoded.access[0]) << 8 |
                          decoded.access[1];
  const uint16_t expected = configStep_ == 1 ? 0x8003
                            : (configStep_ == 2 || configStep_ == 5)
                                ? 0x803e
                                : (configStep_ >= 3 && configStep_ <= 6)
                                    ? 0x801f
                                    : 0;
  if (opcode != expected) return false;
  const uint8_t status = decoded.access[2];
  configAwaitingStatus_ = false;
  if (status != 0) {
    if (Session* session = sessionFor(linkInstance_)) {
      fail(*session, "Mesh config rejected");
    }
    APUTURE_LIGHT_LOG.printf(
        "aputure_light event=config_status step=%u opcode=0x%04x status=%u result=failed\n",
        configStep_, opcode, status);
    return true;
  }
  APUTURE_LIGHT_LOG.printf(
      "aputure_light event=config_status step=%u opcode=0x%04x status=0 result=ok\n",
      configStep_, opcode);
  ++configStep_;
  configRetryCount_ = 0;
  nextConfigAt_ = millis() + 100;
  return true;
}

bool AputureLightRuntime::completeProvisioning() {
  Session* session = sessionFor(linkInstance_);
  if (session == nullptr || !provisioner_.complete()) return false;
  MeshStoreData& meshData = studio::mesh::repository().data();
  const MeshStoreData previous = meshData;
  MeshNodeRecord node; node.instanceId = session->instanceId; node.model = session->model;
  node.unicastAddress = meshData.network.nextUnicastAddress;
  node.elementCount = provisioner_.elementCount();
  std::memcpy(node.deviceKey, provisioner_.deviceKey(), 16);
  std::strncpy(node.bleAddress, provisioningAddress_, sizeof(node.bleAddress)-1);
  node.bleAddressType = provisioningAddressType_;
  if (std::strstr(provisioningName_, "SLCK") != nullptr) {
    node.vendorCompanyId = 0x0211;
    node.vendorModelId = 0x0000;
  } else if (std::strstr(provisioningName_, "Mesh Device") != nullptr) {
    node.vendorCompanyId = 0x03f6;
    node.vendorModelId = 0x1000;
  }
  node.controlGroupAddress = defaultControlGroupAddress(meshData, node);
  if (node.unicastAddress > 0x7fff - node.elementCount ||
      !upsertNode(meshData, node))
    return false;
  meshData.network.nextUnicastAddress =
      static_cast<uint16_t>(node.unicastAddress + node.elementCount);
  if (!studio::mesh::repository().save()) {
    meshData = previous;
    return false;
  }
  session->state.phase = AputureLightState::Phase::PendingConfig;
  session->pairingDirty = true;
  provisioningLink_ = false;
  connected_ = false;
  dataIn_ = nullptr;
  configBatch_ = NetworkPduBatch{};
  configBatchIndex_ = 0;
  provisioner_.cancel();
  studio::ble::bleCentral().disconnect(link_, false);
  return true;
}

bool AputureLightRuntime::configureNext() {
  MeshNodeRecord* node = findNode(studio::mesh::repository().data(), linkInstance_);
  if (node == nullptr || dataIn_ == nullptr) {
    APUTURE_LIGHT_LOG.printf(
        "aputure_light event=config_failed step=%u reason=missing_state node=%u in=%u\n",
        configStep_, node != nullptr ? 1u : 0u, dataIn_ != nullptr ? 1u : 0u);
    return false;
  }
  if (configBatch_.count != 0) {
    const uint8_t index = configBatchIndex_;
    uint8_t proxy[70];
    size_t proxyLength = 0;
    if (index >= configBatch_.count ||
        !wrapProxyPdu(configBatch_.pdus[index], proxy, sizeof(proxy),
                      proxyLength) ||
        !dataIn_->writeValue(proxy, proxyLength, false)) {
      APUTURE_LIGHT_LOG.printf(
          "aputure_light event=config_failed step=%u reason=segment_write index=%u\n",
          configStep_, index);
      configBatch_ = NetworkPduBatch{};
      configBatchIndex_ = 0;
      return false;
    }
    ++configBatchIndex_;
    APUTURE_LIGHT_LOG.printf(
        "aputure_light event=config_segment_sent step=%u index=%u count=%u\n",
        configStep_, index, configBatch_.count);
    if (configBatchIndex_ < configBatch_.count) {
      nextConfigAt_ = millis() + 350;
    } else {
      const uint8_t sentCount = configBatch_.count;
      configBatch_ = NetworkPduBatch{};
      configBatchIndex_ = 0;
      configAwaitingStatus_ = true;
      configStatusDeadlineMs_ = millis() + 2500;
      nextConfigAt_ = configStatusDeadlineMs_;
      APUTURE_LIGHT_LOG.printf("aputure_light event=config_sent step=%u pdus=%u\n",
                        configStep_, sentCount);
    }
    return true;
  }
  APUTURE_LIGHT_LOG.printf("aputure_light event=config_begin step=%u\n", configStep_);
  if (configAwaitingStatus_) {
    if (static_cast<int32_t>(millis() - configStatusDeadlineMs_) < 0) {
      return true;
    }
    configAwaitingStatus_ = false;
    if (configRetryCount_ >= 2) {
      if (Session* session = sessionFor(linkInstance_)) {
        fail(*session, "Mesh config timeout");
      }
      APUTURE_LIGHT_LOG.printf(
          "aputure_light event=config_failed step=%u reason=status_timeout\n",
          configStep_);
      return false;
    }
    ++configRetryCount_;
    APUTURE_LIGHT_LOG.printf("aputure_light event=config_retry step=%u attempt=%u\n",
                      configStep_, configRetryCount_);
  }
  uint8_t access[24] = {}; size_t length = 0;
  const uint16_t element = node->unicastAddress;
  const uint16_t commonGroup =
      studio::mesh::repository().data().network.groupAddress;
  const uint16_t controlGroup = controlGroupFor(node->instanceId);
  const auto put16le = [](uint8_t* out, uint16_t value) {
    out[0] = static_cast<uint8_t>(value);
    out[1] = static_cast<uint8_t>(value >> 8);
  };
  switch (configStep_) {
    case 0:
      access[0] = 0x80; access[1] = 0x08; access[2] = 0; length = 3;
      break;
    case 1:
      access[0] = 0; access[1] = access[2] = access[3] = 0;
      std::memcpy(access + 4,
                  studio::mesh::repository().data().network.applicationKey,
                  16);
      length = 20;
      break;
    case 2:
      if (node->vendorCompanyId == 0) {
        if (Session* session = sessionFor(node->instanceId)) {
          fail(*session, "Unknown vendor model");
        }
        APUTURE_LIGHT_LOG.printf(
            "aputure_light event=config_failed step=%u reason=unknown_vendor\n",
            configStep_);
        return false;
      }
      access[0] = 0x80; access[1] = 0x3d;
      put16le(access + 2, element); put16le(access + 4, 0);
      put16le(access + 6, node->vendorCompanyId);
      put16le(access + 8, node->vendorModelId);
      length = 10;
      break;
    case 3:
    case 4:
      access[0] = 0x80; access[1] = 0x1b;
      put16le(access + 2, element);
      put16le(access + 4, configStep_ == 3 ? commonGroup : controlGroup);
      put16le(access + 6, node->vendorCompanyId);
      put16le(access + 8, node->vendorModelId);
      length = 10;
      break;
    case 5:
      access[0] = 0x80; access[1] = 0x3d;
      put16le(access + 2, element); put16le(access + 4, 0);
      put16le(access + 6, 0x1000);
      length = 8;
      break;
    case 6:
      access[0] = 0x80; access[1] = 0x1b;
      put16le(access + 2, element); put16le(access + 4, commonGroup);
      put16le(access + 6, 0x1000);
      length = 8;
      break;
    default: {
      node->configured = true;
      node->configurationVersion = kCurrentConfigurationVersion;
      if (!studio::mesh::repository().save()) {
        node->configured = false;
        node->configurationVersion = 0;
        APUTURE_LIGHT_LOG.printf(
            "aputure_light event=config_failed step=%u reason=save\n", configStep_);
        return false;
      }
      APUTURE_LIGHT_LOG.printf("aputure_light event=config_complete step=%u\n", configStep_);
      studio::ble::bleCentral().markProtocolReady(link_);
      updateSharedReady();
      refreshGroupPower();
      return true;
    }
  }
  const size_t pduCount = length <= 11 ? 1 : (length + 4 + 11) / 12;
  uint32_t sequences[4] = {};
  for (size_t i = 0; i < pduCount; ++i) {
    if (!studio::mesh::repository().sequences().next(sequences[i])) {
      APUTURE_LIGHT_LOG.printf(
          "aputure_light event=config_failed step=%u reason=sequence index=%u\n",
          configStep_, static_cast<unsigned>(i));
      return false;
    }
  }
  NetworkPduBatch batch;
  if (pduCount == 1) {
    batch.count = 1;
    if (!encodeDeviceMessage(studio::mesh::repository().data().network.networkKey, node->deviceKey,
        access, length, sequences[0], studio::mesh::repository().data().network.provisionerAddress,
        node->unicastAddress, studio::mesh::repository().data().network.ivIndex, batch.pdus[0])) {
      APUTURE_LIGHT_LOG.printf(
          "aputure_light event=config_failed step=%u reason=encode\n", configStep_);
      return false;
    }
  } else if (!encodeSegmentedDeviceMessage(studio::mesh::repository().data().network.networkKey,
      node->deviceKey, access, length, sequences, pduCount,
      studio::mesh::repository().data().network.provisionerAddress, node->unicastAddress,
      studio::mesh::repository().data().network.ivIndex, batch)) {
    APUTURE_LIGHT_LOG.printf(
        "aputure_light event=config_failed step=%u reason=encode_segmented\n",
        configStep_);
    return false;
  }
  configBatch_ = batch;
  configBatchIndex_ = 0;
  nextConfigAt_ = millis();
  return true;
}

bool AputureLightRuntime::sendAccess(studio::InstanceId id, const uint8_t* access, size_t length) {
  const MeshNodeRecord* node = findNode(studio::mesh::repository().data(), id);
  if (node == nullptr || !node->configured) return false;
  return sendAccessTo(node->unicastAddress, access, length);
}

uint16_t AputureLightRuntime::controlGroupFor(studio::InstanceId id) const {
  const MeshStoreData& meshData = studio::mesh::repository().data();
  const MeshNodeRecord* node = findNode(meshData, id);
  return node != nullptr ? defaultControlGroupAddress(meshData, *node) : 0;
}

bool AputureLightRuntime::sendAccessTo(uint16_t destination, const uint8_t* access,
                                 size_t length) {
  if (dataIn_ == nullptr || !connected_) return false;
  uint32_t sequence; if (!studio::mesh::repository().sequences().next(sequence)) return false;
  NetworkPdu network;
  if (!encodeAccessMessage(studio::mesh::repository().data().network.networkKey, studio::mesh::repository().data().network.applicationKey,
      access, length, sequence, studio::mesh::repository().data().network.provisionerAddress,
      destination, studio::mesh::repository().data().network.ivIndex, network)) return false;
  uint8_t proxy[70]; size_t proxyLength = 0;
  return wrapProxyPdu(network, proxy, sizeof(proxy), proxyLength) &&
         dataIn_->writeValue(proxy, proxyLength, false);
}

bool AputureLightRuntime::refreshGroupPower() {
  AccessPayload payload;
  const bool sent = buildPowerStatusGetAccess(payload) &&
                    sendAccessTo(
                        studio::mesh::repository().data().network.groupAddress,
                        payload.bytes, payload.length);
  lastPowerPollMs_ = millis();
  return sent;
}

studio::CommandStatus AputureLightRuntime::dispatch(const studio::DeviceCommand& command) {
  Session* session = sessionFor(command.instanceId);
  if (session == nullptr) return studio::CommandStatus::Unavailable;
  AccessPayload payload;
  bool valid = false;
  if (command.type == studio::CommandType::Refresh) {
    session->state.commandPending = true;
    const bool sent = refreshGroupPower();
    session->state.commandPending = false;
    session->state.lastCommandFailed = !sent;
    return sent ? studio::CommandStatus::Succeeded
                : studio::CommandStatus::Unavailable;
  }
  if (command.type == studio::CommandType::TurnOn || command.type == studio::CommandType::TurnOff) valid = buildPowerAccess(command.type == studio::CommandType::TurnOn, payload);
  else if (command.type == studio::CommandType::SetLightCct && validCctCommand(command.value0, command.value1, command.value2)) valid = buildCctAccess(command.value0, command.value2, command.value1, payload);
  else if (command.type == studio::CommandType::SetLightRgb && validRgbCommand(command.value0, command.value1)) valid = buildRgbAccess(command.value0, command.value1, payload);
  else if (command.type == studio::CommandType::Connect) { session->state.phase=AputureLightState::Phase::Scanning;session->state.error[0]='\0';return beginLink(command.instanceId, findNode(studio::mesh::repository().data(), command.instanceId) == nullptr) ? studio::CommandStatus::Succeeded : studio::CommandStatus::Unavailable; }
  else return studio::CommandStatus::Unsupported;
  if (!valid) return studio::CommandStatus::InvalidArgument;
  session->state.commandPending = true;
  uint16_t destination = 0;
  if (command.type == studio::CommandType::TurnOn ||
      command.type == studio::CommandType::TurnOff) {
    destination = studio::mesh::repository().data().network.groupAddress;
  } else {
    destination = controlGroupFor(command.instanceId);
  }
  const bool sent = destination != 0 &&
                    sendAccessTo(destination, payload.bytes, payload.length);
  session->state.commandPending = false; session->state.lastCommandFailed = !sent;
  if (!sent) return studio::CommandStatus::Unavailable;
  session->state.optimistic = true;
  session->state.powerOptimistic =
      command.type == studio::CommandType::TurnOn ||
      command.type == studio::CommandType::TurnOff;
  if (command.type == studio::CommandType::TurnOn || command.type == studio::CommandType::TurnOff) {
    for (auto& member : sessions_) {
      if (member.instanceId == studio::kInvalidInstanceId) continue;
      member.state.on = command.type == studio::CommandType::TurnOn;
      member.state.optimistic = true;
      member.state.powerOptimistic = true;
    }
  }
  else if (command.type == studio::CommandType::SetLightCct) { session->state.mode=AputureLightState::Mode::Cct; session->state.kelvin=command.value0; session->state.cctBrightness=command.value1; session->state.tintPermille=command.value2; }
  else { session->state.mode=AputureLightState::Mode::Rgb; session->state.rgb=command.value0; session->state.rgbBrightness=command.value1; }
  return studio::CommandStatus::Succeeded;
}

studio::DeviceRuntimeState AputureLightRuntime::runtimeState(studio::InstanceId id) const {
  studio::DeviceRuntimeState out; const Session* session = sessionFor(id); if (!session) return out;
  if (session->state.phase == AputureLightState::Phase::Scanning) out.link = studio::LinkState::Scanning;
  else if (session->state.phase == AputureLightState::Phase::Provisioning || session->state.phase == AputureLightState::Phase::ConnectingProxy || session->state.phase == AputureLightState::Phase::PendingConfig) out.link = studio::LinkState::Connecting;
  else if (session->state.phase == AputureLightState::Phase::Ready) out.link = studio::LinkState::Connected;
  out.protocolReady = session->state.phase == AputureLightState::Phase::Ready;
  out.quality = session->state.optimistic
                    ? studio::StateQuality::Optimistic
                    : (session->state.powerConfirmed &&
                               session->state.nodeReachable
                           ? studio::StateQuality::Confirmed
                           : studio::StateQuality::Unknown);
  out.commandPending = session->state.commandPending; out.commandFailed = session->state.lastCommandFailed;
  return out;
}
const AputureLightState* AputureLightRuntime::state(studio::InstanceId id) const { const Session* s=sessionFor(id); return s ? &s->state : nullptr; }
bool AputureLightRuntime::consumePairingUpdate(studio::InstanceId id, studio::DeviceRecord& record) {
  Session* session=sessionFor(id); const MeshNodeRecord* node=findNode(studio::mesh::repository().data(),id);
  if (!session || !node || !session->pairingDirty) return false;
  session->pairingDirty=false; record.paired=node->configured; return true;
}
void AputureLightRuntime::forgetLocal(studio::InstanceId id) {
  if (!studio::mesh::repository().begin()) return;
  if (removeNode(studio::mesh::repository().data(), id))
    studio::mesh::repository().save();
}
void AputureLightRuntime::fail(Session& session, const char* error) { session.state.phase=AputureLightState::Phase::Failed; session.state.lastCommandFailed=true; std::strncpy(session.state.error,error,sizeof(session.state.error)-1); }
void AputureLightRuntime::updateSharedReady() {
  for (auto& session : sessions_) {
    if (session.instanceId == studio::kInvalidInstanceId) continue;
    const MeshNodeRecord* node=findNode(studio::mesh::repository().data(),session.instanceId);
    if (node && node->configured) { session.state.phase=AputureLightState::Phase::Ready; session.state.proxyConnected=true; session.pairingDirty=true; }
  }
}

}  // namespace aputure_light

#endif
