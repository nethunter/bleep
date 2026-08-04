#include "portal_service.h"

#include <Arduino.h>

#ifdef UI_SIMULATOR

namespace portal {
namespace {
bool running = false;
bool lan = false;
Status simulatedStatus = Status::Ready;
const char* simulatedMessage = "Set up studio Wi-Fi";
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
const char* ssid() { return lan ? "Studio-WiFi" : "Bleep-Setup-SIM"; }
const char* password() { return lan ? "" : "12345678"; }
const char* url() { return lan ? "http://192.168.1.84" : "http://192.168.4.1"; }
void simSetLan(bool connected) {
  lan = connected;
  simulatedStatus = Status::Ready;
  simulatedMessage = connected ? "LAN portal ready" : "Set up studio Wi-Fi";
}
void simSetWifiFeedback(Status status, const char* message) {
  simulatedStatus = status;
  simulatedMessage = message;
}
}  // namespace portal

#else

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include <WiFi.h>

#include <cstdio>
#include <cstring>
#include <new>

#include "core/device_manager.h"
#include "core/home_assistant_config.h"
#include "core/preferences_store.h"
#include "core/scene_service.h"
#include "devices/home_assistant/protocol.h"

namespace portal {
namespace {

constexpr uint32_t kTimeoutMs = 10 * 60 * 1000;
constexpr size_t kDiscoveryLimit = 24;
constexpr size_t kWifiScanLimit = 16;
constexpr uint32_t kWifiConnectTimeoutMs = 15000;
constexpr uint32_t kLanHandoffDelayMs = 8000;
constexpr const char* kSetupPassword = "12345678";

enum class WifiJoinState : uint8_t { Idle, Connecting, Connected, Failed };

WebServer* server = nullptr;
Status currentStatus = Status::Inactive;
uint32_t lastActivity = 0;
bool lanMode = false;
bool switchToLanPending = false;
WifiJoinState wifiJoinState = WifiJoinState::Idle;
uint32_t wifiJoinStarted = 0;
uint32_t switchToLanAt = 0;
char apSsid[24] = "";
char activeSsid[studio::kWifiSsidCapacity] = "";
char pendingWifiSsid[studio::kWifiSsidCapacity] = "";
char pendingWifiPassword[studio::kWifiPasswordCapacity] = "";
char wifiJoinMessage[64] = "Ready to connect";
char statusMessage[48] = "Portal off";
char portalUrl[32] = "http://192.168.4.1";

const char kStyle[] PROGMEM = R"HTML(<style>
:root{color-scheme:dark;--bg:#05070a;--panel:#12161d;--ink:#f3f4f6;--muted:#8a94a6;--cyan:#35c7f2}
*{box-sizing:border-box}body{margin:0;background:radial-gradient(circle at 20% 0,#12303b 0,transparent 38%),var(--bg);color:var(--ink);font:15px ui-monospace,SFMono-Regular,Menlo,monospace}main{max-width:760px;margin:auto;padding:32px 18px 80px}header{border-left:4px solid var(--cyan);padding-left:16px;margin-bottom:24px}h1{font-size:28px;letter-spacing:.08em;margin:0}header p,.hint{color:var(--muted)}section{background:var(--panel);border:1px solid #27313d;border-radius:14px;padding:18px;margin:14px 0;box-shadow:0 20px 50px #0008}h2{font-size:14px;color:var(--cyan);letter-spacing:.14em;text-transform:uppercase;margin:0 0 14px}label{display:block;color:var(--muted);font-size:12px;margin:12px 0 5px}input{width:100%;background:#080b10;border:1px solid #35404e;border-radius:8px;color:var(--ink);padding:11px;font:inherit}input:focus{outline:2px solid var(--cyan);border-color:transparent}.entity{display:grid;grid-template-columns:1fr 1fr;gap:8px;padding:10px 0;border-top:1px solid #252d38}.entity:first-of-type{border-top:0}button{background:var(--cyan);color:#021016;border:0;border-radius:9px;padding:12px 18px;font:700 14px inherit;cursor:pointer}.secondary{background:#27313d;color:var(--ink)}button:disabled{opacity:.5;cursor:wait}#results,#networks{display:grid;gap:6px;margin-top:10px}.result{background:#090d12;border:1px solid #27313d;border-radius:8px;padding:10px;text-align:left;color:var(--ink)}#feedback{margin-top:16px;padding:12px;border:1px solid #35404e;border-radius:8px;white-space:pre-wrap}.ok{border-color:#36d399!important;color:#75e8b7}.bad{border-color:#fb7185!important;color:#fda4af}small{color:var(--muted)}@media(max-width:520px){.entity{grid-template-columns:1fr}}
</style>)HTML";

const char kWifiPage[] PROGMEM = R"HTML(<!doctype html><html><head>
<meta name=viewport content="width=device-width,initial-scale=1"><title>Ble(e)p Wi-Fi</title>)HTML";
const char kWifiBody[] PROGMEM = R"HTML(</head><body><main><header><h1>BLE(E)P / WIFI</h1><p>First-time network setup</p></header>
<form id=wifi><section><h2>Studio network</h2><p class=hint>Choose a nearby network or enter a hidden SSID. After Ble(e)p joins, reconnect this device to that Wi-Fi and use the numeric address shown here and on the panel.</p><button id=scan class=secondary type=button>Scan nearby Wi-Fi</button><div id=networks></div><label>Wi-Fi SSID</label><input name=ssid required maxlength=32 autocomplete=off><label>Wi-Fi password</label><input name=password type=password maxlength=64 autocomplete=current-password></section><button id=connect>Connect Wi-Fi</button><div id=feedback role=status aria-live=polite>Ready to scan or connect.</div></form>
<script>const form=document.querySelector('#wifi'),scan=document.querySelector('#scan'),connect=document.querySelector('#connect'),networks=document.querySelector('#networks'),feedback=document.querySelector('#feedback'),ssid=document.querySelector('[name=ssid]');let timer;
function message(text,kind=''){feedback.textContent=text;feedback.className=kind}
async function scanWifi(){scan.disabled=true;networks.textContent='Scanning nearby networks…';message('Scanning…');try{await fetch('/api/wifi/scan',{method:'POST'});pollScan()}catch(e){networks.textContent='Scan request failed. You can enter the SSID manually.';message('Could not start Wi-Fi scan.','bad');scan.disabled=false}}
async function pollScan(){try{let r=await fetch('/api/wifi/scan'),d=await r.json();if(d.state==='scanning'){setTimeout(pollScan,500);return}networks.textContent='';(d.networks||[]).forEach(n=>{let b=document.createElement('button');b.type='button';b.className='result';let name=document.createElement('div');name.textContent=n.ssid;let meta=document.createElement('small');meta.textContent=n.rssi+' dBm · '+(n.secure?'secured':'open');b.append(name,meta);b.onclick=()=>{ssid.value=n.ssid;ssid.focus();message('Selected '+n.ssid+'. Enter its password, then connect.')};networks.append(b)});if(!networks.children.length)networks.textContent=d.error||'No networks found. Enter the SSID manually.';message('Scan complete.');scan.disabled=false}catch(e){networks.textContent='Scan interrupted. Enter the SSID manually.';message('Could not read scan results.','bad');scan.disabled=false}}
async function pollJoin(){try{let r=await fetch('/api/wifi/status'),d=await r.json();message(d.message,d.state==='connected'?'ok':d.state==='failed'?'bad':'');if(d.state==='connecting'){timer=setTimeout(pollJoin,500);return}connect.disabled=false;scan.disabled=false;if(d.state==='connected'){let link=document.createElement('a');link.href=d.url;link.textContent=d.url;feedback.append(document.createElement('br'),'Reconnect this device to '+d.ssid+', then open ',link,'.');if(d.alias)feedback.append(document.createElement('br'),'Optional alias: '+d.alias)}}catch(e){message('The setup network closed. Reconnect to your normal Wi-Fi and use the numeric address shown on the Ble(e)p panel.','ok')}}
form.onsubmit=async e=>{e.preventDefault();clearTimeout(timer);connect.disabled=true;scan.disabled=true;networks.textContent='';message('Starting Wi-Fi connection…');try{let r=await fetch('/wifi',{method:'POST',body:new URLSearchParams(new FormData(form))});let d=await r.json();if(!r.ok)throw new Error(d.error||'Could not start connection');message(d.message);pollJoin()}catch(e){message(e.message,'bad');connect.disabled=false;scan.disabled=false}};scan.onclick=scanWifi;scanWifi();</script></main></body></html>)HTML";

const char kPortalPage[] PROGMEM = R"HTML(<!doctype html><html><head>
<meta name=viewport content="width=device-width,initial-scale=1"><title>Ble(e)p Portal</title>)HTML";
const char kPortalBody[] PROGMEM = R"HTML(</head><body><main><header><h1>BLE(E)P / LINK</h1><p>Available only while Portal is open on the panel</p></header>
<form method=post action=/save><section><h2>Home Assistant</h2><label>Local Home Assistant URL</label><input name=url value="http://homeassistant.local:8123" required><label>Long-lived token</label><input name=token type=password required></section>
<section><h2>Entity rack / max 4</h2><p class=hint>Search by friendly name or canonical entity ID.</p><input id=q placeholder="light.key or Key Light"><button class=secondary type=button onclick=findEntities()>Scan supported entities</button><div id=results></div><div id=slots></div></section><button>Save configuration</button></form>
<script>const slots=document.querySelector('#slots');for(let i=0;i<4;i++)slots.insertAdjacentHTML('beforeend',`<div class=entity><div><label>Entity ${i+1}</label><input name=id${i} id=id${i} placeholder=light.key_light></div><div><label>Panel name</label><input name=name${i} id=name${i} placeholder=Key Light><input type=hidden name=instance${i}></div></div>`);
async function findEntities(){let p=new URLSearchParams({q:q.value,url:document.querySelector('[name=url]').value,token:document.querySelector('[name=token]').value});results.textContent='Scanning…';let r=await fetch('/api/entities',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:p});let d=await r.json();results.textContent='';(d.entities||[]).forEach(e=>{let b=document.createElement('button');b.type='button';b.className='result';let n=document.createElement('div');n.textContent=e.name;let s=document.createElement('small');s.textContent=e.entity_id+' · '+e.state;b.append(n,s);b.onclick=()=>{for(let i=0;i<4;i++)if(!document.querySelector('#id'+i).value){document.querySelector('#id'+i).value=e.entity_id;document.querySelector('#name'+i).value=e.name;break}};results.append(b)});if(!results.children.length)results.textContent=d.error||'No matches';}
fetch('/api/config').then(r=>r.json()).then(d=>{if(d.url)document.querySelector('[name=url]').value=d.url;(d.entities||[]).forEach((e,i)=>{document.querySelector('#id'+i).value=e.entity_id;document.querySelector('#name'+i).value=e.name;document.querySelector('[name=instance'+i+']').value=e.instance_id})});</script></main></body></html>)HTML";

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

