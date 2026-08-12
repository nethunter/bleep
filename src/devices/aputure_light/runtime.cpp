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
  if (command.type == studio::CommandType::TurnOn ||
      command.type == studio::CommandType::TurnOff) {
    s->state.on = command.type == studio::CommandType::TurnOn;
    s->state.optimistic = true;
    s->state.powerOptimistic = true;
    return studio::CommandStatus::Succeeded;
  }
  const bool compoundCct =
      command.type == studio::CommandType::SetLightCctAndOn;
  const bool compoundRgb =
      command.type == studio::CommandType::SetLightRgbAndOn;
  if ((command.type == studio::CommandType::SetLightCct || compoundCct) &&
      validCctCommand(command.value0, command.value1, command.value2)) {
    s->state.mode = AputureLightState::Mode::Cct;
    s->state.kelvin = command.value0;
    s->state.cctBrightness = command.value1;
    s->state.tintPermille = command.value2;
    s->state.optimistic = true;
    s->state.powerOptimistic = false;
    if (compoundCct) {
      s->state.on = true;
      s->state.powerOptimistic = true;
    }
    return studio::CommandStatus::Succeeded;
  }
  if ((command.type == studio::CommandType::SetLightRgb || compoundRgb) &&
      validRgbCommand(command.value0, command.value1)) {
    s->state.mode = AputureLightState::Mode::Rgb;
    s->state.rgb = command.value0;
    s->state.rgbBrightness = command.value1;
    s->state.optimistic = true;
    s->state.powerOptimistic = false;
    if (compoundRgb) {
      s->state.on = true;
      s->state.powerOptimistic = true;
    }
    return studio::CommandStatus::Succeeded;
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
bool AputureLightRuntime::identifyVendorModel(studio::InstanceId, uint16_t,
                                              uint16_t, const char*) { return false; }
bool AputureLightRuntime::canIdentifyVendorModel(studio::InstanceId id) const {
  const Session* session = sessionFor(id);
  return session != nullptr &&
         session->state.phase == AputureLightState::Phase::Failed;
}
void AputureLightRuntime::cancelPendingCommand(studio::InstanceId id) {
  if (Session* session = sessionFor(id)) session->state.commandPending = false;
}
void AputureLightRuntime::forgetLocal(studio::InstanceId id) { if (auto* s = sessionFor(id)) s->state = AputureLightState{}; }
bool AputureLightRuntime::cancelOnboarding(studio::InstanceId id) {
  candidates_.clear();
  provisioningAddress_[0] = '\0';
  provisioningName_[0] = '\0';
  if (auto* s = sessionFor(id)) s->state = AputureLightState{};
  return true;
}
size_t AputureLightRuntime::onboardingCandidateCount(studio::InstanceId id) const {
  return sessionFor(id) != nullptr ? candidates_.count() : 0;
}
bool AputureLightRuntime::onboardingCandidate(
    studio::InstanceId id, size_t index,
    studio::OnboardingCandidate& candidate) const {
  if (sessionFor(id) == nullptr) return false;
  const auto* entry = candidates_.at(index);
  if (entry == nullptr) return false;
  candidate = studio::OnboardingCandidate{};
  candidate.token = entry->token;
  candidate.addressType = entry->advertisement.address.type;
  candidate.rssi = entry->advertisement.rssi;
  std::strncpy(candidate.address, entry->advertisement.address.value,
               sizeof(candidate.address) - 1);
  studio::ble::advertisementName(entry->advertisement, candidate.name,
                                 sizeof(candidate.name));
  if (candidate.name[0] == '\0') std::strncpy(candidate.name, "Aputure Light", sizeof(candidate.name) - 1);
  return true;
}
bool AputureLightRuntime::selectOnboardingCandidate(studio::InstanceId id,
                                                    uint32_t token) {
  Session* session = sessionFor(id);
  const auto* entry = candidates_.find(token);
  if (session == nullptr || entry == nullptr) return false;
  provisioningAddress_[0] = '\0';
  provisioningName_[0] = '\0';
  std::strncpy(provisioningAddress_, entry->advertisement.address.value,
               sizeof(provisioningAddress_) - 1);
  provisioningAddressType_ = entry->advertisement.address.type;
  studio::ble::advertisementName(entry->advertisement, provisioningName_,
                                 sizeof(provisioningName_));
  session->state.phase = AputureLightState::Phase::Provisioning;
  return true;
}
bool AputureLightRuntime::simObserveCandidate(const studio::ble::Advertisement& advertisement) { return candidates_.observe(advertisement); }
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
void AputureLightRuntime::fail(Session& s, const char* error) { s.state.phase = AputureLightState::Phase::Failed; std::strncpy(s.state.error, error, sizeof(s.state.error)-1); }
void AputureLightRuntime::updateSharedReady() {}
studio::InstanceId AputureLightRuntime::preferredGatewayInstance() const {
  for (const auto& session : sessions_) {
    if (session.instanceId != studio::kInvalidInstanceId)
      return session.instanceId;
  }
  return studio::kInvalidInstanceId;
}
bool AputureLightRuntime::hasActiveUsers() const {
  return preferredGatewayInstance() != studio::kInvalidInstanceId;
}
bool AputureLightRuntime::isKnownGatewayAddress(const char*) const { return true; }
void AputureLightRuntime::returnToOnboardingPicker(const char*) {}
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
  const char* productName = knownProductName(record.displayName);
  if (productName == nullptr) productName = knownProductName(record.bleName);
  if (productName != nullptr) {
    std::strncpy(session->productName, productName,
                 sizeof(session->productName) - 1);
    session->productName[sizeof(session->productName) - 1] = '\0';
  }
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

bool AputureLightRuntime::isKnownGatewayAddress(const char* address) const {
  return isKnownMeshProxyAddress(studio::mesh::repository().data(), address);
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
  if (id == linkInstance_) rollbackPendingProvision();
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
  if (provisioning) candidates_.clear();
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
       isKnownGatewayAddress(advertisement.address.value))) {
    char observedName[studio::kBleNameCapacity] = "";
    studio::ble::advertisementName(advertisement, observedName,
                                   sizeof(observedName));
    APUTURE_LIGHT_LOG.printf(
        "aputure_light event=advertisement role=%s address=%s address_type=%u name=%s\n",
        provisioningLink_ ? "provisioning" : "proxy",
        advertisement.address.value,
        static_cast<unsigned>(advertisement.address.type),
        observedName[0] != '\0' ? observedName : "<empty>");
    if (provisioningLink_) {
      candidates_.observe(advertisement);
      return;
    }
    if (Session* session = sessionFor(linkInstance_)) {
      session->state.phase = provisioningLink_
          ? AputureLightState::Phase::Provisioning
          : AputureLightState::Phase::ConnectingProxy;
    }
    studio::ble::bleCentral().selectAdvertisement(link_, advertisement);
  }
}

