#include "firmware_update.h"

#include <cstdio>
#include <cstring>

#include "core/firmware_update_policy.h"
#include "core/home_assistant_config.h"
#include "core/panel_settings.h"
#ifndef UI_SIMULATOR
#include "core/preferences_store.h"
#endif

#ifdef UI_SIMULATOR

namespace firmware_update {
namespace {
studio::FirmwareUpdatePolicy policy;
Snapshot snapshot;
bool runtimeIdle = true;
bool automaticEligible = true;
bool factoryResetRequested = false;
}

void FirmwareUpdateService::begin() {
  policy.begin(0);
  snapshot = {};
}
void FirmwareUpdateService::loop() {}
void FirmwareUpdateService::noteUserActivity() {}
void FirmwareUpdateService::setRuntimeIdle(bool idle, bool eligible) {
  runtimeIdle = idle;
  automaticEligible = eligible;
}
void FirmwareUpdateService::checkNow(bool allowDisconnect) {
  if (!runtimeIdle && !allowDisconnect) {
    snapshot.status = Status::Deferred;
    snapshot.disconnectRequired = true;
    std::strncpy(snapshot.message, "Disconnect devices to check",
                 sizeof(snapshot.message) - 1);
    return;
  }
  snapshot.status = Status::Idle;
  snapshot.disconnectRequired = false;
  snapshot.wifiConfigured = true;
  std::strncpy(snapshot.message, "No update available",
               sizeof(snapshot.message) - 1);
}
bool FirmwareUpdateService::installAvailable() {
  if (!snapshot.updateAvailable) return false;
  snapshot.status = Status::Downloading;
  std::strncpy(snapshot.message, "Downloading update",
               sizeof(snapshot.message) - 1);
  return true;
}
void FirmwareUpdateService::dismissAvailable() {
  if (snapshot.updateAvailable) {
    snapshot.notificationPending = false;
    std::strncpy(snapshot.message, "Update available in Settings",
                 sizeof(snapshot.message) - 1);
  }
}
bool FirmwareUpdateService::enterRecovery() { return snapshot.recoveryAvailable; }
bool FirmwareUpdateService::requestFactoryReset() {
  factoryResetRequested = snapshot.recoveryAvailable;
  return factoryResetRequested;
}
Snapshot FirmwareUpdateService::status() const { return snapshot; }
void FirmwareUpdateService::simSetAvailable(const char* version,
                                            uint64_t sequence) {
  snapshot.status = Status::Available;
  snapshot.wifiConfigured = true;
  snapshot.updateAvailable = true;
  snapshot.notificationPending = true;
  snapshot.disconnectRequired = false;
  snapshot.releaseSequence = sequence;
  std::strncpy(snapshot.version, version, sizeof(snapshot.version) - 1);
  std::strncpy(snapshot.message, "Update available", sizeof(snapshot.message) - 1);
}
void FirmwareUpdateService::simSetWifiConfigured(bool configured) {
  snapshot.wifiConfigured = configured;
  if (!configured) {
    snapshot.status = Status::Failed;
    std::strncpy(snapshot.message, "Configure Wi-Fi first",
                 sizeof(snapshot.message) - 1);
  }
}
void FirmwareUpdateService::simSetRecoveryAvailable(bool available) {
  snapshot.recoveryAvailable = available;
}
bool FirmwareUpdateService::simFactoryResetRequested() const {
  return factoryResetRequested;
}
void FirmwareUpdateService::simClearFactoryResetRequested() {
  factoryResetRequested = false;
}
FirmwareUpdateService& service() {
  static FirmwareUpdateService instance;
  return instance;
}
}  // namespace firmware_update

#else

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <esp_http_client.h>
#include <esp_ota_ops.h>
#include <mbedtls/pk.h>
#include <mbedtls/sha256.h>
#include <time.h>

#include "build_info.h"
#include "core/partition_recovery_backend.h"
#include "core/recovery_journal.h"
#include "firmware_update_keys.h"

