#include "portal_service.h"

#include <Arduino.h>

#ifdef UI_SIMULATOR

namespace portal {
namespace {
bool running = false;
bool lan = false;
Status simulatedStatus = Status::Ready;
const char* simulatedMessage = "Set up studio Wi-Fi";
char simulatedSavedSsid[33] = "Studio-WiFi";
}
bool begin() {
  if (running) return true;
  running = true;
  lan = false;
  simulatedStatus = Status::Ready;
  simulatedMessage = "Set up studio Wi-Fi";
  return true;
}
void loop() {}
void stop() { running = false; }
bool active() { return running; }
Status status() { return running ? simulatedStatus : Status::Inactive; }
const char* statusText() { return running ? simulatedMessage : "Portal off"; }
const char* ssid() { return lan ? "Studio-WiFi" : "Bleep-Setup-0192C"; }
const char* password() { return ""; }
const char* url() { return lan ? "http://192.168.1.84" : "http://192.168.4.1"; }
const char* qrPayload() {
  return lan ? "http://192.168.1.84"
             : "WIFI:T:nopass;S:Bleep-Setup-0192C;;";
}
const char* unitId() { return "BLP-0123456789AB"; }
SavedWifiSummary savedWifiSummary() {
  SavedWifiSummary summary;
  summary.configured = simulatedSavedSsid[0] != '\0';
  std::strncpy(summary.ssid, simulatedSavedSsid, sizeof(summary.ssid) - 1);
  return summary;
}
void simSetLan(bool connected) {
  lan = connected;
  simulatedStatus = Status::Ready;
  simulatedMessage = connected ? "LAN portal ready" : "Set up studio Wi-Fi";
}
void simSetWifiFeedback(Status status, const char* message) {
  simulatedStatus = status;
  simulatedMessage = message;
}
void simSetSavedWifi(const char* ssid) {
  std::strncpy(simulatedSavedSsid, ssid != nullptr ? ssid : "",
               sizeof(simulatedSavedSsid) - 1);
  simulatedSavedSsid[sizeof(simulatedSavedSsid) - 1] = '\0';
}
}  // namespace portal

#else

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <esp_system.h>
#include <esp_netif.h>

#include <cstdio>
#include <cstring>
#include <new>

#include "core/device_manager.h"
#include "core/command_traits.h"
#include "core/home_assistant_config.h"
#include "core/panel_identity.h"
#include "core/preferences_store.h"
#include "core/scene_service.h"
#include "devices/home_assistant/protocol.h"
#include "assets/portal_logo.h"
#include "captive_dns_codec.h"
#include "portal_assets.h"
#include "portal_scene_parser.h"
#include "wifi_scan.h"

#if ARDUINO_USB_CDC_ON_BOOT
#define PORTAL_DEBUG_PORT Serial0
#else
#define PORTAL_DEBUG_PORT Serial
#endif

namespace portal {
namespace {

constexpr uint32_t kTimeoutMs = 10 * 60 * 1000;
constexpr size_t kDiscoveryLimit = 24;
constexpr size_t kWifiScanLimit = 16;
constexpr uint32_t kWifiConnectTimeoutMs = 15000;
constexpr uint32_t kLanHandoffDelayMs = 8000;
enum class WifiJoinState : uint8_t { Idle, Connecting, Connected, Failed };

struct WifiScanResult {
  char ssid[studio::kWifiSsidCapacity] = "";
  int32_t rssi = 0;
  bool secure = false;
};

WebServer* server = nullptr;
class CaptiveDnsServer {
 public:
  bool start(uint16_t port, IPAddress address) {
    for (size_t i = 0; i < sizeof(address_); ++i) address_[i] = address[i];
    return udp_.begin(port) == 1;
  }

  void stop() { udp_.stop(); }

  void processNextRequest() {
    const int packetSize = udp_.parsePacket();
    if (packetSize <= 0) return;
    const IPAddress remoteAddress = udp_.remoteIP();
    const uint16_t remotePort = udp_.remotePort();
    if (packetSize > static_cast<int>(sizeof(request_))) {
      while (udp_.available()) udp_.read();
      return;
    }
    const int bytesRead = udp_.read(request_, sizeof(request_));
    if (bytesRead != packetSize) return;
    const size_t responseLength = dns::buildResponse(
        request_, static_cast<size_t>(bytesRead), address_, response_,
        sizeof(response_));
    PORTAL_DEBUG_PORT.printf("portal dns request=%d response=%u\n", bytesRead,
                             static_cast<unsigned>(responseLength));
    if (responseLength == 0 || !udp_.beginPacket(remoteAddress, remotePort)) return;
    udp_.write(response_, responseLength);
    udp_.endPacket();
  }

