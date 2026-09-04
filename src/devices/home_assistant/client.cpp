#include "devices/home_assistant/client.h"

#include <Arduino.h>
#include <cstring>
#include <new>

#include "core/preferences_store.h"
#include "devices/home_assistant/protocol.h"

#if ARDUINO_USB_CDC_ON_BOOT
#define HA_LOG Serial0
#else
#define HA_LOG Serial
#endif

#ifdef UI_SIMULATOR

namespace home_assistant {

HomeAssistantClient::~HomeAssistantClient() = default;

HomeAssistantClient::Session* HomeAssistantClient::sessionFor(
    studio::InstanceId instanceId) {
  for (auto& session : sessions_) if (session.instanceId == instanceId) return &session;
  return nullptr;
}
const HomeAssistantClient::Session* HomeAssistantClient::sessionFor(
    studio::InstanceId instanceId) const {
  for (const auto& session : sessions_) if (session.instanceId == instanceId) return &session;
  return nullptr;
}
bool HomeAssistantClient::activate(const studio::DeviceRecord& record) {
  if (sessionFor(record.instanceId) != nullptr) return true;
  for (auto& session : sessions_) {
    if (session.instanceId != studio::kInvalidInstanceId) continue;
    session.instanceId = record.instanceId;
    session.domain = record.homeAssistantDomain;
    std::strncpy(session.entityId, record.homeAssistantEntityId,
                 sizeof(session.entityId) - 1);
    session.state.present = true;
    session.state.available = true;
    session.state.stateKnown = session.domain != studio::HomeAssistantDomain::Button &&
                               session.domain != studio::HomeAssistantDomain::Scene &&
                               session.domain != studio::HomeAssistantDomain::Script;
    std::strcpy(session.state.stateText,
                session.state.stateKnown ? "off" : "ready");
    ++sessionCount_;
    authenticated_ = subscribed_ = true;
    return true;
  }
  return false;
}
void HomeAssistantClient::deactivate(studio::InstanceId instanceId) {
  Session* session = sessionFor(instanceId);
  if (session != nullptr) { *session = Session{}; --sessionCount_; }
  if (sessionCount_ == 0) authenticated_ = subscribed_ = false;
}
void HomeAssistantClient::loop() {}
void HomeAssistantClient::pumpShutdown(uint32_t) {}
studio::CommandStatus HomeAssistantClient::dispatch(
    const studio::DeviceCommand& command) {
  Session* session = sessionFor(command.instanceId);
  if (session == nullptr || !commandSupported(session->domain, command.type)) {
    return studio::CommandStatus::Unsupported;
  }
  session->state.lastCommand = studio::CommandStatus::Succeeded;
  if (command.type == studio::CommandType::TurnOn ||
      command.type == studio::CommandType::TurnOff) {
    session->state.stateKnown = true;
    session->state.on = command.type == studio::CommandType::TurnOn;
    std::strcpy(session->state.stateText, session->state.on ? "on" : "off");
  }
  return studio::CommandStatus::Succeeded;
}
studio::DeviceRuntimeState HomeAssistantClient::runtimeState(
    studio::InstanceId instanceId) const {
  studio::DeviceRuntimeState out;
  const Session* session = sessionFor(instanceId);
  if (session == nullptr) return out;
  out.link = studio::LinkState::Connected;
  out.protocolReady = true;
  out.quality = session->state.stateKnown ? studio::StateQuality::Confirmed
                                          : studio::StateQuality::Unknown;
  out.commandPending = session->state.commandPending;
  return out;
}
const EntityState* HomeAssistantClient::entityState(
    studio::InstanceId instanceId) const {
  const Session* session = sessionFor(instanceId);
  return session != nullptr ? &session->state : nullptr;
}
void HomeAssistantClient::enqueueFrame(const uint8_t*, size_t) {}
void HomeAssistantClient::markWebsocketDisconnected() {}
bool HomeAssistantClient::connectRuntime() { return true; }
void HomeAssistantClient::disconnectRuntime() {}
void HomeAssistantClient::processMessage(const char*, size_t) {}
void HomeAssistantClient::rebuildSubscription() {}
void HomeAssistantClient::refreshInitialStates() {}
void HomeAssistantClient::refreshSession(Session&) {}
void HomeAssistantClient::applyState(Session&, const char*) {}
void HomeAssistantClient::setFailure(studio::CommandStatus) {}

}  // namespace home_assistant