namespace firmware_update {
namespace {

constexpr size_t kManifestCapacity = 1536;
constexpr size_t kSignatureCapacity = 80;
constexpr size_t kMaximumImageSize = 0x2C0000;
constexpr uint32_t kConnectTimeoutMs = 12000;
constexpr uint32_t kRequestTimeoutMs = 30000;
constexpr uint32_t kBootValidationMs = 10000;
constexpr uint32_t kMinimumFreeHeap = 48000;
constexpr uint32_t kMinimumLargestBlock = 36000;
constexpr char kStableManifestUrl[] =
    "https://github.com/nethunter/bleep/releases/latest/download/bleep-update.json";
constexpr char kDevelopmentManifestUrl[] =
    "https://github.com/nethunter/bleep/releases/download/latest/bleep-update.json";
constexpr char kGithubRootCa[] = R"CERT(-----BEGIN CERTIFICATE-----
MIIDjjCCAnagAwIBAgIQAzrx5qcRqaC7KGSxHQn65TANBgkqhkiG9w0BAQsFADBh
MQswCQYDVQQGEwJVUzEVMBMGA1UEChMMRGlnaUNlcnQgSW5jMRkwFwYDVQQLExB3
d3cuZGlnaWNlcnQuY29tMSAwHgYDVQQDExdEaWdpQ2VydCBHbG9iYWwgUm9vdCBH
MjAeFw0xMzA4MDExMjAwMDBaFw0zODAxMTUxMjAwMDBaMGExCzAJBgNVBAYTAlVT
MRUwEwYDVQQKEwxEaWdpQ2VydCBJbmMxGTAXBgNVBAsTEHd3dy5kaWdpY2VydC5j
b20xIDAeBgNVBAMTF0RpZ2lDZXJ0IEdsb2JhbCBSb290IEcyMIIBIjANBgkqhkiG
9w0BAQEFAAOCAQ8AMIIBCgKCAQEAuzfNNNx7a8myaJCtSnX/RrohCgiN9RlUyfuI
2/Ou8jqJkTx65qsGGmvPrC3oXgkkRLpimn7Wo6h+4FR1IAWsULecYxpsMNzaHxmx
1x7e/dfgy5SDN67sH0NO3Xss0r0upS/kqbitOtSZpLYl6ZtrAGCSYP9PIUkY92eQ
q2EGnI/yuum06ZIya7XzV+hdG82MHauVBJVJ8zUtluNJbd134/tJS7SsVQepj5Wz
tCO7TG1F8PapspUwtP1MVYwnSlcUfIKdzXOS0xZKBgyMUNGPHgm+F6HmIcr9g+UQ
vIOlCsRnKPZzFBQ9RnbDhxSJITRNrw9FDKZJobq7nMWxM4MphQIDAQABo0IwQDAP
BgNVHRMBAf8EBTADAQH/MA4GA1UdDwEB/wQEAwIBhjAdBgNVHQ4EFgQUTiJUIBiV
5uNu5g/6+rkS7QYXjzkwDQYJKoZIhvcNAQELBQADggEBAGBnKJRvDkhj6zHd6mcY
1Yl9PMWLSn/pvtsrF9+wX3N3KjITOYFnQoQj8kVnNeyIv/iPsGEMNKSuIEyExtv4
NeF22d+mQrvHRAiGfzZ0JFrabA0UWTW98kndth/Jsw1HKj2ZL7tcu7XUIOGZX1NG
Fdtom/DzMNU+MeKNhJ7jitralj41E6Vf8PlwUHBHQRFXGU7Aj64GxJUTFy8bJZ91
8rGOmaFvE7FBcf6IKshPECBV1/MUReXgRPTqh5Uykw7+U0b6LJ3/iyK5S9kJRaTe
pLiaWN0bfVKfjllDiIGknibVb63dDcY3fe0Dkhvld1927jyNxF1WW6LZZm6zNTfl
MrY=
-----END CERTIFICATE-----
)CERT";

enum class TransferKind : uint8_t { None, Manifest, Signature };
enum class ManifestResult : uint8_t { Invalid, Current, Available };

struct PersistedState {
  uint32_t magic = 0x46575550;
  uint16_t version = 3;
  uint16_t reserved = 0;
  uint64_t installedSequence = 0;
  uint64_t availableSequence = 0;
  uint64_t dismissedSequence = 0;
  uint64_t lastCheckEpoch = 0;
  char lastResult[32] = "Never checked";
};

studio::FirmwareUpdatePolicy policy;
Snapshot snapshot;
bool runtimeIdle = false;
bool automaticEligible = false;
bool ownsWifi = false;
bool bootPending = false;
bool timeSyncStarted = false;
uint32_t bootValidationStarted = 0;
uint32_t operationStarted = 0;
esp_http_client_handle_t client = nullptr;
TransferKind transferKind = TransferKind::None;
char manifest[kManifestCapacity] = {};
size_t manifestLength = 0;
uint8_t signature[kSignatureCapacity] = {};
size_t signatureLength = 0;
char signatureUrl[256] = {};
char payloadUrl[320] = {};
size_t expectedImageSize = 0;
uint8_t expectedSha[32] = {};
PersistedState persisted;

void setMessage(Status status, const char* message) {
  snapshot.status = status;
  std::strncpy(snapshot.message, message, sizeof(snapshot.message) - 1);
  snapshot.message[sizeof(snapshot.message) - 1] = '\0';
}

bool wifiConfigured(studio::HomeAssistantConfig* output = nullptr) {
  studio::PreferencesHomeAssistantBackend backend;
  studio::HomeAssistantConfigStore store(backend);
  studio::HomeAssistantConfig config;
  const studio::ConfigLoadStatus status = store.load(config);
  const bool configured = status != studio::ConfigLoadStatus::Corrupt &&
                          config.wifiSsid[0] != '\0';
  snapshot.wifiConfigured = configured;
  if (configured && output != nullptr) *output = config;
  return configured;
}

void savePersisted() {
  Preferences preferences;
  if (!preferences.begin("studio", false)) return;
  preferences.putBytes("fw_update", &persisted, sizeof(persisted));
  preferences.end();
}

void loadPersisted() {
  Preferences preferences;
  if (!preferences.begin("studio", true)) return;
  PersistedState loaded;
  if (preferences.getBytesLength("fw_update") == sizeof(loaded) &&
      preferences.getBytes("fw_update", &loaded, sizeof(loaded)) == sizeof(loaded) &&
      loaded.magic == persisted.magic && loaded.version == persisted.version) {
    persisted = loaded;
    snapshot.lastCheckEpoch = persisted.lastCheckEpoch;
    std::strncpy(snapshot.lastResult, persisted.lastResult,
                 sizeof(snapshot.lastResult) - 1);
  }
  preferences.end();
}

void recordCheckResult(const char* result) {
  const time_t now = time(nullptr);
  persisted.lastCheckEpoch = now >= 1700000000 ? static_cast<uint64_t>(now) : 0;
  std::strncpy(persisted.lastResult, result, sizeof(persisted.lastResult) - 1);
  persisted.lastResult[sizeof(persisted.lastResult) - 1] = '\0';
  snapshot.lastCheckEpoch = persisted.lastCheckEpoch;
  std::strncpy(snapshot.lastResult, persisted.lastResult,
               sizeof(snapshot.lastResult) - 1);
  snapshot.lastResult[sizeof(snapshot.lastResult) - 1] = '\0';
  savePersisted();
}

void cleanupClient() {
  if (client != nullptr) {
    esp_http_client_cleanup(client);
    client = nullptr;
  }
  transferKind = TransferKind::None;
}

void releaseWifi() {
  cleanupClient();
  if (!ownsWifi) return;
  WiFi.disconnect(true, false);
  WiFi.mode(WIFI_OFF);
  ownsWifi = false;
}

void fail(const char* message) {
  const bool checkFailure = true;
  timeSyncStarted = false;
  releaseWifi();
  policy.checked(millis(), false);
  if (checkFailure) recordCheckResult(message);
  setMessage(Status::Failed, message);
}

esp_err_t onHttpEvent(esp_http_client_event_t* event) {
  if (event->event_id != HTTP_EVENT_ON_DATA || event->data_len <= 0) return ESP_OK;
  const uint8_t* bytes = static_cast<const uint8_t*>(event->data);
  const size_t length = static_cast<size_t>(event->data_len);
  if (transferKind == TransferKind::Manifest) {
    if (manifestLength + length >= sizeof(manifest)) return ESP_FAIL;
    std::memcpy(manifest + manifestLength, bytes, length);
    manifestLength += length;
    manifest[manifestLength] = '\0';
  } else if (transferKind == TransferKind::Signature) {
    if (signatureLength + length > sizeof(signature)) return ESP_FAIL;
    std::memcpy(signature + signatureLength, bytes, length);
    signatureLength += length;
  }
  return ESP_OK;
}

bool startTransfer(const char* url, TransferKind kind) {
  cleanupClient();
  esp_http_client_config_t config = {};
  config.url = url;
  config.cert_pem = kGithubRootCa;
  config.user_agent = "Bleep-Firmware-Updater/1";
  config.timeout_ms = 5000;
  config.max_redirection_count = 4;
  config.event_handler = onHttpEvent;
  config.is_async = true;
  config.buffer_size = 1024;
  transferKind = kind;
  client = esp_http_client_init(&config);
  operationStarted = millis();
  return client != nullptr;
}

bool allowedEffectiveUrl(const char* url) {
  static const char* const prefixes[] = {
      "https://github.com/nethunter/bleep/releases/",
      "https://release-assets.githubusercontent.com/",
      "https://objects.githubusercontent.com/",
      "https://github-releases.githubusercontent.com/",
  };
  for (const char* prefix : prefixes) {
    if (std::strncmp(url, prefix, std::strlen(prefix)) == 0) return true;
  }
  return false;
}

bool hexNibble(char value, uint8_t& result) {
  if (value >= '0' && value <= '9') result = value - '0';
  else if (value >= 'a' && value <= 'f') result = value - 'a' + 10;
  else if (value >= 'A' && value <= 'F') result = value - 'A' + 10;
  else return false;
  return true;
}

bool decodeSha(const char* text, uint8_t output[32]) {
  if (text == nullptr || std::strlen(text) != 64) return false;
  for (size_t i = 0; i < 32; ++i) {
    uint8_t high = 0;
    uint8_t low = 0;
    if (!hexNibble(text[i * 2], high) || !hexNibble(text[i * 2 + 1], low)) return false;
    output[i] = static_cast<uint8_t>((high << 4) | low);
  }
  return true;
}

bool verifyManifest(const char* publicKey) {
  uint8_t digest[32];
  mbedtls_sha256_ret(reinterpret_cast<const unsigned char*>(manifest),
                     manifestLength, digest, 0);
  mbedtls_pk_context key;
  mbedtls_pk_init(&key);
  const int parsed = mbedtls_pk_parse_public_key(
      &key, reinterpret_cast<const unsigned char*>(publicKey),
      std::strlen(publicKey) + 1);
  const int verified = parsed == 0
      ? mbedtls_pk_verify(&key, MBEDTLS_MD_SHA256, digest, sizeof(digest),
                          signature, signatureLength)
      : parsed;
  mbedtls_pk_free(&key);
  return verified == 0;
}

ManifestResult parseVerifiedManifest() {
  const bool development = studio::panelSettings().get().firmwareUpdateChannel ==
                           studio::FirmwareUpdateChannel::Development;
  const char* expectedChannel = development ? "development" : "stable";
  const char* expectedKeyId = development ? firmware_update_keys::kDevelopmentKeyId
                                          : firmware_update_keys::kStableKeyId;
  const char* publicKey = development ? firmware_update_keys::kDevelopmentPublicKey
                                      : firmware_update_keys::kStablePublicKey;
  if (!verifyManifest(publicKey)) return ManifestResult::Invalid;
  JsonDocument document;
  if (deserializeJson(document, manifest, manifestLength) != DeserializationError::Ok) {
    return ManifestResult::Invalid;
  }
  if (std::strcmp(document["channel"] | "", expectedChannel) != 0 ||
      std::strcmp(document["key_id"] | "", expectedKeyId) != 0) {
    return ManifestResult::Invalid;
  }
  if ((document["schema"] | 0) != 1 ||
      std::strcmp(document["hardware"] | "", build_info::kHardware) != 0 ||
      std::strcmp(document["profile"] | "", "bleep") != 0 ||
      (document["partition_schema"] | 0) != 2 ||
      (document["recovery_schema"] | 0) != 1) return ManifestResult::Invalid;
  const uint64_t sequence = document["release_sequence"] | 0ULL;
  const size_t imageSize = document["image_size"] | 0U;
  const char* url = document["payload_url"] | "";
  if (imageSize == 0 ||
      imageSize > kMaximumImageSize ||
      std::strncmp(url, "https://github.com/nethunter/bleep/releases/download/",
                   std::strlen("https://github.com/nethunter/bleep/releases/download/")) != 0 ||
      !decodeSha(document["sha256"] | "", expectedSha)) {
    return ManifestResult::Invalid;
  }
  if (sequence <= build_info::kReleaseSequence ||
      sequence <= persisted.installedSequence) return ManifestResult::Current;
  snapshot.releaseSequence = sequence;
  expectedImageSize = imageSize;
  std::strncpy(snapshot.version, document["version"] | "unknown",
               sizeof(snapshot.version) - 1);
  std::strncpy(payloadUrl, url, sizeof(payloadUrl) - 1);
  return ManifestResult::Available;
}

bool beginWifi() {
  studio::HomeAssistantConfig config;
  if (!wifiConfigured(&config)) {
    setMessage(Status::Failed, "Configure Wi-Fi first");
    return false;
  }
  if (WiFi.status() == WL_CONNECTED) {
    ownsWifi = false;
    operationStarted = millis();
    timeSyncStarted = false;
    setMessage(Status::Connecting, "Preparing secure connection");
    return true;
  }
  WiFi.mode(WIFI_STA);
  WiFi.begin(config.wifiSsid, config.wifiPassword);
  ownsWifi = true;
  operationStarted = millis();
  timeSyncStarted = false;
  setMessage(Status::Connecting, "Connecting to Wi-Fi");
  return true;
}

void startManifestRequest() {
  const bool development = studio::panelSettings().get().firmwareUpdateChannel ==
                           studio::FirmwareUpdateChannel::Development;
  const char* url = development ? kDevelopmentManifestUrl : kStableManifestUrl;
  manifestLength = 0;
  signatureLength = 0;
  std::snprintf(signatureUrl, sizeof(signatureUrl), "%.*s.sig",
                static_cast<int>(std::strlen(url) - 5), url);
  if (!startTransfer(url, TransferKind::Manifest)) {
    fail("Could not start update check");
    return;
  }
  setMessage(Status::Checking, "Checking for updates");
}

void finishCheck() {
  snapshot.updateAvailable = true;
  persisted.availableSequence = snapshot.releaseSequence;
  recordCheckResult("Update available");
  snapshot.notificationPending =
      snapshot.releaseSequence != persisted.dismissedSequence;
  snapshot.disconnectRequired = false;
  policy.checked(millis(), true);
  releaseWifi();
  setMessage(Status::Available, "Update available");
}

void handleCompletedTransfer() {
  const int code = esp_http_client_get_status_code(client);
  const TransferKind finished = transferKind;
  char effectiveUrl[384] = {};
  const bool allowed = esp_http_client_get_url(
      client, effectiveUrl, sizeof(effectiveUrl)) == ESP_OK &&
      allowedEffectiveUrl(effectiveUrl);
  cleanupClient();
  if (!allowed) {
    fail("Update redirect was not allowed");
    return;
  }
  if (code != 200) {
    fail("Update server error");
    return;
  }
  if (finished == TransferKind::Manifest) {
    if (!startTransfer(signatureUrl, TransferKind::Signature)) {
      fail("Could not fetch signature");
    }
  } else if (finished == TransferKind::Signature) {
    const ManifestResult result = parseVerifiedManifest();
    if (result == ManifestResult::Invalid) {
      fail("Update signature or target invalid");
      return;
    }
    if (result == ManifestResult::Current) {
      persisted.availableSequence = 0;
      recordCheckResult("Firmware is up to date");
      policy.checked(millis(), true);
      releaseWifi();
      setMessage(Status::Idle, "Firmware is up to date");
      return;
    }
    finishCheck();
  }
}

bool selectRecoveryAndRestart() {
  const esp_partition_t* recovery = esp_partition_find_first(
      ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, "recovery");
  if (recovery == nullptr || esp_ota_set_boot_partition(recovery) != ESP_OK) return false;
  ESP.restart();
  return true;
}

}  // namespace