bool matches(const char* value, const char* query) {
  if (query == nullptr || query[0] == '\0') return true;
  String left(value != nullptr ? value : "");
  String right(query);
  left.toLowerCase();
  right.toLowerCase();
  return left.indexOf(right) >= 0;
}

void handleWifiSave() {
  if (!requireStage(false)) return;
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

  WiFi.scanDelete();
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

void startWifiScan() {
  if (!requireStage(false)) return;
  if (wifiJoinState == WifiJoinState::Connecting) {
    server->send(409, "application/json",
                 "{\"error\":\"Wait for the Wi-Fi connection attempt to finish\"}");
    return;
  }
  WiFi.scanDelete();
  const int result = WiFi.scanNetworks(true, false);
  if (result == WIFI_SCAN_FAILED) {
    server->send(500, "application/json",
                 "{\"error\":\"Could not start Wi-Fi scan\"}");
    return;
  }
  setStatus(Status::Testing, "Scanning nearby Wi-Fi");
  server->send(202, "application/json", "{\"state\":\"scanning\"}");
}

void sendWifiScan() {
  if (!requireStage(false)) return;
  const int found = WiFi.scanComplete();
  JsonDocument doc;
  JsonArray networks = doc["networks"].to<JsonArray>();
  if (found == WIFI_SCAN_RUNNING) {
    doc["state"] = "scanning";
  } else if (found < 0) {
    doc["state"] = "failed";
    doc["error"] = "Wi-Fi scan failed. Enter the SSID manually.";
    setStatus(Status::Ready, "Wi-Fi scan failed");
  } else {
    doc["state"] = "complete";
    for (int i = 0; i < found && networks.size() < kWifiScanLimit; ++i) {
      const String candidate = WiFi.SSID(i);
      if (candidate.length() == 0) continue;
      bool duplicate = false;
      for (JsonObject existing : networks) {
        if (candidate == existing["ssid"].as<const char*>()) {
          duplicate = true;
          break;
        }
      }
      if (duplicate) continue;
      JsonObject item = networks.add<JsonObject>();
      item["ssid"] = candidate;
      item["rssi"] = WiFi.RSSI(i);
      item["secure"] = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
    }
    WiFi.scanDelete();
    setStatus(Status::Ready, networks.size() == 0 ? "No Wi-Fi found" : "Choose studio Wi-Fi");
  }
  String body;
  serializeJson(doc, body);
  server->send(200, "application/json", body);
}

void handleEntities() {
  if (!requireStage(true)) return;
  setStatus(Status::Testing, "Querying Home Assistant");
  const String baseUrl = server->arg("url");
  const String token = server->arg("token");
  const String query = server->arg("q");
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
              (!matches(entityId, query.c_str()) && !matches(friendly, query.c_str()))) {
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
  if (!requireStage(true)) return;
  studio::PreferencesHomeAssistantBackend backend;
  studio::HomeAssistantConfigStore store(backend);
  studio::HomeAssistantConfig config;
  store.load(config);
  JsonDocument doc;
  doc["url"] = config.baseUrl;
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
  studio::PreferencesHomeAssistantBackend backend;
  studio::HomeAssistantConfigStore store(backend);
  studio::HomeAssistantConfig previous;
  if (store.load(previous) == studio::ConfigLoadStatus::Corrupt ||
      previous.wifiSsid[0] == '\0') {
    server->send(409, "text/plain", "Wi-Fi must be configured first");
    return;
  }
  studio::HomeAssistantConfig config = previous;
  config.configured = true;
  std::strncpy(config.baseUrl, server->arg("url").c_str(),
               sizeof(config.baseUrl) - 1);
  std::strncpy(config.token, server->arg("token").c_str(),
               sizeof(config.token) - 1);
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
  server->send(200, "text/html",
      "<body style='background:#05070a;color:#f3f4f6;font-family:monospace;padding:3rem'><h1>LINK SAVED</h1><p>Return to the panel and exit Portal.</p></body>");
}

void installHandlers() {
  server->on("/", HTTP_GET, [] {
    touch();
    sendPage(lanMode ? kPortalPage : kWifiPage,
             lanMode ? kPortalBody : kWifiBody);
  });
  server->on("/wifi", HTTP_POST, handleWifiSave);
  server->on("/api/wifi/status", HTTP_GET, sendWifiStatus);
  server->on("/api/wifi/scan", HTTP_POST, startWifiScan);
  server->on("/api/wifi/scan", HTTP_GET, sendWifiScan);
  server->on("/api/config", HTTP_GET, handleConfig);
  server->on("/api/entities", HTTP_POST, handleEntities);
  server->on("/save", HTTP_POST, handleSave);
  server->onNotFound([] {
    touch();
    server->sendHeader("Location", "/", true);
    server->send(302);
  });
}

bool startServer(IPAddress address) {
  server = new (std::nothrow) WebServer(address, 80);
  if (server == nullptr) return false;
  installHandlers();
  server->begin();
  return true;
}

void destroyServer() {
  if (server == nullptr) return;
  server->stop();
  delete server;
  server = nullptr;
}

bool startSetupAp() {
  MDNS.end();
  lanMode = false;
  switchToLanPending = false;
  switchToLanAt = 0;
  wifiJoinState = WifiJoinState::Idle;
  std::strncpy(wifiJoinMessage, "Ready to connect", sizeof(wifiJoinMessage) - 1);
  WiFi.mode(WIFI_AP_STA);
  if (!WiFi.softAP(apSsid, kSetupPassword)) return false;
  std::strncpy(activeSsid, apSsid, sizeof(activeSsid) - 1);
  std::strncpy(portalUrl, "http://192.168.4.1", sizeof(portalUrl) - 1);
  if (!startServer(WiFi.softAPIP())) {
    WiFi.softAPdisconnect(true);
    return false;
  }
  setStatus(Status::Ready, "Set up studio Wi-Fi");
  return true;
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
  const uint64_t chip = ESP.getEfuseMac();
  std::snprintf(apSsid, sizeof(apSsid), "Bleep-Setup-%04X",
                static_cast<unsigned>(chip & 0xffff));

  studio::PreferencesHomeAssistantBackend backend;
  studio::HomeAssistantConfigStore store(backend);
  studio::HomeAssistantConfig config;
  const studio::ConfigLoadStatus loaded = store.load(config);
  if (loaded != studio::ConfigLoadStatus::Corrupt && config.wifiSsid[0] != '\0') {
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
  if (!startSetupAp()) {
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    setStatus(Status::Error, "Could not start Portal");
    return false;
  }
  lastActivity = millis();
  return true;
}

void loop() {
  if (currentStatus == Status::Inactive || currentStatus == Status::Error) return;
  if (server != nullptr) server->handleClient();
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
  WiFi.disconnect(true, false);
  WiFi.softAPdisconnect(true);
  MDNS.end();
  WiFi.mode(WIFI_OFF);
  lanMode = false;
  switchToLanPending = false;
  setStatus(Status::Inactive, "Portal off");
}

bool active() { return currentStatus != Status::Inactive; }
Status status() { return currentStatus; }
const char* statusText() { return statusMessage; }
const char* ssid() { return activeSsid; }
const char* password() {
  return (lanMode || wifiJoinState == WifiJoinState::Connected) ? "" : kSetupPassword;
}
const char* url() { return portalUrl; }

}  // namespace portal

#endif