#else

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WebSocketsClient.h>
#include <WiFi.h>
#include <esp_netif.h>

#include "wifi_station_cache.h"

namespace home_assistant {
namespace {

HomeAssistantClient* activeClient = nullptr;
WebSocketsClient* websocket = nullptr;

uint32_t nowMs() { return millis(); }

void markStateUnknown(EntityState& state) {
  state.present = true;
  state.available = true;
  state.stateKnown = false;
  std::strcpy(state.stateText, "unknown");
}

bool parseEndpoint(const char* url, char* host, size_t hostCapacity,
                   uint16_t& port) {
  if (!studio::validLocalHomeAssistantUrl(url)) return false;
  const char* start = url + 7;
  const char* slash = std::strchr(start, '/');
  const char* end = slash != nullptr ? slash : start + std::strlen(start);
  const char* colon = nullptr;
  for (const char* cursor = start; cursor < end; ++cursor) {
    if (*cursor == ':') colon = cursor;
  }
  const char* hostEnd = colon != nullptr ? colon : end;
  const size_t length = static_cast<size_t>(hostEnd - start);
  if (length == 0 || length >= hostCapacity) return false;
  std::memcpy(host, start, length);
  host[length] = '\0';
  port = 8123;
  if (colon != nullptr) {
    const long parsed = std::strtol(colon + 1, nullptr, 10);
    if (parsed <= 0 || parsed > 65535) return false;
    port = static_cast<uint16_t>(parsed);
  }
  return true;
}

// Deferred station shutdown shared by every client instance (see
// HomeAssistantClient::pumpShutdown).
bool wifiShutdownPending = false;
uint32_t wifiShutdownStartedMs = 0;
constexpr uint32_t kWifiShutdownSettleMs = 250;
constexpr uint32_t kWifiShutdownTimeoutMs = 2000;

void websocketEvent(WStype_t type, uint8_t* payload, size_t length) {
  if (activeClient == nullptr) return;
  if (type == WStype_TEXT) {
    activeClient->enqueueFrame(payload, length);
  } else if (type == WStype_DISCONNECTED) {
    activeClient->markWebsocketDisconnected();
  }
}

}  // namespace

HomeAssistantClient::~HomeAssistantClient() { disconnectRuntime(); }

void HomeAssistantClient::pumpShutdown(uint32_t nowMs) {
  if (!wifiShutdownPending ||
      nowMs - wifiShutdownStartedMs < kWifiShutdownSettleMs) {
    return;
  }
  if (WiFi.status() == WL_CONNECTED &&
      nowMs - wifiShutdownStartedMs < kWifiShutdownTimeoutMs) {
    return;
  }
  WiFi.mode(WIFI_OFF);
  wifiShutdownPending = false;
}

HomeAssistantClient::Session* HomeAssistantClient::sessionFor(
    studio::InstanceId instanceId) {
  for (auto& session : sessions_) {
    if (session.instanceId == instanceId) return &session;
  }
  return nullptr;
}

const HomeAssistantClient::Session* HomeAssistantClient::sessionFor(
    studio::InstanceId instanceId) const {
  for (const auto& session : sessions_) {
    if (session.instanceId == instanceId) return &session;
  }
  return nullptr;
}

bool HomeAssistantClient::activate(const studio::DeviceRecord& record) {
  if (!supportedEntityId(record.homeAssistantEntityId) ||
      record.homeAssistantDomain != domainFromEntityId(record.homeAssistantEntityId)) {
    return false;
  }
  if (sessionFor(record.instanceId) != nullptr) return true;
  for (auto& session : sessions_) {
    if (session.instanceId != studio::kInvalidInstanceId) continue;
    session.instanceId = record.instanceId;
    session.domain = record.homeAssistantDomain;
    std::strncpy(session.entityId, record.homeAssistantEntityId,
                 sizeof(session.entityId) - 1);
    session.state = {};
    ++sessionCount_;
    subscriptionDirty_ = true;
    if (!wifiStarted_ && !connectRuntime()) {
      *sessionFor(record.instanceId) = Session{};
      --sessionCount_;
      return false;
    }
    return true;
  }
  return false;
}

void HomeAssistantClient::deactivate(studio::InstanceId instanceId) {
  Session* session = sessionFor(instanceId);
  if (session == nullptr) return;
  *session = Session{};
  --sessionCount_;
  subscriptionDirty_ = true;
  if (sessionCount_ == 0) disconnectRuntime();
}

bool HomeAssistantClient::connectRuntime() {
  static studio::PreferencesHomeAssistantBackend backend;
  studio::HomeAssistantConfigStore store(backend);
  if (store.load(config_) != studio::ConfigLoadStatus::Loaded ||
      !config_.homeAssistantConfigured || !config_.wifiConfigured) {
    HA_LOG.println("ha event=config_unavailable");
    return false;
  }
  // A shutdown that has not reached WIFI_OFF yet simply becomes a rejoin; the
  // driver is still initialized, so no un-init/re-init cycle is paid.
  wifiShutdownPending = false;
  WiFi.mode(WIFI_STA);
  // Keep the retained HA socket responsive while allowing the station radio
  // to enter modem sleep between access-point beacons.
  WiFi.setSleep(true);
  fastJoinAttempt_ =
      wifi_station_cache::begin(config_.wifiSsid, config_.wifiPassword);
  HA_LOG.printf("ha event=wifi_begin free_heap=%lu max_alloc=%lu fast=%u\n",
                static_cast<unsigned long>(ESP.getFreeHeap()),
                static_cast<unsigned long>(ESP.getMaxAllocHeap()),
                fastJoinAttempt_ ? 1u : 0u);
  wifiAssociated_ = false;
  wifiStarted_ = true;
  wifiStartedAtMs_ = nowMs();
  wifiDisconnectedAt_ = nowMs();
  retryAt_ = 0;
  return true;
}

void HomeAssistantClient::disconnectRuntime() {
  activeClient = nullptr;
  if (websocket != nullptr) {
    websocket->disconnect();
    delete websocket;
    websocket = nullptr;
  }
  // Stop DHCP, drop the association, and let pumpShutdown() turn the radio
  // off after a settle interval. Deinitializing immediately raced the tcpip
  // task ("wifi:timeout when WiFi un-init"), leaked heap, and left the
  // driver half-stopped.
  esp_netif_t* station = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
  if (station != nullptr) esp_netif_dhcpc_stop(station);
  WiFi.disconnect(false, false);
  wifiShutdownPending = true;
  wifiShutdownStartedMs = nowMs();
  fastJoinAttempt_ = wifiAssociated_ = false;
  wifiStarted_ = websocketStarted_ = authenticated_ = subscribed_ = false;
  wifiDisconnectedAt_ = 0;
  subscriptionId_ = 0;
  frameHead_ = frameTail_ = frameCount_ = 0;
  frameFault_ = false;
  websocketDisconnected_ = false;
}

void HomeAssistantClient::enqueueFrame(const uint8_t* payload, size_t length) {
  if (payload == nullptr || length > kFrameCapacity) {
    frameFault_ = true;
    return;
  }
  if (frameCount_ >= 2) {
    frameFault_ = true;
    return;
  }
  std::memcpy(frames_[frameHead_], payload, length);
  frames_[frameHead_][length] = '\0';
  frameLengths_[frameHead_] = length;
  frameHead_ = static_cast<uint8_t>((frameHead_ + 1) % 2);
  ++frameCount_;
}

void HomeAssistantClient::markWebsocketDisconnected() {
  websocketDisconnected_ = true;
}

void HomeAssistantClient::setFailure(studio::CommandStatus status) {
  for (auto& session : sessions_) {
    if (session.instanceId == studio::kInvalidInstanceId) continue;
    session.state.lastCommand = status;
    session.state.commandAttempted = true;
    session.state.stateKnown = false;
    session.state.commandPending = false;
    session.refreshNeeded = true;
    std::strcpy(session.state.stateText, "unknown");
  }
}

void HomeAssistantClient::applyState(Session& session, const char* state) {
  session.refreshNeeded = false;
  session.refreshAfterMs = 0;
  session.state.present = state != nullptr;
  if (state == nullptr) {
    session.state.available = false;
    session.state.stateKnown = false;
    std::strcpy(session.state.stateText, "missing");
    return;
  }
  session.state.available = std::strcmp(state, "unavailable") != 0 &&
                            std::strcmp(state, "unknown") != 0;
  std::strncpy(session.state.stateText, state,
               sizeof(session.state.stateText) - 1);
  const bool stateful = session.domain == studio::HomeAssistantDomain::Light ||
                        session.domain == studio::HomeAssistantDomain::Switch ||
                        session.domain == studio::HomeAssistantDomain::InputBoolean;
  session.state.stateKnown = stateful && session.state.available;
  session.state.on = std::strcmp(state, "on") == 0;
  if (session.state.commandPending && session.state.stateKnown &&
      session.state.on == session.expectedOn) {
    session.state.commandPending = false;
    session.state.lastCommand = studio::CommandStatus::Succeeded;
  }
}

void HomeAssistantClient::processMessage(const char* payload, size_t length) {
  JsonDocument doc;
  if (deserializeJson(doc, payload, length) != DeserializationError::Ok) {
    setFailure(studio::CommandStatus::InvalidArgument);
    return;
  }
  const char* type = doc["type"] | "";
  if (std::strcmp(type, "auth_required") == 0) {
    HA_LOG.println("ha event=ws_auth_required");
    char auth[384];
    if (buildAuth(auth, sizeof(auth), config_.token) && websocket != nullptr) {
      websocket->sendTXT(auth);
    }
    return;
  }
  if (std::strcmp(type, "auth_invalid") == 0) {
    HA_LOG.println("ha event=ws_auth_invalid");
    setFailure(studio::CommandStatus::Unavailable);
    disconnectRuntime();
    retryAt_ = nowMs() + 30000;
    return;
  }
  if (std::strcmp(type, "auth_ok") == 0) {
    HA_LOG.printf("ha event=ws_auth_ok free_heap=%lu max_alloc=%lu\n",
                  static_cast<unsigned long>(ESP.getFreeHeap()),
                  static_cast<unsigned long>(ESP.getMaxAllocHeap()));
    authenticated_ = true;
    failures_ = 0;
    refreshInitialStates();
    subscriptionDirty_ = true;
    return;
  }
  if (std::strcmp(type, "result") == 0) {
    const uint32_t id = doc["id"] | 0;
    const bool success = doc["success"] | false;
    if (id == subscriptionId_) {
      subscribed_ = success;
      HA_LOG.printf("ha event=subscription_result success=%u\n",
                    success ? 1u : 0u);
      if (success) {
        // A subscribed WebSocket is sufficient to route service calls. Initial
        // REST reads are best-effort state confirmation and may be deferred
        // under mixed BLE/Wi-Fi memory pressure; keep that state honest as
        // unknown without holding sequence preparation in Connecting.
        for (auto& session : sessions_) {
          if (session.instanceId != studio::kInvalidInstanceId &&
              !session.state.present) {
            markStateUnknown(session.state);
          }
        }
        HA_LOG.println("ha event=protocol_ready state=unknown");
      }
      return;
    }
    for (auto& session : sessions_) {
      if (session.pendingMessageId != id) continue;
      session.pendingMessageId = 0;
      if (!success) {
        session.state.commandPending = false;
        session.state.lastCommand = studio::CommandStatus::Unavailable;
        HA_LOG.printf("ha event=service_result id=%lu success=0\n",
                      static_cast<unsigned long>(id));
      } else {
        // Idempotent service calls may not produce a state-change event. API
        // acceptance completes delivery, but does not change the separately
        // confirmed entity state shown by the UI.
        session.state.commandPending = false;
        session.state.lastCommand = studio::CommandStatus::Succeeded;
        session.refreshNeeded = true;
        session.refreshAfterMs = nowMs() + 1000;
        HA_LOG.printf("ha event=service_result id=%lu success=1\n",
                      static_cast<unsigned long>(id));
      }
      return;
    }
  }
  if (std::strcmp(type, "event") != 0) return;
  JsonObject trigger = doc["event"]["variables"]["trigger"].as<JsonObject>();
  const char* entityId = trigger["entity_id"] | "";
  if (entityId[0] == '\0') {
    entityId = doc["event"]["data"]["entity_id"] | "";
  }
  const char* state = trigger["to_state"]["state"] | nullptr;
  if (state == nullptr) {
    state = doc["event"]["data"]["new_state"]["state"] | nullptr;
  }
  for (auto& session : sessions_) {
    if (std::strcmp(session.entityId, entityId) == 0) {
      if (state != nullptr) {
        applyState(session, state);
      } else {
        markStateUnknown(session.state);
        session.refreshNeeded = true;
      }
      return;
    }
  }
}

void HomeAssistantClient::refreshInitialStates() {
  for (auto& session : sessions_) {
    if (session.instanceId == studio::kInvalidInstanceId) continue;
    session.refreshNeeded = true;
    session.refreshAfterMs = 0;
    session.refreshFailures = 0;
  }
}

void HomeAssistantClient::refreshSession(Session& session) {
  constexpr uint32_t kMinimumRestFreeHeap = 20U * 1024U;
  constexpr uint32_t kMinimumRestLargestBlock = 12U * 1024U;
  const uint32_t freeHeap = ESP.getFreeHeap();
  const uint32_t largestBlock = ESP.getMaxAllocHeap();
  if (freeHeap < kMinimumRestFreeHeap ||
      largestBlock < kMinimumRestLargestBlock) {
    session.refreshNeeded = true;
    session.refreshAfterMs = nowMs() + 5000;
    HA_LOG.printf("ha event=rest_deferred free_heap=%lu max_alloc=%lu\n",
                  static_cast<unsigned long>(freeHeap),
                  static_cast<unsigned long>(largestBlock));
    return;
  }
  const bool confirmingCommand = session.state.commandPending;
  session.refreshNeeded = false;
  session.refreshAfterMs = 0;
  bool retry = false;
  int status = 0;
  HTTPClient http;
  char url[studio::kHomeAssistantUrlCapacity +
           studio::kHomeAssistantEntityIdCapacity + 16] = "";
  const size_t baseLength = std::strlen(config_.baseUrl);
  const bool trailingSlash =
      baseLength > 0 && config_.baseUrl[baseLength - 1] == '/';
  std::snprintf(url, sizeof(url), "%s%sapi/states/%s", config_.baseUrl,
                trailingSlash ? "" : "/", session.entityId);
  http.setConnectTimeout(1500);
  http.setTimeout(1500);
  if (!http.begin(url)) {
    markStateUnknown(session.state);
    retry = true;
    if (confirmingCommand) {
      session.state.commandPending = false;
      session.state.lastCommand = studio::CommandStatus::Unavailable;
    }
  } else {
    char auth[studio::kHomeAssistantTokenCapacity + 8] = "Bearer ";
    std::strncat(auth, config_.token, sizeof(auth) - std::strlen(auth) - 1);
    http.addHeader("Authorization", auth);
    status = http.GET();
    if (status == 200) {
      JsonDocument doc;
      const DeserializationError error = deserializeJson(doc, http.getStream());
      const char* state = error == DeserializationError::Ok
          ? doc["state"].as<const char*>() : nullptr;
      if (state != nullptr) {
        applyState(session, state);
        session.refreshFailures = 0;
      } else {
        markStateUnknown(session.state);
        retry = true;
      }
    } else if (status == 404) {
      applyState(session, nullptr);
      session.refreshFailures = 0;
    } else {
      markStateUnknown(session.state);
      retry = true;
    }
    http.end();
  }
  if (confirmingCommand && session.state.commandPending) {
    session.state.commandPending = false;
    session.state.lastCommand = studio::CommandStatus::Unavailable;
  }
  if (retry) {
    session.refreshNeeded = true;
    if (session.refreshFailures < 4) ++session.refreshFailures;
    const uint32_t delay = session.refreshFailures >= 4
                               ? 30000
                               : 1000u << session.refreshFailures;
    session.refreshAfterMs = nowMs() + delay;
    HA_LOG.printf("ha event=rest_retry status=%d free_heap=%lu max_alloc=%lu\n",
                  status, static_cast<unsigned long>(ESP.getFreeHeap()),
                  static_cast<unsigned long>(ESP.getMaxAllocHeap()));
  }
}

void HomeAssistantClient::rebuildSubscription() {
  if (!authenticated_) return;
  if (subscriptionId_ != 0) {
    char unsubscribe[96];
    std::snprintf(unsubscribe, sizeof(unsubscribe),
                  "{\"id\":%lu,\"type\":\"unsubscribe_events\","
                  "\"subscription\":%lu}",
                  static_cast<unsigned long>(nextMessageId_++),
                  static_cast<unsigned long>(subscriptionId_));
    if (websocket != nullptr) websocket->sendTXT(unsubscribe);
  }
  subscriptionId_ = nextMessageId_++;
  JsonDocument doc;
  doc["id"] = subscriptionId_;
  doc["type"] = "subscribe_trigger";
  JsonArray triggers = doc["trigger"].to<JsonArray>();
  for (const auto& session : sessions_) {
    if (session.instanceId == studio::kInvalidInstanceId) continue;
    JsonObject trigger = triggers.add<JsonObject>();
    trigger["platform"] = "state";
    trigger["entity_id"] = session.entityId;
  }
  char payload[512] = "";
  const size_t payloadLength = serializeJson(doc, payload, sizeof(payload));
  subscribed_ = false;
  if (websocket != nullptr && payloadLength < sizeof(payload)) {
    websocket->sendTXT(reinterpret_cast<uint8_t*>(payload), payloadLength);
  }
  subscriptionDirty_ = false;
}

void HomeAssistantClient::loop() {
  const uint32_t now = nowMs();
  if (sessionCount_ == 0) return;
  if (!wifiStarted_) {
    if (retryAt_ == 0 || now >= retryAt_) connectRuntime();
    return;
  }
  if (WiFi.status() != WL_CONNECTED) {
    if (wifiDisconnectedAt_ == 0) wifiDisconnectedAt_ = now;
    if (now - wifiDisconnectedAt_ > 10000) {
      HA_LOG.printf("ha event=wifi_timeout status=%d free_heap=%lu max_alloc=%lu\n",
                    static_cast<int>(WiFi.status()),
                    static_cast<unsigned long>(ESP.getFreeHeap()),
                    static_cast<unsigned long>(ESP.getMaxAllocHeap()));
      // A cached channel/BSSID that no longer answers must not steer the
      // next attempt; fall back to a full scan.
      if (fastJoinAttempt_) wifi_station_cache::invalidate();
      disconnectRuntime();
      ++failures_;
      const uint32_t delay = 1000u << (failures_ > 4 ? 4 : failures_);
      retryAt_ = now + delay;
    }
    return;
  }
  wifiDisconnectedAt_ = 0;
  if (!wifiAssociated_) {
    wifiAssociated_ = true;
    wifi_station_cache::rememberCurrent(config_.wifiSsid);
    HA_LOG.printf("ha event=wifi_connected fast=%u elapsed_ms=%lu\n",
                  fastJoinAttempt_ ? 1u : 0u,
                  static_cast<unsigned long>(now - wifiStartedAtMs_));
  }
  if (!websocketStarted_) {
    if (retryAt_ != 0 && static_cast<int32_t>(now - retryAt_) < 0) return;
    char host[80];
    uint16_t port = 8123;
    if (!parseEndpoint(config_.baseUrl, host, sizeof(host), port)) {
      setFailure(studio::CommandStatus::InvalidArgument);
      return;
    }
    if (websocket == nullptr) {
      websocket = new (std::nothrow) WebSocketsClient;
      if (websocket == nullptr) {
        setFailure(studio::CommandStatus::Unavailable);
        retryAt_ = now + 2000;
        return;
      }
    }
    activeClient = this;
    HA_LOG.printf("ha event=ws_begin free_heap=%lu max_alloc=%lu\n",
                  static_cast<unsigned long>(ESP.getFreeHeap()),
                  static_cast<unsigned long>(ESP.getMaxAllocHeap()));
    websocket->begin(host, port, "/api/websocket");
    websocket->onEvent(websocketEvent);
    websocket->setReconnectInterval(0);
    websocketStarted_ = true;
  }
  websocket->loop();
  if (websocketDisconnected_) {
    websocketDisconnected_ = false;
    websocketStarted_ = authenticated_ = subscribed_ = false;
    subscriptionId_ = 0;
    retryAt_ = now + 1000;
    HA_LOG.printf("ha event=ws_disconnected free_heap=%lu max_alloc=%lu\n",
                  static_cast<unsigned long>(ESP.getFreeHeap()),
                  static_cast<unsigned long>(ESP.getMaxAllocHeap()));
    return;
  }
  if (frameFault_) {
    frameFault_ = false;
    HA_LOG.println("ha event=frame_fault");
    setFailure(studio::CommandStatus::InvalidArgument);
  }
  while (frameCount_ > 0) {
    // Consume the slot before handling it: an `auth_invalid` handler tears the
    // runtime down and zeroes frameCount_, and decrementing afterwards wrapped
    // the counter to 255 and replayed the same stale frame forever.
    const uint8_t index = frameTail_;
    frameTail_ = static_cast<uint8_t>((frameTail_ + 1) % 2);
    --frameCount_;
    processMessage(frames_[index], frameLengths_[index]);
    if (!websocketStarted_) return;
  }
  if (subscriptionDirty_) rebuildSubscription();
  for (auto& session : sessions_) {
    if (session.state.commandPending && now - session.pendingSince >= 5000) {
      session.refreshNeeded = true;
    }
  }
  for (auto& session : sessions_) {
    if (authenticated_ &&
        session.instanceId != studio::kInvalidInstanceId &&
        session.refreshNeeded &&
        (session.refreshAfterMs == 0 ||
         static_cast<int32_t>(now - session.refreshAfterMs) >= 0)) {
      refreshSession(session);
      break;
    }
  }
}

studio::CommandStatus HomeAssistantClient::dispatch(
    const studio::DeviceCommand& command) {
  Session* session = sessionFor(command.instanceId);
  if (session == nullptr || !authenticated_ || !subscribed_ ||
      !session->state.present || !session->state.available) {
    return studio::CommandStatus::Unavailable;
  }
  if (!commandSupported(session->domain, command.type)) {
    return studio::CommandStatus::Unsupported;
  }
  char payload[320];
  const uint32_t id = nextMessageId_++;
  if (!buildServiceCall(payload, sizeof(payload), id, session->domain,
                        command.type, session->entityId) ||
      websocket == nullptr || !websocket->sendTXT(payload)) {
    return studio::CommandStatus::QueueFull;
  }
  session->pendingMessageId = id;
  session->state.commandPending = true;
  session->state.commandAttempted = true;
  session->state.lastCommand = studio::CommandStatus::Queued;
  session->pendingSince = nowMs();
  session->expectedOn = command.type == studio::CommandType::TurnOn;
  return studio::CommandStatus::Succeeded;
}

studio::DeviceRuntimeState HomeAssistantClient::runtimeState(
    studio::InstanceId instanceId) const {
  studio::DeviceRuntimeState state;
  const Session* session = sessionFor(instanceId);
  if (session == nullptr) return state;
  if (WiFi.status() != WL_CONNECTED) {
    state.link = wifiStarted_ ? studio::LinkState::Connecting
                              : studio::LinkState::Disconnected;
  } else {
    state.link = studio::LinkState::Connected;
  }
  state.protocolReady = authenticated_ && subscribed_ && session->state.present;
  state.quality = session->state.stateKnown ? studio::StateQuality::Confirmed
                                            : studio::StateQuality::Unknown;
  state.commandPending = session->state.commandPending;
  state.commandFailed = session->state.commandAttempted &&
                        !session->state.commandPending &&
                        session->state.lastCommand != studio::CommandStatus::Succeeded;
  return state;
}

const EntityState* HomeAssistantClient::entityState(
    studio::InstanceId instanceId) const {
  const Session* session = sessionFor(instanceId);
  return session != nullptr ? &session->state : nullptr;
}

}  // namespace home_assistant

#endif