void FirmwareUpdateService::begin() {
  policy.begin(millis());
  snapshot = {};
  loadPersisted();
  wifiConfigured();
  const esp_partition_t* running = esp_ota_get_running_partition();
  esp_ota_img_states_t state;
  bootPending = running != nullptr &&
                esp_ota_get_state_partition(running, &state) == ESP_OK &&
                state == ESP_OTA_IMG_PENDING_VERIFY;
  bootValidationStarted = millis();
  snapshot.recoveryAvailable =
      esp_partition_find_first(ESP_PARTITION_TYPE_APP,
                               ESP_PARTITION_SUBTYPE_APP_FACTORY,
                               "recovery") != nullptr;
}

void FirmwareUpdateService::loop() {
  const uint32_t now = millis();
  if (bootPending && now - bootValidationStarted >= kBootValidationMs) {
    if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) {
      studio::PartitionRecoveryJournalBackend backend;
      studio::RecoveryJournal journal(backend);
      studio::RecoveryRecord completed;
      if (journal.load(completed) &&
          completed.releaseSequence > persisted.installedSequence) {
        persisted.installedSequence = completed.releaseSequence;
        savePersisted();
      }
      journal.clear();
      bootPending = false;
    }
  }
  if (snapshot.status == Status::Connecting) {
    if (WiFi.status() == WL_CONNECTED) {
      if (!timeSyncStarted) {
        configTime(0, 0, "pool.ntp.org", "time.nist.gov");
        timeSyncStarted = true;
        operationStarted = now;
        setMessage(Status::Connecting, "Synchronizing secure time");
        return;
      }
      if (time(nullptr) < 1700000000) {
        if (now - operationStarted >= kConnectTimeoutMs) {
          fail("Secure time unavailable");
        }
        return;
      }
      timeSyncStarted = false;
      startManifestRequest();
    } else if (now - operationStarted >= kConnectTimeoutMs) {
      fail("Wi-Fi connection timed out");
    }
    return;
  }
  if (client != nullptr) {
    if (!runtimeIdle) {
      policy.deferStartup();
      releaseWifi();
      setMessage(Status::Deferred, "Update check deferred");
      return;
    }
    const esp_err_t result = esp_http_client_perform(client);
    if (result == ESP_OK) handleCompletedTransfer();
    else if (result != ESP_ERR_HTTP_EAGAIN && result != ESP_ERR_HTTP_CONNECTING) {
      fail("Network transfer failed");
    } else if (now - operationStarted >= kRequestTimeoutMs) {
      fail("Update request timed out");
    }
    return;
  }
  if (snapshot.status == Status::Available ||
      snapshot.status == Status::RebootPending) return;
  if (!runtimeIdle) policy.deferStartup();
  const bool heapSafe = ESP.getFreeHeap() >= kMinimumFreeHeap &&
      heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) >= kMinimumLargestBlock;
  if (policy.immediateRequested() && runtimeIdle && !heapSafe) {
    setMessage(Status::Deferred, "Waiting for available memory");
  }
  const bool checkEligible = runtimeIdle && heapSafe &&
      (automaticEligible || policy.immediateRequested());
  if (policy.shouldCheck(now, checkEligible, snapshot.wifiConfigured)) {
    if (!beginWifi()) policy.checked(now, false);
  }
}