 private:
  WiFiUDP udp_;
  uint8_t address_[4] = {};
  uint8_t request_[dns::kMaxRequestSize] = {};
  uint8_t response_[dns::kMaxResponseSize] = {};
};

CaptiveDnsServer* dnsServer = nullptr;
Status currentStatus = Status::Inactive;
uint32_t lastActivity = 0;
bool lanMode = false;
// Deferred radio power-off after stop(); see finishRadioOff().
bool radioOffPending = false;
uint32_t radioOffStartedMs = 0;
constexpr uint32_t kRadioOffSettleMs = 250;
constexpr uint32_t kRadioOffTimeoutMs = 2000;
bool switchToLanPending = false;
bool exitPending = false;
bool setupScanPending = false;
bool portalScanPending = false;
bool portalScanFailed = false;
WifiJoinState wifiJoinState = WifiJoinState::Idle;
uint32_t wifiJoinStarted = 0;
uint32_t switchToLanAt = 0;
char apSsid[studio::kPanelSetupSsidCapacity] = "";
char panelIdentity[studio::kPanelIdentityCapacity] = "";
char activeSsid[studio::kWifiSsidCapacity] = "";
char pendingWifiSsid[studio::kWifiSsidCapacity] = "";
char pendingWifiPassword[studio::kWifiPasswordCapacity] = "";
char wifiJoinMessage[64] = "Ready to connect";
char statusMessage[48] = "Portal off";
char portalUrl[32] = "http://192.168.4.1";
char qrPayloadValue[96] = "";
char portalNonce[17] = "";
WifiScanResult wifiScanResults[kWifiScanLimit] = {};
size_t wifiScanCount = 0;
uint8_t lastSetupClientCount = 0xff;

const char kStyle[] PROGMEM = R"HTML(<style>
:root{color-scheme:dark;--bg:#05070a;--panel:#12161d;--ink:#f3f4f6;--muted:#8a94a6;--cyan:#35c7f2}
*{box-sizing:border-box}body{margin:0;background:radial-gradient(circle at 20% 0,#12303b 0,transparent 38%),var(--bg);color:var(--ink);font:15px ui-monospace,SFMono-Regular,Menlo,monospace}main{max-width:760px;margin:auto;padding:32px 18px 80px}header{border-left:4px solid var(--cyan);padding-left:16px;margin-bottom:24px}h1{font-size:28px;letter-spacing:.08em;margin:0}header p,.hint{color:var(--muted)}section{background:var(--panel);border:1px solid #27313d;border-radius:14px;padding:18px;margin:14px 0;box-shadow:0 20px 50px #0008}h2{font-size:14px;color:var(--cyan);letter-spacing:.14em;text-transform:uppercase;margin:0 0 14px}label{display:block;color:var(--muted);font-size:12px;margin:12px 0 5px}input{width:100%;background:#080b10;border:1px solid #35404e;border-radius:8px;color:var(--ink);padding:11px;font:inherit}input:focus{outline:2px solid var(--cyan);border-color:transparent}.entity{display:grid;grid-template-columns:1fr 1fr;gap:8px;padding:10px 0;border-top:1px solid #252d38}.entity:first-of-type{border-top:0}button{background:var(--cyan);color:#021016;border:0;border-radius:9px;padding:12px 18px;font:700 14px inherit;cursor:pointer}.secondary{background:#27313d;color:var(--ink)}button:disabled{opacity:.5;cursor:wait}#results,#networks{display:grid;gap:6px;margin-top:10px}.result{background:#090d12;border:1px solid #27313d;border-radius:8px;padding:10px;text-align:left;color:var(--ink)}#feedback{margin-top:16px;padding:12px;border:1px solid #35404e;border-radius:8px;white-space:pre-wrap}.ok{border-color:#36d399!important;color:#75e8b7}.bad{border-color:#fb7185!important;color:#fda4af}small{color:var(--muted)}@media(max-width:520px){.entity{grid-template-columns:1fr}}
</style>)HTML";

void setStatus(Status value, const char* message) {
  currentStatus = value;
  std::strncpy(statusMessage, message, sizeof(statusMessage) - 1);
  statusMessage[sizeof(statusMessage) - 1] = '\0';
}

void touch() { lastActivity = millis(); }

bool requireStage(bool requireLan) {
  touch();
  if (lanMode == requireLan) return true;
  server->send(404, "text/plain", "Portal stage changed; use the address on the panel");
  return false;
}

void sendPage(const char* head, const char* body) {
  server->setContentLength(CONTENT_LENGTH_UNKNOWN);
  server->send(200, "text/html", "");
  server->sendContent_P(head);
  server->sendContent_P(kStyle);
  server->sendContent_P(body);
}

void sendPortalPage() {
  server->setContentLength(CONTENT_LENGTH_UNKNOWN);
  server->sendHeader("Cache-Control", "no-store");
  server->sendHeader("X-Frame-Options", "DENY");
  server->send(200, "text/html", "");
  server->sendContent_P(assets::kHead);
  server->sendContent_P(assets::kStyle);
  server->sendContent("<script>window.PORTAL_NONCE='");
  server->sendContent(portalNonce);
  server->sendContent("'</script>");
  server->sendContent_P(assets::kBody);
}

void sendCaptivePortalPage() {
  PORTAL_DEBUG_PORT.println("portal http captive-probe");
  touch();
  sendPortalPage();
}

void sendJson(int status, JsonDocument& doc) {
  String body;
  serializeJson(doc, body);
  server->send(status, "application/json", body);
}

void sendError(int status, const char* code, const char* message) {
  JsonDocument doc;
  doc["error"] = code;
  doc["message"] = message;
  sendJson(status, doc);
}

bool requireMutation() {
  touch();
  if (server->header("X-Portal-Nonce") == portalNonce) return true;
  sendError(403, "invalid_session", "Reload the Portal and try again");
  return false;
}

uint32_t hashBytes(uint32_t value, const void* bytes, size_t length) {
  const uint8_t* data = static_cast<const uint8_t*>(bytes);
  for (size_t i = 0; i < length; ++i) value = (value ^ data[i]) * 16777619u;
  return value;
}

uint32_t deviceRevision(const studio::DeviceRecord& record) {
  uint32_t value = 2166136261u;
  value = hashBytes(value, &record.instanceId, sizeof(record.instanceId));
  value = hashBytes(value, &record.driverId, sizeof(record.driverId));
  value = hashBytes(value, &record.enabled, sizeof(record.enabled));
  value = hashBytes(value, record.displayName, sizeof(record.displayName));
  value = hashBytes(value, record.homeAssistantEntityId,
                    sizeof(record.homeAssistantEntityId));
  return value;
}

uint32_t sceneRevision(const studio::SceneRecord& record) {
  uint32_t value = 2166136261u;
  value = hashBytes(value, &record.sceneId, sizeof(record.sceneId));
  value = hashBytes(value, &record.enabled, sizeof(record.enabled));
  value = hashBytes(value, record.name, sizeof(record.name));
  value = hashBytes(value, &record.startCount, sizeof(record.startCount));
  value = hashBytes(value, record.startSteps,
                    sizeof(studio::SceneStep) * record.startCount);
  value = hashBytes(value, &record.stopCount, sizeof(record.stopCount));
  value = hashBytes(value, &record.stopMode, sizeof(record.stopMode));
  return hashBytes(value, record.stopSteps,
                   sizeof(studio::SceneStep) * record.stopCount);
}

const char* driverName(studio::DriverId id) {
  const studio::DriverDescriptor* descriptor = studio::DriverCatalog::find(id);
  if (descriptor != nullptr) return descriptor->model;
  switch (id) {
    case studio::DriverId::SharkNanoII: return "Shark Nano II";
    case studio::DriverId::CanonBle: return "Canon (Smart)";
    case studio::DriverId::CanonTrigger: return "Canon (Trigger)";
    case studio::DriverId::TascamX8: return "Portacapture X8";
    case studio::DriverId::HomeAssistant: return "Home Assistant Entity";
    case studio::DriverId::AputureLight: return "Aputure Light";
    case studio::DriverId::ZhiyunLight: return "ZHIYUN Light";
    default: return "Unavailable driver";
  }
}

void addAction(JsonArray actions, studio::CommandType command,
               const char* id, const char* label,
               const studio::InstanceProfile& profile) {
  const uint32_t required = studio::requiredCapabilities(command);
  if (required == 0 || (profile.capabilities & required) != required) return;
  JsonObject action = actions.add<JsonObject>();
  action["id"] = id;
  action["label"] = label;
}

void serializeDevice(JsonObject out, const studio::DeviceRecord& record) {
  const studio::DriverDescriptor* descriptor =
      studio::DriverCatalog::find(record.driverId);
  out["id"] = record.instanceId;
  out["revision"] = deviceRevision(record);
  out["name"] = record.displayName;
  out["driver"] = driverName(record.driverId);
  out["driver_available"] = descriptor != nullptr;
  out["enabled"] = record.enabled;
  out["paired"] = record.paired;
  if (record.homeAssistantEntityId[0] != '\0') {
    out["entity_id"] = record.homeAssistantEntityId;
  }
  JsonArray actions = out["actions"].to<JsonArray>();
  if (descriptor == nullptr || !record.enabled) return;
  const studio::InstanceProfile profile = studio::devices().profile(record.instanceId);
  addAction(actions, studio::CommandType::RecordTrigger, "record_trigger",
            "Record trigger", profile);
  addAction(actions, studio::CommandType::RecordStart, "record_start",
            "Record start", profile);
  addAction(actions, studio::CommandType::RecordStop, "record_stop",
            "Record stop", profile);
  const uint32_t colorCapabilities =
      studio::capabilityBit(studio::Capability::SetLightCct) |
      studio::capabilityBit(studio::Capability::SetLightRgb);
  const bool hasColor = (profile.capabilities & colorCapabilities) != 0;
  if (!hasColor) {
    addAction(actions, studio::CommandType::TurnOn, "turn_on", "Turn on",
              profile);
  }
  addAction(actions, studio::CommandType::TurnOff, "turn_off", "Turn off", profile);
  addAction(actions, studio::CommandType::Press, "press", "Press", profile);
  addAction(actions, studio::CommandType::Activate, "activate", "Activate", profile);
  addAction(actions, studio::CommandType::SetLightCctAndOn,
            "set_light_cct_and_on", "Set look + On (CCT)", profile);
  addAction(actions, studio::CommandType::SetLightRgbAndOn,
            "set_light_rgb_and_on", "Set look + On (RGB)", profile);
}

const char* commandId(studio::CommandType command) {
  switch (command) {
    case studio::CommandType::RecordTrigger: return "record_trigger";
    case studio::CommandType::RecordStart: return "record_start";
    case studio::CommandType::RecordStop: return "record_stop";
    case studio::CommandType::TurnOn: return "turn_on";
    case studio::CommandType::TurnOff: return "turn_off";
    case studio::CommandType::Press: return "press";
    case studio::CommandType::Activate: return "activate";
    case studio::CommandType::SetLightCct: return "set_light_cct";
    case studio::CommandType::SetLightRgb: return "set_light_rgb";
    case studio::CommandType::SetLightCctAndOn: return "set_light_cct_and_on";
    case studio::CommandType::SetLightRgbAndOn: return "set_light_rgb_and_on";
    default: return "unsupported";
  }
}

const char* commandLabel(studio::CommandType command) {
  switch (command) {
    case studio::CommandType::RecordTrigger: return "Record trigger";
    case studio::CommandType::RecordStart: return "Record start";
    case studio::CommandType::RecordStop: return "Record stop";
    case studio::CommandType::TurnOn: return "Turn on";
    case studio::CommandType::TurnOff: return "Turn off";
    case studio::CommandType::Press: return "Press";
    case studio::CommandType::Activate: return "Activate";
    case studio::CommandType::SetLightCct: return "Set CCT";
    case studio::CommandType::SetLightRgb: return "Set RGB";
    case studio::CommandType::SetLightCctAndOn: return "Set look + On (CCT)";
    case studio::CommandType::SetLightRgbAndOn: return "Set look + On (RGB)";
    default: return "Unsupported";
  }
}

void serializeStep(JsonObject out, const studio::SceneStep& step) {
  if (step.type == studio::SceneStepType::Wait) {
    out["kind"] = "wait";
    out["wait_ms"] = step.waitMs;
    return;
  }
  out["kind"] = "action";
  out["target_id"] = step.targetId;
  out["command"] = commandId(step.command);
  out["label"] = commandLabel(step.command);
  out["value0"] = step.value0;
  out["value1"] = step.value1;
  out["value2"] = step.value2;
}

void serializeScene(JsonObject out, const studio::SceneRecord& record) {
  out["id"] = record.sceneId;
  out["revision"] = sceneRevision(record);
  out["name"] = record.name;
  out["enabled"] = record.enabled;
  out["stop_mode"] = record.stopMode == studio::SceneStopMode::Generated
      ? "generated" : "custom";
  JsonArray start = out["start"].to<JsonArray>();
  for (uint8_t i = 0; i < record.startCount; ++i) {
    serializeStep(start.add<JsonObject>(), record.startSteps[i]);
  }
  JsonArray stop = out["stop"].to<JsonArray>();
  for (uint8_t i = 0; i < record.stopCount; ++i) {
    serializeStep(stop.add<JsonObject>(), record.stopSteps[i]);
  }
}

bool parseIdPath(const String& uri, const char* prefix, uint32_t& id) {
  if (!uri.startsWith(prefix)) return false;
  const String value = uri.substring(std::strlen(prefix));
  if (value.length() == 0) return false;
  for (size_t i = 0; i < value.length(); ++i) {
    if (value[i] < '0' || value[i] > '9') return false;
  }
  id = static_cast<uint32_t>(value.toInt());
  return id != 0;
}

bool parseBody(JsonDocument& doc) {
  if (!server->hasArg("plain") || server->arg("plain").length() > 4096) {
    sendError(413, "request_too_large", "Request body is missing or too large");
    return false;
  }
  if (deserializeJson(doc, server->arg("plain")) != DeserializationError::Ok) {
    sendError(400, "invalid_json", "Request body is not valid JSON");
    return false;
  }
  return true;
}

const char* validationMessage(studio::SceneValidationStatus status) {
  switch (status) {
    case studio::SceneValidationStatus::Empty: return "Add at least one Start or Stop step";
    case studio::SceneValidationStatus::InvalidName: return "Sequence name is required";
    case studio::SceneValidationStatus::Full: return "This sequence has too many steps";
    case studio::SceneValidationStatus::MissingTarget: return "A target device is missing";
    case studio::SceneValidationStatus::DisabledTarget: return "A target device is disabled";
    case studio::SceneValidationStatus::UnsupportedCommand: return "A step uses an unsupported action";
    case studio::SceneValidationStatus::MissingCapability: return "A device does not support the selected action";
    case studio::SceneValidationStatus::WaitOutOfRange: return "Wait must be between 0 and 60000 ms";
    case studio::SceneValidationStatus::TooManyTargets: return "The sequence uses too many devices";
    case studio::SceneValidationStatus::Busy: return "The sequence is currently busy";
    default: return "Sequence validation failed";
  }
}

bool connectStation(const char* ssid, const char* password, bool keepAp) {
  if (ssid == nullptr || ssid[0] == '\0') return false;
  WiFi.mode(keepAp ? WIFI_AP_STA : WIFI_STA);
  WiFi.begin(ssid, password != nullptr ? password : "");
  const uint32_t started = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - started < 10000) {
    delay(10);
  }
  return WiFi.status() == WL_CONNECTED;
}

bool authHttp(HTTPClient& http, const String& url, const char* token) {
  http.setConnectTimeout(2000);
  http.setTimeout(3000);
  if (!http.begin(url)) return false;
  String authorization = "Bearer ";
  authorization += token;
  http.addHeader("Authorization", authorization);
  return true;
}

int validateApi(const String& baseUrl, const char* token) {
  String endpoint = baseUrl;
  if (!endpoint.endsWith("/")) endpoint += "/";
  endpoint += "api/";
  HTTPClient http;
  if (!authHttp(http, endpoint, token)) return -1;
  const int status = http.GET();
  http.end();
  return status;
}

void handleWifiSave() {
  if (!requireStage(false)) return;
  if (!requireMutation()) return;
  const String ssid = server->arg("ssid");
  const String password = server->arg("password");
  if (ssid.length() == 0 || ssid.length() >= sizeof(pendingWifiSsid) ||
      password.length() >= sizeof(pendingWifiPassword)) {
    server->send(400, "application/json",
                 "{\"error\":\"Invalid Wi-Fi name or password length\"}");
    return;
  }
  if (wifiJoinState == WifiJoinState::Connecting) {
    server->send(409, "application/json",
                 "{\"error\":\"A Wi-Fi connection is already in progress\"}");
    return;
  }

  // Abandon any in-flight AP-mode scan: the join owns the radio now, and the
  // loop's scan-completion branch must not overwrite the join status.
  wifi_scan::cancel();
  portalScanPending = false;
  portalScanFailed = false;
  std::strncpy(pendingWifiSsid, ssid.c_str(), sizeof(pendingWifiSsid) - 1);
  pendingWifiSsid[sizeof(pendingWifiSsid) - 1] = '\0';
  std::strncpy(pendingWifiPassword, password.c_str(),
               sizeof(pendingWifiPassword) - 1);
  pendingWifiPassword[sizeof(pendingWifiPassword) - 1] = '\0';
  wifiJoinState = WifiJoinState::Connecting;
  wifiJoinStarted = millis();
  switchToLanPending = false;
  setStatus(Status::Testing, "Joining studio Wi-Fi");
  std::snprintf(wifiJoinMessage, sizeof(wifiJoinMessage), "Connecting to %s…",
                pendingWifiSsid);
  WiFi.mode(WIFI_AP_STA);
  WiFi.begin(pendingWifiSsid, pendingWifiPassword);
  server->send(202, "application/json",
               "{\"state\":\"connecting\",\"message\":\"Connecting to Wi-Fi…\"}");
}

bool saveConnectedWifi() {
  studio::PreferencesHomeAssistantBackend backend;
  studio::HomeAssistantConfigStore store(backend);
  studio::HomeAssistantConfig config;
  store.load(config);
  std::strncpy(config.wifiSsid, pendingWifiSsid, sizeof(config.wifiSsid) - 1);
  std::strncpy(config.wifiPassword, pendingWifiPassword,
               sizeof(config.wifiPassword) - 1);
  config.wifiConfigured = true;
  if (!store.save(config)) {
    return false;
  }
  std::strncpy(activeSsid, config.wifiSsid, sizeof(activeSsid) - 1);
  std::snprintf(portalUrl, sizeof(portalUrl), "http://%s",
                WiFi.localIP().toString().c_str());
  return true;
}

void sendWifiStatus() {
  if (!requireStage(false)) return;
  JsonDocument doc;
  switch (wifiJoinState) {
    case WifiJoinState::Connecting: doc["state"] = "connecting"; break;
    case WifiJoinState::Connected: doc["state"] = "connected"; break;
    case WifiJoinState::Failed: doc["state"] = "failed"; break;
    default: doc["state"] = "idle"; break;
  }
  doc["message"] = wifiJoinMessage;
  if (wifiJoinState == WifiJoinState::Connected) {
    doc["ssid"] = activeSsid;
    doc["url"] = portalUrl;
    doc["alias"] = "http://bleep.local";
    const uint32_t acknowledgedHandoff = millis() + 2500;
    if (switchToLanAt == 0 || acknowledgedHandoff < switchToLanAt) {
      switchToLanAt = acknowledgedHandoff;
    }
  }
  String body;
  serializeJson(doc, body);
  server->send(200, "application/json", body);
}

void cacheWifiScanResults(int found) {
  wifiScanCount = 0;
  if (found > 0) {
    for (int i = 0; i < found && wifiScanCount < kWifiScanLimit; ++i) {
      const String candidate = WiFi.SSID(i);
      if (candidate.length() == 0) continue;
      bool duplicate = false;
      for (size_t existing = 0; existing < wifiScanCount; ++existing) {
        duplicate |= candidate == wifiScanResults[existing].ssid;
      }
      if (duplicate) continue;
      WifiScanResult& result = wifiScanResults[wifiScanCount++];
      std::strncpy(result.ssid, candidate.c_str(), sizeof(result.ssid) - 1);
      result.rssi = WiFi.RSSI(i);
      result.secure = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
    }
  }
  WiFi.scanDelete();
  PORTAL_DEBUG_PORT.printf("portal wifi scan found=%d cached=%u\n", found,
                           static_cast<unsigned>(wifiScanCount));
}

void startWifiScan() {
  if (!requireStage(false)) return;
  if (!requireMutation()) return;
  if (wifiJoinState == WifiJoinState::Connecting) {
    server->send(409, "application/json",
                 "{\"error\":\"Wait for the Wi-Fi connection attempt to finish\"}");
    return;
  }
  if (portalScanPending) {
    server->send(202, "application/json", "{\"state\":\"scanning\"}");
    return;
  }
  if (!wifi_scan::start()) {
    server->send(500, "application/json",
                 "{\"error\":\"Could not start Wi-Fi scan\"}");
    return;
  }
  portalScanPending = true;
  portalScanFailed = false;
  setStatus(Status::Testing, "Scanning nearby Wi-Fi");
  PORTAL_DEBUG_PORT.println("portal wifi scan started");
  server->send(202, "application/json", "{\"state\":\"scanning\"}");
}

void sendWifiScan() {
  if (!requireStage(false)) return;
  JsonDocument doc;
  if (portalScanPending) {
    doc["state"] = "scanning";
    sendJson(200, doc);
    return;
  }
  if (portalScanFailed) {
    doc["state"] = "failed";
    doc["error"] = "Wi-Fi scan failed. Enter the SSID manually.";
    sendJson(200, doc);
    return;
  }
  JsonArray networks = doc["networks"].to<JsonArray>();
  doc["state"] = "complete";
  for (size_t i = 0; i < wifiScanCount; ++i) {
    JsonObject item = networks.add<JsonObject>();
    item["ssid"] = wifiScanResults[i].ssid;
    item["rssi"] = wifiScanResults[i].rssi;
    item["secure"] = wifiScanResults[i].secure;
  }
  if (wifiScanCount == 0) doc["error"] = "No networks found. Enter the SSID manually.";
  String body;
  serializeJson(doc, body);
  server->send(200, "application/json", body);
}

void handleEntities() {
  if (!requireStage(true)) return;
  if (!requireMutation()) return;
  setStatus(Status::Testing, "Querying Home Assistant");
  const String baseUrl = server->arg("url");
  String token = server->arg("token");
  const String query = server->arg("q");
  if (token.length() == 0) {
    studio::PreferencesHomeAssistantBackend backend;
    studio::HomeAssistantConfigStore store(backend);
    studio::HomeAssistantConfig config;
    store.load(config);
    token = config.token;
  }
  JsonDocument response;
  JsonArray results = response["entities"].to<JsonArray>();
  if (WiFi.status() != WL_CONNECTED ||
      !studio::validLocalHomeAssistantUrl(baseUrl.c_str()) || token.length() == 0) {
    response["error"] = "Wi-Fi, local URL, or token is invalid";
  } else if (validateApi(baseUrl, token.c_str()) != 200) {
    response["error"] = "Home Assistant API or token rejected";
  } else {
    HTTPClient http;
    String endpoint = baseUrl;
    if (endpoint.endsWith("/")) endpoint.remove(endpoint.length() - 1);
    endpoint += "/api/states";
    if (!authHttp(http, endpoint, token.c_str())) {
      response["error"] = "Could not open Home Assistant";
    } else {
      const int code = http.GET();
      if (code != 200) {
        response["error"] = code == 401 ? "Token rejected" : "Home Assistant unavailable";
      } else {
        Stream* stream = http.getStreamPtr();
        while (http.connected() || stream->available()) {
          const int next = stream->peek();
          if (next < 0) { delay(1); continue; }
          if (next == ']') { stream->read(); break; }
          if (next != '{') { stream->read(); continue; }
          JsonDocument entity;
          if (deserializeJson(entity, *stream) != DeserializationError::Ok) break;
          const char* entityId = entity["entity_id"] | "";
          const char* friendly = entity["attributes"]["friendly_name"] | entityId;
          if (!home_assistant::supportedEntityId(entityId) ||
              !home_assistant::matchesEntitySearch(entityId, friendly,
                                                   query.c_str())) {
            continue;
          }
          JsonObject item = results.add<JsonObject>();
          item["entity_id"] = entityId;
          item["name"] = friendly;
          item["state"] = entity["state"] | "unknown";
          if (results.size() >= kDiscoveryLimit) break;
        }
      }
      http.end();
    }
  }
  String body;
  serializeJson(response, body);
  server->send(200, "application/json", body);
  setStatus(Status::Ready, "LAN portal ready");
}

void handleConfig() {
  touch();
  studio::PreferencesHomeAssistantBackend backend;
  studio::HomeAssistantConfigStore store(backend);
  studio::HomeAssistantConfig config;
  store.load(config);
  JsonDocument doc;
  doc["url"] = config.baseUrl[0] != '\0'
      ? config.baseUrl : "http://homeassistant.local:8123";
  doc["token_stored"] = config.token[0] != '\0';
  JsonArray entities = doc["entities"].to<JsonArray>();
  for (size_t i = 0; i < studio::devices().count(); ++i) {
    const studio::DeviceRecord* record = studio::devices().at(i);
    if (record == nullptr || record->driverId != studio::DriverId::HomeAssistant) continue;
    JsonObject entity = entities.add<JsonObject>();
    entity["instance_id"] = record->instanceId;
    entity["entity_id"] = record->homeAssistantEntityId;
    entity["name"] = record->displayName;
  }
  String body;
  serializeJson(doc, body);
  server->send(200, "application/json", body);
}

void handleSave() {
  if (!requireStage(true)) return;
  if (!requireMutation()) return;
  studio::PreferencesHomeAssistantBackend backend;
  studio::HomeAssistantConfigStore store(backend);
  studio::HomeAssistantConfig previous;
  if (store.load(previous) == studio::ConfigLoadStatus::Corrupt ||
      !previous.wifiConfigured || previous.wifiSsid[0] == '\0') {
    server->send(409, "text/plain", "Wi-Fi must be configured first");
    return;
  }
  studio::HomeAssistantConfig config = previous;
  config.homeAssistantConfigured = true;
  std::strncpy(config.baseUrl, server->arg("url").c_str(),
               sizeof(config.baseUrl) - 1);
  const String submittedToken = server->arg("token");
  if (submittedToken.length() > 0) {
    std::strncpy(config.token, submittedToken.c_str(), sizeof(config.token) - 1);
  }
  if (!studio::validLocalHomeAssistantUrl(config.baseUrl) || config.token[0] == '\0') {
    server->send(400, "text/plain", "Invalid local Home Assistant configuration");
    return;
  }
  const int apiStatus = validateApi(config.baseUrl, config.token);
  if (apiStatus != 200) {
    server->send(apiStatus == 401 ? 401 : 502, "text/plain",
                 apiStatus == 401 ? "Home Assistant token rejected"
                                  : "Home Assistant API unavailable");
    return;
  }

  studio::HomeAssistantEntitySelection selections[4] = {};
  size_t count = 0;
  for (size_t i = 0; i < 4; ++i) {
    const String id = server->arg(String("id") + i);
    if (id.length() == 0) continue;
    const studio::HomeAssistantDomain domain =
        home_assistant::domainFromEntityId(id.c_str());
    if (domain == studio::HomeAssistantDomain::None) {
      server->send(400, "text/plain", "Unsupported entity domain");
      return;
    }
    const String instance = server->arg(String("instance") + i);
    selections[count].instanceId = instance.length() > 0
        ? static_cast<studio::InstanceId>(instance.toInt())
        : studio::kInvalidInstanceId;
    selections[count].domain = domain;
    std::strncpy(selections[count].entityId, id.c_str(),
                 sizeof(selections[count].entityId) - 1);
    const String name = server->arg(String("name") + i);
    std::strncpy(selections[count].displayName,
                 name.length() > 0 ? name.c_str() : id.c_str(),
                 sizeof(selections[count].displayName) - 1);
    ++count;
  }
  for (size_t i = 0; i < studio::devices().count(); ++i) {
    const studio::DeviceRecord* record = studio::devices().at(i);
    if (record == nullptr || record->driverId != studio::DriverId::HomeAssistant) continue;
    bool retained = false;
    for (size_t s = 0; s < count; ++s) retained |= selections[s].instanceId == record->instanceId;
    if (!retained && studio::scenes().referencesInstance(record->instanceId)) {
      server->send(409, "text/plain", "Remove this entity from Ble(e)p scenes first");
      return;
    }
  }
  if (!store.save(config)) {
    server->send(500, "text/plain", "Could not store Home Assistant secrets");
    return;
  }
  const studio::RegistryStatus replaced =
      studio::devices().replaceHomeAssistantEntities(selections, count);
  if (replaced != studio::RegistryStatus::Ok) {
    store.save(previous);
    server->send(500, "text/plain", "Could not store entity selection");
    return;
  }
  setStatus(Status::Saved, "Saved - exit Portal");
  JsonDocument response;
  response["saved"] = true;
  sendJson(200, response);
}

void handleSummary() {
  touch();
  JsonDocument doc;
  doc["devices"] = studio::devices().count();
  doc["device_capacity"] = CONFIG_MAX_DEVICE_INSTANCES;
  doc["sequences"] = studio::scenes().count();
  doc["panel_id"] = panelIdentity;
  doc["lan"] = lanMode;
  doc["network"] = activeSsid;
  doc["timeout_seconds"] =
      lastActivity > millis() ? 0 : (kTimeoutMs - (millis() - lastActivity)) / 1000;
  sendJson(200, doc);
}

void handleDevices() {
  touch();
  JsonDocument doc;
  JsonArray devices = doc["devices"].to<JsonArray>();
  for (size_t i = 0; i < studio::devices().count(); ++i) {
    const studio::DeviceRecord* record = studio::devices().at(i);
    if (record != nullptr) serializeDevice(devices.add<JsonObject>(), *record);
  }
  sendJson(200, doc);
}

void handleDeviceMutation(studio::InstanceId instanceId) {
  if (!requireMutation()) return;
  const studio::DeviceRecord* current = studio::devices().find(instanceId);
  if (current == nullptr) {
    sendError(404, "device_not_found", "Device no longer exists");
    return;
  }
  JsonDocument request;
  if (!parseBody(request)) return;
  if ((request["revision"] | 0u) != deviceRevision(*current)) {
    sendError(409, "stale_revision", "Device changed; reload and try again");
    return;
  }
  if (server->method() == HTTP_PATCH) {
    const char* name = request["name"] | "";
    const bool enabled = request["enabled"] | current->enabled;
    if (name[0] == '\0' || std::strlen(name) >= studio::kDeviceNameCapacity) {
      sendError(422, "invalid_name", "Device name must be 1 to 31 characters");
      return;
    }
    if (studio::devices().update(instanceId, name, enabled) !=
        studio::RegistryStatus::Ok) {
      sendError(500, "save_failed", "Device could not be saved");
      return;
    }
    JsonDocument response;
    serializeDevice(response.to<JsonObject>(), *studio::devices().find(instanceId));
    sendJson(200, response);
    return;
  }
  if (server->method() != HTTP_DELETE) {
    sendError(405, "method_not_allowed", "Unsupported device operation");
    return;
  }
  JsonDocument conflict;
  JsonArray references = conflict["sequences"].to<JsonArray>();
  for (size_t i = 0; i < studio::scenes().count(); ++i) {
    const studio::SceneRecord* scene = studio::scenes().at(i);
    if (scene == nullptr) continue;
    bool referenced = false;
    const studio::SceneStep* lists[] = {scene->startSteps, scene->stopSteps};
    const uint8_t counts[] = {scene->startCount, scene->stopCount};
    for (size_t list = 0; list < 2 && !referenced; ++list) {
      for (uint8_t step = 0; step < counts[list]; ++step) {
        referenced = lists[list][step].type == studio::SceneStepType::Action &&
                     lists[list][step].targetId == instanceId;
        if (referenced) break;
      }
    }
    if (referenced) {
      JsonObject item = references.add<JsonObject>();
      item["id"] = scene->sceneId;
      item["name"] = scene->name;
    }
  }
  if (!references.isNull() && references.size() > 0) {
    conflict["error"] = "device_referenced";
    conflict["message"] = "Remove this device from its sequences first";
    sendJson(409, conflict);
    return;
  }
  if (studio::devices().remove(instanceId) != studio::RegistryStatus::Ok) {
    sendError(500, "remove_failed", "Device could not be removed");
    return;
  }
  JsonDocument response;
  response["removed"] = true;
  sendJson(200, response);
}

void handleSequences() {
  touch();
  JsonDocument doc;
  JsonArray sequences = doc["sequences"].to<JsonArray>();
  for (size_t i = 0; i < studio::scenes().count(); ++i) {
    const studio::SceneRecord* record = studio::scenes().at(i);
    if (record != nullptr) serializeScene(sequences.add<JsonObject>(), *record);
  }
  sendJson(200, doc);
}

void handleCreateSequence() {
  if (!requireMutation()) return;
  JsonDocument request;
  if (!parseBody(request)) return;
  const char* name = request["name"] | "";
  if (name[0] == '\0' || std::strlen(name) >= studio::kDeviceNameCapacity) {
    sendError(422, "invalid_name", "Sequence name must be 1 to 31 characters");
    return;
  }
  studio::SceneId id = studio::kInvalidSceneId;
  const studio::SceneId sourceId = request["source_id"] | studio::kInvalidSceneId;
  const studio::SceneRegistryStatus status = sourceId == studio::kInvalidSceneId
      ? studio::scenes().add(name, id)
      : studio::scenes().duplicate(sourceId, name, id);
  if (status != studio::SceneRegistryStatus::Ok) {
    sendError(507, "save_failed",
              "Sequence could not be stored; free panel storage and try again");
    return;
  }
  JsonDocument response;
  serializeScene(response.to<JsonObject>(), *studio::scenes().find(id));
  sendJson(201, response);
}

void sendStepError(const char* list, const StepParseResult& result) {
  char message[96];
  std::snprintf(message, sizeof(message), "%s step %u %s", list,
                static_cast<unsigned>(result.index + 1),
                stepParseMessage(result.status));
  sendError(422, "invalid_step", message);
}

void handleSequenceMutation(studio::SceneId sceneId) {
  if (server->method() == HTTP_GET) {
    touch();
    const studio::SceneRecord* record = studio::scenes().find(sceneId);
    if (record == nullptr) {
      sendError(404, "sequence_not_found", "Sequence no longer exists");
      return;
    }
    JsonDocument response;
    serializeScene(response.to<JsonObject>(), *record);
    sendJson(200, response);
    return;
  }
  if (!requireMutation()) return;
  const studio::SceneRecord* current = studio::scenes().find(sceneId);
  if (current == nullptr) {
    sendError(404, "sequence_not_found", "Sequence no longer exists");
    return;
  }
  JsonDocument request;
  if (!parseBody(request)) return;
  if ((request["revision"] | 0u) != sceneRevision(*current)) {
    sendError(409, "stale_revision", "Sequence changed; reload and try again");
    return;
  }
  if (server->method() == HTTP_DELETE) {
    if (studio::scenes().remove(sceneId) != studio::SceneRegistryStatus::Ok) {
      sendError(500, "remove_failed", "Sequence could not be removed");
      return;
    }
    JsonDocument response;
    response["removed"] = true;
    sendJson(200, response);
    return;
  }
  if (server->method() != HTTP_PUT) {
    sendError(405, "method_not_allowed", "Unsupported sequence operation");
    return;
  }
  studio::SceneRecord updated = *current;
  const char* name = request["name"] | "";
  if (name[0] == '\0' || std::strlen(name) >= sizeof(updated.name)) {
    sendError(422, "invalid_name", "Sequence name must be 1 to 31 characters");
    return;
  }
  std::strncpy(updated.name, name, sizeof(updated.name) - 1);
  updated.name[sizeof(updated.name) - 1] = '\0';
  updated.enabled = request["enabled"] | updated.enabled;
  const StepParseResult startResult = parseSceneSteps(
      request["start"], updated.startSteps, updated.startCount);
  if (startResult.status != StepParseStatus::Ok) {
    sendStepError("Start", startResult);
    return;
  }
  const char* stopMode = request["stop_mode"] | "generated";
  if (std::strcmp(stopMode, "generated") == 0) {
    updated.stopMode = studio::SceneStopMode::Generated;
    studio::generateStopSteps(updated);
  } else if (std::strcmp(stopMode, "custom") == 0) {
    updated.stopMode = studio::SceneStopMode::Custom;
    const StepParseResult stopResult = parseSceneSteps(
        request["stop"], updated.stopSteps, updated.stopCount);
    if (stopResult.status != StepParseStatus::Ok) {
      sendStepError("Stop", stopResult);
      return;
    }
  } else {
    sendError(422, "invalid_stop_mode", "Stop mode must be generated or custom");
    return;
  }
  const studio::SceneValidationStatus validation = studio::scenes().validate(updated);
  if (validation != studio::SceneValidationStatus::Ok) {
    sendError(422, "validation_failed", validationMessage(validation));
    return;
  }
  if (studio::scenes().replace(updated) != studio::SceneRegistryStatus::Ok) {
    sendError(500, "save_failed", "Sequence could not be saved");
    return;
  }
  JsonDocument response;
  serializeScene(response.to<JsonObject>(), *studio::scenes().find(sceneId));
  sendJson(200, response);
}

void handleExit() {
  if (!requireMutation()) return;
  JsonDocument response;
  response["closing"] = true;
  sendJson(200, response);
  exitPending = true;
}

void installHandlers() {
  server->on("/", HTTP_GET, [] {
    PORTAL_DEBUG_PORT.println("portal http root");
    touch();
    sendPortalPage();
  });
  if (!lanMode) {
    // Serve a non-empty 200 response for the probes used by the major phone
    // platforms. A relative redirect is not consistently treated as a captive
    // result and can leave the sign-on assistant unopened.
    server->on("/generate_204", HTTP_GET, sendCaptivePortalPage);
    server->on("/gen_204", HTTP_GET, sendCaptivePortalPage);
    server->on("/hotspot-detect.html", HTTP_GET, sendCaptivePortalPage);
    server->on("/library/test/success.html", HTTP_GET, sendCaptivePortalPage);
    server->on("/connecttest.txt", HTTP_GET, sendCaptivePortalPage);
    server->on("/ncsi.txt", HTTP_GET, sendCaptivePortalPage);
  }
  server->on("/wifi", HTTP_POST, handleWifiSave);
  server->on("/api/wifi/status", HTTP_GET, sendWifiStatus);
  server->on("/api/wifi/scan", HTTP_POST, startWifiScan);
  server->on("/api/wifi/scan", HTTP_GET, sendWifiScan);
  server->on("/api/config", HTTP_GET, handleConfig);
  server->on("/api/entities", HTTP_POST, handleEntities);
  server->on("/save", HTTP_POST, handleSave);
  server->on("/api/summary", HTTP_GET, handleSummary);
  server->on("/api/devices", HTTP_GET, handleDevices);
  server->on("/api/sequences", HTTP_GET, handleSequences);
  server->on("/api/sequences", HTTP_POST, handleCreateSequence);
  server->on("/api/exit", HTTP_POST, handleExit);
  server->on("/assets/bleep-logo.webp", HTTP_GET, [] {
    touch();
    server->sendHeader("Cache-Control", "private, max-age=600");
    server->send_P(200, PSTR("image/webp"),
                   reinterpret_cast<PGM_P>(assets::kPortalLogoWebp),
                   assets::kPortalLogoWebpSize);
  });
  server->onNotFound([] {
    uint32_t id = 0;
    if (parseIdPath(server->uri(), "/api/devices/", id)) {
      handleDeviceMutation(static_cast<studio::InstanceId>(id));
      return;
    }
    if (parseIdPath(server->uri(), "/api/sequences/", id)) {
      handleSequenceMutation(static_cast<studio::SceneId>(id));
      return;
    }
    touch();
    if (!lanMode && server->method() == HTTP_GET) {
      PORTAL_DEBUG_PORT.println("portal http unknown-get");
      sendPortalPage();
      return;
    }
    server->sendHeader("Location", "/", true);
    server->send(302);
  });
}

bool startServer(IPAddress address) {
  // Binding to the AP address immediately after mode creation can fail before
  // lwIP publishes that interface. WebServer does not surface bind failure, so
  // listen on all Portal-owned interfaces and keep the address only for UX.
  server = new (std::nothrow) WebServer(80);
  if (server == nullptr) return false;
  const char* headers[] = {"X-Portal-Nonce"};
  server->collectHeaders(headers, 1);
  installHandlers();
  server->begin();
  PORTAL_DEBUG_PORT.printf("portal http listening=%s:80\n",
                           address.toString().c_str());
  return true;
}

void destroyServer() {
  if (dnsServer != nullptr) {
    dnsServer->stop();
    delete dnsServer;
    dnsServer = nullptr;
  }
  if (server == nullptr) return;
  server->stop();
  delete server;
  server = nullptr;
}

bool startCaptiveDns(IPAddress address) {
  dnsServer = new (std::nothrow) CaptiveDnsServer();
  if (dnsServer == nullptr) return false;
  if (dnsServer->start(53, address)) return true;
  delete dnsServer;
  dnsServer = nullptr;
  return false;
}

bool startSetupAp() {
  MDNS.end();
  lanMode = false;
  switchToLanPending = false;
  switchToLanAt = 0;
  wifiJoinState = WifiJoinState::Idle;
  std::strncpy(wifiJoinMessage, "Ready to connect", sizeof(wifiJoinMessage) - 1);
  if (!WiFi.mode(WIFI_AP_STA)) return false;
  delay(50);
  const IPAddress setupAddress(192, 168, 4, 1);
  const IPAddress setupMask(255, 255, 255, 0);
  if (!WiFi.softAPConfig(setupAddress, setupAddress, setupMask)) return false;
  if (!WiFi.softAP(apSsid)) return false;
  std::strncpy(activeSsid, apSsid, sizeof(activeSsid) - 1);
  std::strncpy(portalUrl, "http://192.168.4.1", sizeof(portalUrl) - 1);
  const IPAddress activeAddress = WiFi.softAPIP();
  if (!startServer(activeAddress) || !startCaptiveDns(activeAddress)) {
    destroyServer();
    WiFi.softAPdisconnect(true);
    return false;
  }
  lastSetupClientCount = 0xff;
  PORTAL_DEBUG_PORT.printf("portal ap ready ip=%s\n",
                           activeAddress.toString().c_str());
  setStatus(Status::Ready, "AP Portal ready");
  return true;
}

bool beginSetupScan() {
  setupScanPending = false;
  wifiScanCount = 0;
  WiFi.mode(WIFI_STA);
  if (!wifi_scan::start()) return false;
  setupScanPending = true;
  setStatus(Status::Starting, "Scanning studio Wi-Fi");
  return true;
}

void finishSetupScan() {
  const int found = wifi_scan::complete();
  if (found == WIFI_SCAN_RUNNING) return;
  cacheWifiScanResults(found);
  setupScanPending = false;
  if (!startSetupAp()) {
    WiFi.mode(WIFI_OFF);
    setStatus(Status::Error, "Could not start Portal");
  }
}

bool startLanPortal() {
  lanMode = true;
  switchToLanPending = false;
  switchToLanAt = 0;
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  MDNS.end();
  if (MDNS.begin("bleep")) {
    MDNS.addService("http", "tcp", 80);
  }
  std::snprintf(portalUrl, sizeof(portalUrl), "http://%s",
                WiFi.localIP().toString().c_str());
  if (!startServer(WiFi.localIP())) {
    MDNS.end();
    return false;
  }
  setStatus(Status::Ready, "LAN portal ready");
  return true;
}

}  // namespace

bool begin() {
  if (currentStatus == Status::Error) stop();
  if (currentStatus != Status::Inactive) return true;
  studio::scenes().cancel();
  studio::devices().deactivateAll();
  exitPending = false;
  std::snprintf(portalNonce, sizeof(portalNonce), "%08lX%08lX",
                static_cast<unsigned long>(esp_random()),
                static_cast<unsigned long>(esp_random()));
  const uint64_t chip =
      studio::canonicalPanelHardwareId(ESP.getEfuseMac());
  unitId();
  studio::formatPanelSetupSsid(chip, apSsid);

  studio::PreferencesHomeAssistantBackend backend;
  studio::HomeAssistantConfigStore store(backend);
  studio::HomeAssistantConfig config;
  const studio::ConfigLoadStatus loaded = store.load(config);
  if (loaded != studio::ConfigLoadStatus::Corrupt && config.wifiConfigured &&
      config.wifiSsid[0] != '\0') {
    setStatus(Status::Starting, "Joining studio Wi-Fi");
    if (connectStation(config.wifiSsid, config.wifiPassword, false)) {
      std::strncpy(activeSsid, config.wifiSsid, sizeof(activeSsid) - 1);
      if (startLanPortal()) {
        lastActivity = millis();
        return true;
      }
    }
  }

  WiFi.disconnect(true, false);
  if (!beginSetupScan() && !startSetupAp()) {
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    setStatus(Status::Error, "Could not start Portal");
    return false;
  }
  lastActivity = millis();
  return true;
}

void finishRadioOff(uint32_t now) {
  if (!radioOffPending || now - radioOffStartedMs < kRadioOffSettleMs) return;
  if (WiFi.status() == WL_CONNECTED &&
      now - radioOffStartedMs < kRadioOffTimeoutMs) {
    return;
  }
  WiFi.mode(WIFI_OFF);
  radioOffPending = false;
}

void loop() {
  finishRadioOff(millis());
  if (currentStatus == Status::Inactive || currentStatus == Status::Error) return;
  if (setupScanPending) {
    finishSetupScan();
    return;
  }
  if (dnsServer != nullptr) dnsServer->processNextRequest();
  if (server != nullptr) server->handleClient();
  if (portalScanPending) {
    const int found = wifi_scan::complete();
    if (found != WIFI_SCAN_RUNNING) {
      cacheWifiScanResults(found);
      portalScanPending = false;
      portalScanFailed = found < 0;
      setStatus(Status::Ready,
                portalScanFailed ? "Wi-Fi scan failed" : "AP Portal ready");
    }
  }
  if (!lanMode) {
    const uint8_t clientCount = WiFi.softAPgetStationNum();
    if (clientCount != lastSetupClientCount) {
      lastSetupClientCount = clientCount;
      PORTAL_DEBUG_PORT.printf("portal ap clients=%u\n",
                               static_cast<unsigned>(clientCount));
    }
  }
  if (exitPending) {
    stop();
    return;
  }
  if (!lanMode && wifiJoinState == WifiJoinState::Connecting) {
    const wl_status_t status = WiFi.status();
    if (status == WL_CONNECTED) {
      if (saveConnectedWifi()) {
        wifiJoinState = WifiJoinState::Connected;
        switchToLanPending = true;
        switchToLanAt = millis() + kLanHandoffDelayMs;
        std::snprintf(wifiJoinMessage, sizeof(wifiJoinMessage),
                      "Connected. LAN address: %s", portalUrl);
        setStatus(Status::Starting, "Wi-Fi connected - note address");
      } else {
        wifiJoinState = WifiJoinState::Failed;
        std::strncpy(wifiJoinMessage, "Connected, but credentials could not be saved",
                     sizeof(wifiJoinMessage) - 1);
        setStatus(Status::Ready, "Wi-Fi save failed - retry");
        WiFi.disconnect(false, false);
      }
    } else if (status == WL_NO_SSID_AVAIL || status == WL_CONNECT_FAILED ||
               millis() - wifiJoinStarted >= kWifiConnectTimeoutMs) {
      wifiJoinState = WifiJoinState::Failed;
      const char* reason = status == WL_NO_SSID_AVAIL
          ? "Network not found. Scan again or check the SSID."
          : status == WL_CONNECT_FAILED
              ? "Connection rejected. Check the Wi-Fi password."
              : "Connection timed out. Move closer or check the password.";
      std::strncpy(wifiJoinMessage, reason, sizeof(wifiJoinMessage) - 1);
      wifiJoinMessage[sizeof(wifiJoinMessage) - 1] = '\0';
      setStatus(Status::Ready, "Wi-Fi failed - retry");
      WiFi.disconnect(false, false);
    }
  }
  if (switchToLanPending && switchToLanAt != 0 &&
      static_cast<int32_t>(millis() - switchToLanAt) >= 0) {
    destroyServer();
    if (!startLanPortal()) {
      WiFi.disconnect(true, false);
      WiFi.mode(WIFI_OFF);
      setStatus(Status::Error, "LAN Portal failed");
      return;
    }
  }
  if (lanMode && WiFi.status() != WL_CONNECTED) {
    destroyServer();
    WiFi.disconnect(true, false);
    if (!startSetupAp()) {
      WiFi.softAPdisconnect(true);
      WiFi.mode(WIFI_OFF);
      setStatus(Status::Error, "Wi-Fi lost");
      return;
    }
  }
  if (millis() - lastActivity >= kTimeoutMs) stop();
}

void stop() {
  if (currentStatus == Status::Inactive) return;
  destroyServer();
  wifi_scan::cancel();
  // Never deinitialize the driver in the same call that drops the station:
  // the tcpip task still owes a DHCP release and sends it through the Wi-Fi
  // driver (esp_netif_down -> dhcp_release_and_stop -> ieee80211_output),
  // which faulted at a null driver pointer when Finish & Exit was pressed.
  // Stop the DHCP client, disconnect, and let loop() power the radio off
  // once the association is gone.
  esp_netif_t* station = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
  if (station != nullptr) esp_netif_dhcpc_stop(station);
  WiFi.disconnect(false, false);
  WiFi.softAPdisconnect(false);
  MDNS.end();
  radioOffPending = true;
  radioOffStartedMs = millis();
  lanMode = false;
  switchToLanPending = false;
  setupScanPending = false;
  portalScanPending = false;
  portalScanFailed = false;
  exitPending = false;
  portalNonce[0] = '\0';
  lastSetupClientCount = 0xff;
  setStatus(Status::Inactive, "Portal off");
}

bool active() { return currentStatus != Status::Inactive; }
Status status() { return currentStatus; }
const char* statusText() { return statusMessage; }
const char* ssid() { return activeSsid; }
const char* password() {
  return "";
}
const char* url() { return portalUrl; }
const char* qrPayload() {
  if (lanMode) return portalUrl;
  std::snprintf(qrPayloadValue, sizeof(qrPayloadValue),
                "WIFI:T:nopass;S:%s;;", apSsid);
  return qrPayloadValue;
}

const char* unitId() {
  if (panelIdentity[0] == '\0') {
    studio::formatPanelIdentity(
        studio::canonicalPanelHardwareId(ESP.getEfuseMac()), panelIdentity);
  }
  return panelIdentity;
}

SavedWifiSummary savedWifiSummary() {
  SavedWifiSummary summary;
  studio::PreferencesHomeAssistantBackend backend;
  studio::HomeAssistantConfigStore store(backend);
  studio::HomeAssistantConfig config;
  const studio::ConfigLoadStatus status = store.load(config);
  if (status != studio::ConfigLoadStatus::Corrupt && config.wifiConfigured &&
      config.wifiSsid[0] != '\0') {
    summary.configured = true;
    std::strncpy(summary.ssid, config.wifiSsid, sizeof(summary.ssid) - 1);
  }
  return summary;
}

}  // namespace portal

#endif