size_t AputureLightRuntime::onboardingCandidateCount(
    studio::InstanceId id) const {
  return provisioningLink_ && sessionFor(id) != nullptr ? candidates_.count()
                                                         : 0;
}

bool AputureLightRuntime::onboardingCandidate(
    studio::InstanceId id, size_t index,
    studio::OnboardingCandidate& candidate) const {
  if (!provisioningLink_ || sessionFor(id) == nullptr) return false;
  const auto* entry = candidates_.at(index);
  if (entry == nullptr) return false;
  candidate = studio::OnboardingCandidate{};
  candidate.token = entry->token;
  candidate.addressType = entry->advertisement.address.type;
  candidate.rssi = entry->advertisement.rssi;
  std::strncpy(candidate.address, entry->advertisement.address.value,
               sizeof(candidate.address) - 1);
  studio::ble::advertisementName(entry->advertisement, candidate.name,
                                 sizeof(candidate.name));
  if (candidate.name[0] == '\0') {
    std::strncpy(candidate.name, "Aputure Light", sizeof(candidate.name) - 1);
  }
  return true;
}

bool AputureLightRuntime::selectOnboardingCandidate(studio::InstanceId id,
                                                    uint32_t token) {
  Session* session = sessionFor(id);
  const auto* entry = candidates_.find(token);
  if (!provisioningLink_ || session == nullptr || entry == nullptr) return false;
  if (!studio::ble::bleCentral().selectAdvertisement(link_,
                                                      entry->advertisement)) {
    session->state.phase = AputureLightState::Phase::Scanning;
    studio::ble::bleCentral().requestScan(link_, true);
    return false;
  }
  std::strncpy(provisioningAddress_, entry->advertisement.address.value,
               sizeof(provisioningAddress_) - 1);
  provisioningAddress_[sizeof(provisioningAddress_) - 1] = '\0';
  provisioningAddressType_ = entry->advertisement.address.type;
  studio::ble::advertisementName(entry->advertisement, provisioningName_,
                                 sizeof(provisioningName_));
  session->state.phase = AputureLightState::Phase::Provisioning;
  return true;
}