void FirmwareUpdateService::noteUserActivity() { policy.noteUserActivity(millis()); }
void FirmwareUpdateService::setRuntimeIdle(bool idle, bool eligible) {
  if (eligible && !automaticEligible) wifiConfigured();
  runtimeIdle = idle;
  automaticEligible = eligible;
}

void FirmwareUpdateService::checkNow(bool allowDisconnect) {
  wifiConfigured();
  if (!snapshot.wifiConfigured) {
    setMessage(Status::Failed, "Configure Wi-Fi first");
    return;
  }
  if (!runtimeIdle && !allowDisconnect) {
    snapshot.disconnectRequired = true;
    setMessage(Status::Deferred, "Disconnect devices to check");
    return;
  }
  snapshot.disconnectRequired = false;
  policy.requestImmediate();
}

bool FirmwareUpdateService::installAvailable() {
  if (!snapshot.updateAvailable || payloadUrl[0] == '\0' ||
      expectedImageSize == 0 || manifestLength == 0 || signatureLength == 0) return false;
  studio::RecoveryRecord record;
  record.operation = studio::RecoveryOperation::InstallRequested;
  record.channel = static_cast<uint8_t>(
      studio::panelSettings().get().firmwareUpdateChannel);
  record.releaseSequence = snapshot.releaseSequence;
  record.manifestLength = manifestLength;
  record.signatureLength = signatureLength;
  std::memcpy(record.manifest, manifest, manifestLength);
  std::memcpy(record.signature, signature, signatureLength);
  studio::PartitionRecoveryJournalBackend backend;
  studio::RecoveryJournal journal(backend);
  studio::RecoveryRecord ignored;
  journal.load(ignored);
  if (!journal.save(record)) return false;
  return selectRecoveryAndRestart();
}