void AputureLightRuntime::returnToOnboardingPicker(const char* error) {
  if (!rollbackPendingProvision()) {
    if (Session* session = sessionFor(linkInstance_)) {
      session->state.phase = AputureLightState::Phase::Failed;
      session->state.lastCommandFailed = true;
      std::strncpy(session->state.error, "Mesh rollback save failed",
                   sizeof(session->state.error) - 1);
      session->state.error[sizeof(session->state.error) - 1] = '\0';
    }
    return;
  }
  provisioner_.cancel();
  provisioningLink_ = true;
  provisioningAddress_[0] = '\0';
  provisioningName_[0] = '\0';
  connected_ = false;
  dataIn_ = nullptr;
  if (Session* session = sessionFor(linkInstance_)) {
    session->state.phase = AputureLightState::Phase::Scanning;
    session->state.lastCommandFailed = error != nullptr;
    if (error != nullptr) {
      std::strncpy(session->state.error, error,
                   sizeof(session->state.error) - 1);
      session->state.error[sizeof(session->state.error) - 1] = '\0';
    }
  }
  if (link_ != studio::ble::kInvalidLinkHandle) {
    studio::ble::bleCentral().disconnect(link_, false);
    studio::ble::bleCentral().requestScan(link_, true);
  }
}

bool AputureLightRuntime::rollbackPendingProvision() {
  if (provisioningSnapshot_ == nullptr) return true;
  const uint32_t sequenceHighWater =
      studio::mesh::repository().data().network.sequenceHighWater;
  MeshStoreData restored = *provisioningSnapshot_;
  if (sequenceHighWater >
      restored.network.sequenceHighWater) {
    restored.network.sequenceHighWater = sequenceHighWater;
  }
  if (!studio::mesh::repository().replace(restored)) return false;
  delete provisioningSnapshot_;
  provisioningSnapshot_ = nullptr;
  if (Session* session = sessionFor(linkInstance_)) session->pairingDirty = false;
  return true;
}