void FirmwareUpdateService::dismissAvailable() {
  if (!snapshot.updateAvailable) return;
  persisted.dismissedSequence = snapshot.releaseSequence;
  savePersisted();
  snapshot.notificationPending = false;
  setMessage(Status::Available, "Update available in Settings");
}

bool FirmwareUpdateService::enterRecovery() {
  studio::PartitionRecoveryJournalBackend backend;
  studio::RecoveryJournal journal(backend);
  studio::RecoveryRecord record;
  journal.load(record);
  record = {};
  if (!journal.save(record)) return false;
  return selectRecoveryAndRestart();
}

bool FirmwareUpdateService::requestFactoryReset() {
  studio::RecoveryRecord record;
  record.operation = studio::RecoveryOperation::FactoryResetRequested;
  record.channel = static_cast<uint8_t>(studio::FirmwareUpdateChannel::Stable);
  studio::PartitionRecoveryJournalBackend backend;
  studio::RecoveryJournal journal(backend);
  studio::RecoveryRecord ignored;
  journal.load(ignored);
  return journal.save(record) && selectRecoveryAndRestart();
}

Snapshot FirmwareUpdateService::status() const { return snapshot; }

FirmwareUpdateService& service() {
  static FirmwareUpdateService instance;
  return instance;
}

}  // namespace firmware_update

#endif