bool AputureLightRuntime::cancelOnboarding(studio::InstanceId id) {
  if (id != linkInstance_) return true;
  if (!rollbackPendingProvision()) {
    if (Session* session = sessionFor(id)) {
      session->state.phase = AputureLightState::Phase::Failed;
      session->state.lastCommandFailed = true;
      std::strncpy(session->state.error, "Mesh rollback save failed",
                   sizeof(session->state.error) - 1);
      session->state.error[sizeof(session->state.error) - 1] = '\0';
    }
    return false;
  }
  provisioner_.cancel();
  candidates_.clear();
  provisioningAddress_[0] = '\0';
  provisioningName_[0] = '\0';
  if (link_ != studio::ble::kInvalidLinkHandle) {
    studio::ble::bleCentral().disconnect(link_, false);
  }
  return true;
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
      if (provisioningLink_ || provisioningSnapshot_ != nullptr) {
        returnToOnboardingPicker("GATT setup failed");
        return;
      }
      if (Session* session = sessionFor(linkInstance_))
        fail(*session, "GATT setup failed");
      studio::ble::bleCentral().markProtocolFailed(link_);
    }
  } else if (event.type == studio::ble::EventType::Disconnected) {
    connected_ = false;
    dataIn_ = nullptr;
    configAwaitingStatus_ = false;
    configBatch_ = NetworkPduBatch{};
    configBatchIndex_ = 0;
    ++gatewayGeneration_;
    if (provisioningLink_) {
      provisioningAddress_[0] = '\0';
      provisioningName_[0] = '\0';
      provisioner_.cancel();
      if (Session* session = sessionFor(linkInstance_)) {
        session->state.phase = AputureLightState::Phase::Scanning;
      }
    }
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
    if (provisioningLink_ || provisioningSnapshot_ != nullptr) {
      returnToOnboardingPicker("Connect failed");
      return;
    }
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
  const MeshStoreData& diagnosticMesh = studio::mesh::repository().data();
  for (uint8_t i = 0; i < diagnosticMesh.nodeCount; ++i) {
    const MeshNodeRecord& diagnosticNode = diagnosticMesh.nodes[i];
    if (diagnosticNode.model != studio::DriverId::AputureLight) continue;
    APUTURE_LIGHT_LOG.printf(
        "aputure_light event=node instance=%lu unicast=0x%04x configured=%u config_version=%u company=0x%04x model=0x%04x group=0x%04x address=%s\n",
        static_cast<unsigned long>(diagnosticNode.instanceId),
        diagnosticNode.unicastAddress, diagnosticNode.configured ? 1u : 0u,
        static_cast<unsigned>(diagnosticNode.configurationVersion),
        diagnosticNode.vendorCompanyId, diagnosticNode.vendorModelId,
        diagnosticNode.controlGroupAddress,
        diagnosticNode.bleAddress[0] != '\0' ? diagnosticNode.bleAddress
                                               : "<empty>");
  }
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
  for (auto& session : sessions_) {
    if (session.state.nodeReachable &&
        static_cast<uint32_t>(now - session.state.lastSeenMs) >
            kNodeFreshnessMs) {
      session.state.nodeReachable = false;
    }
    if (session.compoundPending &&
        static_cast<int32_t>(now - session.compoundPowerAt) >= 0) {
      AccessPayload power;
      const bool sent = buildPowerAccess(true, power) &&
                        sendAccess(session.instanceId, power.bytes,
                                   power.length);
      session.compoundPending = false;
      session.state.commandPending = false;
      session.state.lastCommandFailed = !sent;
      if (sent) {
        session.state.on = true;
        session.state.optimistic = true;
        session.state.powerOptimistic = true;
      }
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
      returnToOnboardingPicker("Provisioning failed");
    }
    return;
  }

  const MeshStoreData& meshData = studio::mesh::repository().data();
  const MeshNodeRecord* configuringNode = findNode(meshData, linkInstance_);
  if (configuringNode != nullptr && configAwaitingStatus_) {
    APUTURE_LIGHT_LOG.print("aputure_light event=config_rx bytes=");
    for (size_t i = 0; i < notification.length; ++i) {
      APUTURE_LIGHT_LOG.printf("%02x", notification.bytes[i]);
    }
    APUTURE_LIGHT_LOG.println();
  }
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
  APUTURE_LIGHT_LOG.printf(
      "aputure_light event=access_rx source=0x%04x destination=0x%04x sequence=%lu bytes=",
      decoded.source, decoded.destination,
      static_cast<unsigned long>(decoded.sequence));
  for (size_t i = 0; i < decoded.accessLength; ++i) {
    APUTURE_LIGHT_LOG.printf("%02x", decoded.access[i]);
  }
  APUTURE_LIGHT_LOG.println();
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
  ConfigurationStatusExpectation expected;
  expected.elementAddress = node->unicastAddress;
  expected.companyId = node->vendorCompanyId;
  expected.modelId = node->vendorModelId;
  expected.vendorModel = configStep_ >= 2 && configStep_ <= 4;
  if (configStep_ == 1) {
    expected.type = ConfigurationStatusType::AppKey;
  } else if (configStep_ == 2 || configStep_ == 5) {
    expected.type = ConfigurationStatusType::ModelApp;
    if (configStep_ == 5) expected.modelId = 0x1000;
  } else if (configStep_ >= 3 && configStep_ <= 6) {
    expected.type = ConfigurationStatusType::ModelSubscription;
    expected.groupAddress = configStep_ == 4
                                ? controlGroupFor(node->instanceId)
                                : studio::mesh::repository().data().network.groupAddress;
    if (configStep_ == 6) expected.modelId = 0x1000;
  } else {
    return false;
  }
  uint8_t status = 0;
  if (!matchConfigurationStatus(decoded.access, decoded.accessLength, expected,
                                status)) {
    APUTURE_LIGHT_LOG.printf(
        "aputure_light event=config_status_ignored step=%u source=0x%04x sequence=%lu\n",
        configStep_, decoded.source, static_cast<unsigned long>(decoded.sequence));
    return false;
  }
  const uint16_t opcode = static_cast<uint16_t>(decoded.access[0]) << 8 |
                          decoded.access[1];
  configAwaitingStatus_ = false;
  if (status != 0) {
    returnToOnboardingPicker("Mesh config rejected");
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
  MeshStoreData* snapshot = new (std::nothrow) MeshStoreData(previous);
  if (snapshot == nullptr) return false;
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
  APUTURE_LIGHT_LOG.printf(
      "aputure_light event=provisioned address=%s address_type=%u name=%s company=0x%04x model=0x%04x unicast=0x%04x elements=%u\n",
      node.bleAddress, static_cast<unsigned>(node.bleAddressType),
      provisioningName_[0] != '\0' ? provisioningName_ : "<empty>",
      node.vendorCompanyId, node.vendorModelId, node.unicastAddress,
      static_cast<unsigned>(node.elementCount));
  node.controlGroupAddress = defaultControlGroupAddress(meshData, node);
  const char* productName = knownProductName(provisioningName_);
  if (productName != nullptr) {
    std::strncpy(session->productName, productName,
                 sizeof(session->productName) - 1);
  }
  if (node.unicastAddress > 0x7fff - node.elementCount ||
      !upsertNode(meshData, node)) {
    delete snapshot;
    return false;
  }
  meshData.network.nextUnicastAddress =
      static_cast<uint16_t>(node.unicastAddress + node.elementCount);
  if (!studio::mesh::repository().save()) {
    meshData = previous;
    delete snapshot;
    return false;
  }
  delete provisioningSnapshot_;
  provisioningSnapshot_ = snapshot;
  session->state.phase = AputureLightState::Phase::PendingConfig;
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
      returnToOnboardingPicker("Mesh config timeout");
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
      delete provisioningSnapshot_;
      provisioningSnapshot_ = nullptr;
      APUTURE_LIGHT_LOG.printf("aputure_light event=config_complete step=%u\n", configStep_);
      studio::ble::bleCentral().markProtocolReady(link_);
      updateSharedReady();
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
  return memberControlGroupAddress(meshData, id);
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
  if (!wrapProxyPdu(network, proxy, sizeof(proxy), proxyLength)) return false;
  APUTURE_LIGHT_LOG.printf(
      "aputure_light event=access_tx destination=0x%04x sequence=%lu bytes=",
      destination, static_cast<unsigned long>(sequence));
  for (size_t i = 0; i < length; ++i) APUTURE_LIGHT_LOG.printf("%02x", access[i]);
  const bool sent = dataIn_->writeValue(proxy, proxyLength, false);
  APUTURE_LIGHT_LOG.printf(" result=%s\n", sent ? "ok" : "failed");
  return sent;
}

studio::CommandStatus AputureLightRuntime::dispatch(const studio::DeviceCommand& command) {
  Session* session = sessionFor(command.instanceId);
  if (session == nullptr) return studio::CommandStatus::Unavailable;
  AccessPayload payload;
  bool valid = false;
  if (command.type == studio::CommandType::Refresh) {
    // No read-only vendor power query has been captured. In particular,
    // 260e... is a group Power On command and must never be used as polling.
    session->state.lastCommandFailed = false;
    return studio::CommandStatus::Succeeded;
  }
  const bool compoundCct =
      command.type == studio::CommandType::SetLightCctAndOn;
  const bool compoundRgb =
      command.type == studio::CommandType::SetLightRgbAndOn;
  if (command.type == studio::CommandType::TurnOn ||
      command.type == studio::CommandType::TurnOff) {
    valid = buildPowerAccess(command.type == studio::CommandType::TurnOn,
                             payload);
  } else if ((command.type == studio::CommandType::SetLightCct || compoundCct) &&
             validCctCommand(command.value0, command.value1, command.value2)) {
    valid = buildCctAccess(command.value0, command.value2, command.value1,
                           payload);
  } else if ((command.type == studio::CommandType::SetLightRgb || compoundRgb) &&
             validRgbCommand(command.value0, command.value1)) {
    valid = buildRgbAccess(command.value0, command.value1, payload);
  }
  else if (command.type == studio::CommandType::Connect) {
    if (provisioningSnapshot_ != nullptr) {
      returnToOnboardingPicker();
      return provisioningSnapshot_ == nullptr
                 ? studio::CommandStatus::Succeeded
                 : studio::CommandStatus::Unavailable;
    }
    MeshNodeRecord* node =
        findNode(studio::mesh::repository().data(), command.instanceId);
    session->state.error[0] = '\0';
    if (connected_ && !provisioningLink_ && dataIn_ != nullptr &&
        node != nullptr && !node->configured) {
      linkInstance_ = command.instanceId;
      session->state.phase = AputureLightState::Phase::PendingConfig;
      configStep_ = 1;
      configRetryCount_ = 0;
      configAwaitingStatus_ = false;
      configBatch_ = NetworkPduBatch{};
      configBatchIndex_ = 0;
      nextConfigAt_ = millis();
      return studio::CommandStatus::Succeeded;
    }
    session->state.phase = AputureLightState::Phase::Scanning;
    return beginLink(command.instanceId, node == nullptr)
               ? studio::CommandStatus::Succeeded
               : studio::CommandStatus::Unavailable;
  }
  else return studio::CommandStatus::Unsupported;
  if (!valid) return studio::CommandStatus::InvalidArgument;
  session->state.commandPending = true;
  const bool sent = sendAccess(command.instanceId, payload.bytes,
                               payload.length);
  session->state.commandPending = false; session->state.lastCommandFailed = !sent;
  if (!sent) return studio::CommandStatus::Unavailable;
  session->state.optimistic = true;
  session->state.powerOptimistic =
      command.type == studio::CommandType::TurnOn ||
      command.type == studio::CommandType::TurnOff;
  if (command.type == studio::CommandType::TurnOn ||
      command.type == studio::CommandType::TurnOff) {
    session->state.on = command.type == studio::CommandType::TurnOn;
  } else if (command.type == studio::CommandType::SetLightCct || compoundCct) {
    session->state.mode = AputureLightState::Mode::Cct;
    session->state.kelvin = command.value0;
    session->state.cctBrightness = command.value1;
    session->state.tintPermille = command.value2;
  } else {
    session->state.mode = AputureLightState::Mode::Rgb;
    session->state.rgb = command.value0;
    session->state.rgbBrightness = command.value1;
  }
  if (compoundCct || compoundRgb) {
    session->compoundPending = true;
    session->compoundPowerAt = millis() + 100;
    session->state.commandPending = true;
  }
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
  session->pairingDirty=false;
  record.paired=node->configured;
  record.bleAddressType=node->bleAddressType;
  std::strncpy(record.bleAddress, node->bleAddress,
               sizeof(record.bleAddress) - 1);
  record.bleAddress[sizeof(record.bleAddress) - 1] = '\0';
  const char* modelName = session->productName[0] != '\0'
                              ? session->productName
                              : knownProductName(record.displayName);
  if (modelName == nullptr) modelName = knownProductName(record.bleName);
  if (modelName == nullptr) {
    modelName = knownVendorModelName(node->vendorCompanyId,
                                     node->vendorModelId);
  }
  if (modelName != nullptr) {
    std::strncpy(record.bleName, modelName, sizeof(record.bleName) - 1);
    record.bleName[sizeof(record.bleName) - 1] = '\0';
    const bool managedName =
        std::strcmp(record.displayName, "Aputure Light") == 0 ||
        std::strcmp(record.displayName, "Aputure MC Pro") == 0 ||
        std::strcmp(record.displayName, "amaran Ace 25c") == 0 ||
        std::strcmp(record.displayName, "amaran Pano 60c") == 0 ||
        std::strcmp(record.displayName, "amaran Pano 120c") == 0;
    if (managedName) {
      std::strncpy(record.displayName, modelName,
                   sizeof(record.displayName) - 1);
      record.displayName[sizeof(record.displayName) - 1] = '\0';
    }
  }
  return true;
}
bool AputureLightRuntime::identifyVendorModel(studio::InstanceId id,
                                              uint16_t companyId,
                                              uint16_t modelId,
                                              const char* productName) {
  APUTURE_LIGHT_LOG.printf(
      "aputure_light event=identify_requested instance=%lu company=0x%04x model=0x%04x\n",
      static_cast<unsigned long>(id), companyId, modelId);
  if (companyId == 0 || !ensureLoaded()) {
    APUTURE_LIGHT_LOG.println(
        "aputure_light event=identify_failed reason=repository");
    return false;
  }
  MeshStoreData& meshData = studio::mesh::repository().data();
  MeshNodeRecord* node = findNode(meshData, id);
  Session* session = sessionFor(id);
  if (node == nullptr || node->configured || session == nullptr) {
    APUTURE_LIGHT_LOG.printf(
        "aputure_light event=identify_failed reason=state node=%u configured=%u session=%u\n",
        node != nullptr ? 1u : 0u,
        node != nullptr && node->configured ? 1u : 0u,
        session != nullptr ? 1u : 0u);
    return false;
  }
  const MeshNodeRecord previous = *node;
  if (!assignVendorModel(meshData, id, companyId, modelId)) {
    APUTURE_LIGHT_LOG.println(
        "aputure_light event=identify_failed reason=assignment");
    return false;
  }
  if (!studio::mesh::repository().save()) {
    *node = previous;
    APUTURE_LIGHT_LOG.println(
        "aputure_light event=identify_failed reason=save");
    return false;
  }
  session->state.phase = AputureLightState::Phase::PendingConfig;
  session->state.error[0] = '\0';
  session->state.lastCommandFailed = false;
  const char* canonicalProduct = knownProductName(productName);
  if (canonicalProduct != nullptr) {
    std::strncpy(session->productName, canonicalProduct,
                 sizeof(session->productName) - 1);
    session->productName[sizeof(session->productName) - 1] = '\0';
  }
  // The unknown-model gate is reached only after AppKey Add has already been
  // confirmed. Resume at the vendor bind instead of replaying that stage.
  configStep_ = 2;
  configRetryCount_ = 0;
  configAwaitingStatus_ = false;
  configBatch_ = NetworkPduBatch{};
  configBatchIndex_ = 0;
  nextConfigAt_ = millis();
  // Let DeviceManager apply and persist the confirmed fixture name while mesh
  // configuration continues; pairing itself remains false until completion.
  session->pairingDirty = true;
  const char* identifiedName = session->productName[0] != '\0'
                                   ? session->productName
                                   : knownVendorModelName(companyId, modelId);
  APUTURE_LIGHT_LOG.printf(
      "aputure_light event=identify_saved instance=%lu name=%s control_group=0x%04x resume_step=%u\n",
      static_cast<unsigned long>(id),
      identifiedName != nullptr ? identifiedName : "<unknown>",
      node->controlGroupAddress,
      static_cast<unsigned>(configStep_));
  if (connected_ && !provisioningLink_ && dataIn_ != nullptr) {
    linkInstance_ = id;
    return true;
  }
  return beginLink(id, false);
}
bool AputureLightRuntime::canIdentifyVendorModel(studio::InstanceId id) const {
  const MeshNodeRecord* node =
      findNode(studio::mesh::repository().data(), id);
  return node != nullptr && !node->configured && sessionFor(id) != nullptr;
}
void AputureLightRuntime::cancelPendingCommand(studio::InstanceId id) {
  Session* session = sessionFor(id);
  if (session == nullptr) return;
  session->compoundPending = false;
  session->compoundPowerAt = 0;
  session->state.commandPending = false;
  session->state.lastCommandFailed = false;
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
