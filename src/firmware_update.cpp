#include "firmware_update.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>

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
bool recoveryRequested = false;
bool factoryResetRequested = false;
}

void FirmwareUpdateService::begin() {
  policy.begin(0);
  snapshot = {};
  recoveryRequested = false;
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
bool FirmwareUpdateService::enterRecovery() {
  recoveryRequested = snapshot.recoveryAvailable;
  return recoveryRequested;
}
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
void FirmwareUpdateService::simSetChecking() {
  snapshot.status = Status::Checking;
  snapshot.wifiConfigured = true;
  snapshot.updateAvailable = false;
  snapshot.notificationPending = false;
  snapshot.disconnectRequired = false;
  std::strncpy(snapshot.message, "Checking for updates",
               sizeof(snapshot.message) - 1);
}
void FirmwareUpdateService::simSetFailure(const char* message) {
  snapshot.status = Status::Failed;
  snapshot.wifiConfigured = true;
  snapshot.updateAvailable = false;
  snapshot.notificationPending = false;
  snapshot.disconnectRequired = false;
  std::strncpy(snapshot.lastResult, message, sizeof(snapshot.lastResult) - 1);
  snapshot.lastResult[sizeof(snapshot.lastResult) - 1] = '\0';
  std::strncpy(snapshot.message, message, sizeof(snapshot.message) - 1);
  snapshot.message[sizeof(snapshot.message) - 1] = '\0';
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
void FirmwareUpdateService::simSetRecoveryRefresh(uint8_t progressPercent) {
  snapshot.status = Status::Downloading;
  snapshot.recoveryUpdatePending = true;
  snapshot.progressPercent = progressPercent;
  std::strncpy(snapshot.message, "Updating recovery", sizeof(snapshot.message) - 1);
}
void FirmwareUpdateService::simClearRecoveryRefresh() {
  snapshot.status = Status::Idle;
  snapshot.recoveryUpdatePending = false;
  snapshot.progressPercent = 0;
}
bool FirmwareUpdateService::simRecoveryRequested() const {
  return recoveryRequested;
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
#include <esp_image_format.h>
#include <esp_netif.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
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
constexpr int kHttpBufferSize = 8192;
constexpr size_t kMaximumImageSize = 0x2C0000;
constexpr size_t kMaximumRecoveryImageSize = 0xF0000;
constexpr uint32_t kConnectTimeoutMs = 12000;
constexpr uint32_t kRequestTimeoutMs = 30000;
constexpr uint32_t kRecoveryRequestTimeoutMs = 120000;
constexpr uint32_t kBootValidationMs = 10000;
constexpr uint32_t kWifiShutdownSettleMs = 250;
constexpr uint32_t kWifiShutdownTimeoutMs = 2000;
constexpr uint32_t kMinimumFreeHeap = 48000;
constexpr uint32_t kMinimumLargestBlock = 36000;
constexpr char kStableManifestUrl[] =
    "https://github.com/nethunter/bleep/releases/latest/download/bleep-update.json";
constexpr char kDevelopmentManifestUrl[] =
    "https://github.com/nethunter/bleep/releases/download/latest/bleep-update.json";
// GitHub and its release CDN currently terminate through different public PKI
// chains. Keep the bounded roots together so an ordinary certificate rotation
// cannot strand the updater before it can verify Ble(e)p's own signed manifest.
constexpr char kUpdateTrustRoots[] = R"CERT(-----BEGIN CERTIFICATE-----
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
-----BEGIN CERTIFICATE-----
MIICjzCCAhWgAwIBAgIQXIuZxVqUxdJxVt7NiYDMJjAKBggqhkjOPQQDAzCBiDEL
MAkGA1UEBhMCVVMxEzARBgNVBAgTCk5ldyBKZXJzZXkxFDASBgNVBAcTC0plcnNl
eSBDaXR5MR4wHAYDVQQKExVUaGUgVVNFUlRSVVNUIE5ldHdvcmsxLjAsBgNVBAMT
JVVTRVJUcnVzdCBFQ0MgQ2VydGlmaWNhdGlvbiBBdXRob3JpdHkwHhcNMTAwMjAx
MDAwMDAwWhcNMzgwMTE4MjM1OTU5WjCBiDELMAkGA1UEBhMCVVMxEzARBgNVBAgT
Ck5ldyBKZXJzZXkxFDASBgNVBAcTC0plcnNleSBDaXR5MR4wHAYDVQQKExVUaGUg
VVNFUlRSVVNUIE5ldHdvcmsxLjAsBgNVBAMTJVVTRVJUcnVzdCBFQ0MgQ2VydGlm
aWNhdGlvbiBBdXRob3JpdHkwdjAQBgcqhkjOPQIBBgUrgQQAIgNiAAQarFRaqflo
I+d61SRvU8Za2EurxtW20eZzca7dnNYMYf3boIkDuAUU7FfO7l0/4iGzzvfUinng
o4N+LZfQYcTxmdwlkWOrfzCjtHDix6EznPO/LlxTsV+zfTJ/ijTjeXmjQjBAMB0G
A1UdDgQWBBQ64QmG1M8ZwpZ2dEl23OA1xmNjmjAOBgNVHQ8BAf8EBAMCAQYwDwYD
VR0TAQH/BAUwAwEB/zAKBggqhkjOPQQDAwNoADBlAjA2Z6EWCNzklwBBHU6+4WMB
zzuqQhFkoJ2UOQIReVx7Hfpkue4WQrO/isIJxOzksU0CMQDpKmFHjFJKS04YcPbW
RNZu9YO6bVi9JNlWSOrvxKJGgYhqOkbRqZtNyWHa0V1Xahg=
-----END CERTIFICATE-----
-----BEGIN CERTIFICATE-----
MIIFazCCA1OgAwIBAgIRAIIQz7DSQONZRGPgu2OCiwAwDQYJKoZIhvcNAQELBQAw
TzELMAkGA1UEBhMCVVMxKTAnBgNVBAoTIEludGVybmV0IFNlY3VyaXR5IFJlc2Vh
cmNoIEdyb3VwMRUwEwYDVQQDEwxJU1JHIFJvb3QgWDEwHhcNMTUwNjA0MTEwNDM4
WhcNMzUwNjA0MTEwNDM4WjBPMQswCQYDVQQGEwJVUzEpMCcGA1UEChMgSW50ZXJu
ZXQgU2VjdXJpdHkgUmVzZWFyY2ggR3JvdXAxFTATBgNVBAMTDElTUkcgUm9vdCBY
MTCCAiIwDQYJKoZIhvcNAQEBBQADggIPADCCAgoCggIBAK3oJHP0FDfzm54rVygc
h77ct984kIxuPOZXoHj3dcKi/vVqbvYATyjb3miGbESTtrFj/RQSa78f0uoxmyF+
0TM8ukj13Xnfs7j/EvEhmkvBioZxaUpmZmyPfjxwv60pIgbz5MDmgK7iS4+3mX6U
A5/TR5d8mUgjU+g4rk8Kb4Mu0UlXjIB0ttov0DiNewNwIRt18jA8+o+u3dpjq+sW
T8KOEUt+zwvo/7V3LvSye0rgTBIlDHCNAymg4VMk7BPZ7hm/ELNKjD+Jo2FR3qyH
B5T0Y3HsLuJvW5iB4YlcNHlsdu87kGJ55tukmi8mxdAQ4Q7e2RCOFvu396j3x+UC
B5iPNgiV5+I3lg02dZ77DnKxHZu8A/lJBdiB3QW0KtZB6awBdpUKD9jf1b0SHzUv
KBds0pjBqAlkd25HN7rOrFleaJ1/ctaJxQZBKT5ZPt0m9STJEadao0xAH0ahmbWn
OlFuhjuefXKnEgV4We0+UXgVCwOPjdAvBbI+e0ocS3MFEvzG6uBQE3xDk3SzynTn
jh8BCNAw1FtxNrQHusEwMFxIt4I7mKZ9YIqioymCzLq9gwQbooMDQaHWBfEbwrbw
qHyGO0aoSCqI3Haadr8faqU9GY/rOPNk3sgrDQoo//fb4hVC1CLQJ13hef4Y53CI
rU7m2Ys6xt0nUW7/vGT1M0NPAgMBAAGjQjBAMA4GA1UdDwEB/wQEAwIBBjAPBgNV
HRMBAf8EBTADAQH/MB0GA1UdDgQWBBR5tFnme7bl5AFzgAiIyBpY9umbbjANBgkq
hkiG9w0BAQsFAAOCAgEAVR9YqbyyqFDQDLHYGmkgJykIrGF1XIpu+ILlaS/V9lZL
ubhzEFnTIZd+50xx+7LSYK05qAvqFyFWhfFQDlnrzuBZ6brJFe+GnY+EgPbk6ZGQ
3BebYhtF8GaV0nxvwuo77x/Py9auJ/GpsMiu/X1+mvoiBOv/2X/qkSsisRcOj/KK
NFtY2PwByVS5uCbMiogziUwthDyC3+6WVwW6LLv3xLfHTjuCvjHIInNzktHCgKQ5
ORAzI4JMPJ+GslWYHb4phowim57iaztXOoJwTdwJx4nLCgdNbOhdjsnvzqvHu7Ur
TkXWStAmzOVyyghqpZXjFaH3pO3JLF+l+/+sKAIuvtd7u+Nxe5AW0wdeRlN8NwdC
jNPElpzVmbUq4JUagEiuTDkHzsxHpFKVK7q4+63SM1N95R1NbdWhscdCb+ZAJzVc
oyi3B43njTOQ5yOf+1CceWxG1bQVs5ZufpsMljq4Ui0/1lvh+wjChP4kqKOJ2qxq
4RgqsahDYVvTH9w7jXbyLeiNdd8XM2w9U/t7y0Ff/9yi0GE44Za4rF2LN9d11TPA
mRGunUHBcnWEvgJBQl9nJEiU0Zsnvgc/ubhPgXRR4Xq37Z0j4r7g1SgEEzwxA57d
emyPxgcYxn/eR44/KJ4EBs+lVDR3veyJm+kXQ99b21/+jh5Xos1AnX5iItreGCc=
-----END CERTIFICATE-----
)CERT";

enum class TransferKind : uint8_t { None, Manifest, Signature, RecoveryImage };
enum class ManifestResult : uint8_t { Invalid, Current, Available };
enum class OperationKind : uint8_t { None, Check, RecoveryRefresh };
enum class RecoveryManifestResult : uint8_t { None, Invalid, Current, Pending };

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
bool wifiShutdownPending = false;
bool bootPending = false;
bool timeSyncStarted = false;
uint32_t bootValidationStarted = 0;
uint32_t operationStarted = 0;
uint32_t wifiShutdownStarted = 0;
esp_http_client_handle_t client = nullptr;
TransferKind transferKind = TransferKind::None;
OperationKind operationKind = OperationKind::None;
char manifest[kManifestCapacity] = {};
size_t manifestLength = 0;
uint8_t signature[kSignatureCapacity] = {};
size_t signatureLength = 0;
char signatureUrl[256] = {};
char payloadUrl[320] = {};
size_t expectedImageSize = 0;
uint8_t expectedSha[32] = {};
char recoveryPayloadUrl[320] = {};
size_t expectedRecoveryImageSize = 0;
uint8_t expectedRecoverySha[32] = {};
uint64_t expectedRecoverySequence = 0;
uint64_t installedRecoverySequence = 0;
bool recoveryRefreshPending = false;
uint8_t recoveryRefreshFailures = 0;
uint32_t recoveryRetryAt = 0;
const esp_partition_t* recoveryWritePartition = nullptr;
bool recoveryOtaActive = false;
size_t recoveryBytesWritten = 0;
mbedtls_sha256_context recoveryShaContext;
bool recoveryShaActive = false;
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
                          config.wifiConfigured && config.wifiSsid[0] != '\0';
  snapshot.wifiConfigured = configured;
  if (configured && output != nullptr) *output = config;
  return configured;
}

void savePersisted() {
  Preferences preferences;
  if (!preferences.begin("studio", false)) return;
  preferences.putBytes("fw_update", &persisted, sizeof(persisted));
  preferences.putULong64("fw_rec_seq", installedRecoverySequence);
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
  installedRecoverySequence = preferences.getULong64("fw_rec_seq", 0);
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

void cleanupRecoveryWrite(bool abortWrite) {
  (void)abortWrite;
  recoveryWritePartition = nullptr;
  recoveryOtaActive = false;
  if (recoveryShaActive) {
    mbedtls_sha256_free(&recoveryShaContext);
    recoveryShaActive = false;
  }
  recoveryBytesWritten = 0;
}

void releaseWifi() {
  cleanupClient();
  if (!ownsWifi) return;
  // disconnect(true) can deinitialize the ESP32-C3 driver before the tcpip
  // task finishes its DHCP release, leaving that task with a dead Wi-Fi queue.
  esp_netif_t* station = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
  if (station != nullptr) esp_netif_dhcpc_stop(station);
  WiFi.disconnect(false, false);
  ownsWifi = false;
  wifiShutdownPending = true;
  wifiShutdownStarted = millis();
}

void finishWifiShutdown(uint32_t now) {
  if (!wifiShutdownPending || now - wifiShutdownStarted < kWifiShutdownSettleMs) return;
  if (WiFi.status() == WL_CONNECTED &&
      now - wifiShutdownStarted < kWifiShutdownTimeoutMs) return;
  WiFi.mode(WIFI_OFF);
  wifiShutdownPending = false;
}

void fail(const char* message) {
  const bool checkFailure = true;
  timeSyncStarted = false;
  releaseWifi();
  policy.checked(millis(), false);
  operationKind = OperationKind::None;
  if (checkFailure) recordCheckResult(message);
  setMessage(Status::Failed, message);
}

void scheduleRecoveryRetry(const char* message, bool retryable) {
  const bool recoveryWasBeingWritten = recoveryOtaActive;
  cleanupClient();
  cleanupRecoveryWrite(true);
  if (recoveryWasBeingWritten) snapshot.recoveryAvailable = false;
  releaseWifi();
  operationKind = OperationKind::None;
  if (retryable) {
    static constexpr uint32_t delays[] = {
        60UL * 60UL * 1000UL,
        6UL * 60UL * 60UL * 1000UL,
        24UL * 60UL * 60UL * 1000UL,
    };
    const size_t index = std::min<size_t>(recoveryRefreshFailures,
                                          sizeof(delays) / sizeof(delays[0]) - 1);
    recoveryRetryAt = millis() + delays[index];
    if (recoveryRefreshFailures < 0xff) ++recoveryRefreshFailures;
  } else {
    recoveryRefreshPending = false;
    snapshot.recoveryUpdatePending = false;
  }
  recordCheckResult(message);
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
  } else if (transferKind == TransferKind::RecoveryImage) {
    if (!recoveryOtaActive || recoveryBytesWritten + length > expectedRecoveryImageSize ||
        (recoveryBytesWritten == 0 && bytes[0] != 0xE9) ||
        mbedtls_sha256_update_ret(&recoveryShaContext, bytes, length) != 0 ||
        recoveryWritePartition == nullptr ||
        esp_partition_write(recoveryWritePartition, recoveryBytesWritten,
                            bytes, length) != ESP_OK) {
      return ESP_FAIL;
    }
    recoveryBytesWritten += length;
    snapshot.progressPercent = static_cast<uint8_t>(
        std::min<size_t>(99, recoveryBytesWritten * 100 / expectedRecoveryImageSize));
  }
  return ESP_OK;
}

bool startTransfer(const char* url, TransferKind kind) {
  cleanupClient();
  esp_http_client_config_t config = {};
  config.url = url;
  config.cert_pem = kUpdateTrustRoots;
  config.user_agent = "Bleep-Firmware-Updater/1";
  config.timeout_ms = 5000;
  config.max_redirection_count = 4;
  config.event_handler = onHttpEvent;
  config.is_async = true;
  // GitHub release responses carry large security headers, and asset redirects
  // use long signed URLs. ESP-IDF configures RX and TX independently; leaving
  // TX at its 512-byte default makes the redirected request line overflow.
  config.buffer_size = kHttpBufferSize;
  config.buffer_size_tx = kHttpBufferSize;
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

const char* channelName(uint8_t channel) {
  return channel == static_cast<uint8_t>(studio::FirmwareUpdateChannel::Development)
      ? "development" : "stable";
}

const char* channelKeyId(uint8_t channel) {
  return channel == static_cast<uint8_t>(studio::FirmwareUpdateChannel::Development)
      ? firmware_update_keys::kDevelopmentKeyId : firmware_update_keys::kStableKeyId;
}

const char* channelPublicKey(uint8_t channel) {
  return channel == static_cast<uint8_t>(studio::FirmwareUpdateChannel::Development)
      ? firmware_update_keys::kDevelopmentPublicKey
      : firmware_update_keys::kStablePublicKey;
}

RecoveryManifestResult parseRecoveryManifest(
    const studio::RecoveryRecord& record) {
  if (record.manifestLength == 0 || record.manifestLength >= sizeof(manifest) ||
      record.signatureLength == 0 || record.signatureLength > sizeof(signature) ||
      record.channel > static_cast<uint8_t>(studio::FirmwareUpdateChannel::Development)) {
    return RecoveryManifestResult::None;
  }
  std::memcpy(manifest, record.manifest, record.manifestLength);
  manifestLength = record.manifestLength;
  manifest[manifestLength] = '\0';
  std::memcpy(signature, record.signature, record.signatureLength);
  signatureLength = record.signatureLength;
  if (!verifyManifest(channelPublicKey(record.channel))) {
    return RecoveryManifestResult::Invalid;
  }
  JsonDocument document;
  if (deserializeJson(document, manifest, manifestLength) != DeserializationError::Ok) {
    return RecoveryManifestResult::Invalid;
  }
  if (std::strcmp(document["channel"] | "", channelName(record.channel)) != 0 ||
      std::strcmp(document["key_id"] | "", channelKeyId(record.channel)) != 0 ||
      std::strcmp(document["hardware"] | "", build_info::kHardware) != 0 ||
      std::strcmp(document["profile"] | "", "bleep") != 0 ||
      (document["schema"] | 0) != 1 ||
      (document["partition_schema"] | 0) != 2 ||
      (document["recovery_schema"] | 0) != 1 ||
      (document["release_sequence"] | 0ULL) != record.releaseSequence ||
      record.releaseSequence != build_info::kReleaseSequence) {
    return RecoveryManifestResult::Invalid;
  }
  if (!document["recovery_sequence"].is<uint64_t>()) {
    return RecoveryManifestResult::None;
  }
  const uint64_t sequence = document["recovery_sequence"] | 0ULL;
  const size_t imageSize = document["recovery_image_size"] | 0U;
  const char* url = document["recovery_payload_url"] | "";
  if (sequence == 0 || imageSize == 0 || imageSize > kMaximumRecoveryImageSize ||
      std::strncmp(url, "https://github.com/nethunter/bleep/releases/download/",
                   std::strlen("https://github.com/nethunter/bleep/releases/download/")) != 0 ||
      !decodeSha(document["recovery_sha256"] | "", expectedRecoverySha)) {
    return RecoveryManifestResult::Invalid;
  }
  if (sequence <= installedRecoverySequence) return RecoveryManifestResult::Current;
  expectedRecoverySequence = sequence;
  expectedRecoveryImageSize = imageSize;
  std::strncpy(recoveryPayloadUrl, url, sizeof(recoveryPayloadUrl) - 1);
  recoveryPayloadUrl[sizeof(recoveryPayloadUrl) - 1] = '\0';
  return RecoveryManifestResult::Pending;
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

bool startRecoveryRequest() {
  const esp_partition_t* recovery = esp_partition_find_first(
      ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, "recovery");
  if (recovery == nullptr || recovery == esp_ota_get_running_partition() ||
      expectedRecoveryImageSize == 0 ||
      expectedRecoveryImageSize > recovery->size) return false;
  cleanupRecoveryWrite(true);
  if (!startTransfer(recoveryPayloadUrl, TransferKind::RecoveryImage)) return false;
  constexpr size_t kFlashSectorSize = 4096;
  const size_t eraseSize = (expectedRecoveryImageSize + kFlashSectorSize - 1) &
                           ~(kFlashSectorSize - 1);
  if (eraseSize > recovery->size ||
      esp_partition_erase_range(recovery, 0, eraseSize) != ESP_OK) {
    cleanupClient();
    return false;
  }
  recoveryWritePartition = recovery;
  recoveryOtaActive = true;
  mbedtls_sha256_init(&recoveryShaContext);
  recoveryShaActive = true;
  if (mbedtls_sha256_starts_ret(&recoveryShaContext, 0) != 0) {
    cleanupClient();
    cleanupRecoveryWrite(true);
    return false;
  }
  recoveryBytesWritten = 0;
  snapshot.progressPercent = 0;
  setMessage(Status::Downloading, "Updating recovery");
  return true;
}

void finishRecoveryRequest() {
  uint8_t digest[32] = {};
  if (!recoveryOtaActive || recoveryBytesWritten != expectedRecoveryImageSize ||
      !recoveryShaActive ||
      mbedtls_sha256_finish_ret(&recoveryShaContext, digest) != 0 ||
      std::memcmp(digest, expectedRecoverySha, sizeof(digest)) != 0) {
    scheduleRecoveryRetry("Recovery image verification failed", false);
    return;
  }
  mbedtls_sha256_free(&recoveryShaContext);
  recoveryShaActive = false;
  const esp_partition_t* recovery = recoveryWritePartition;
  recoveryWritePartition = nullptr;
  recoveryOtaActive = false;
  esp_image_metadata_t metadata = {};
  const esp_partition_pos_t position = {
      recovery == nullptr ? 0U : recovery->address,
      recovery == nullptr ? 0U : recovery->size,
  };
  if (recovery == nullptr ||
      esp_image_verify(ESP_IMAGE_VERIFY, &position, &metadata) != ESP_OK) {
    recoveryBytesWritten = 0;
    snapshot.recoveryAvailable = false;
    scheduleRecoveryRetry("Recovery image was not bootable", false);
    return;
  }
  recoveryBytesWritten = 0;
  esp_app_desc_t description = {};
  if (esp_ota_get_partition_description(recovery, &description) != ESP_OK) {
    snapshot.recoveryAvailable = false;
    scheduleRecoveryRetry("Recovery validation failed", false);
    return;
  }
  installedRecoverySequence = expectedRecoverySequence;
  snapshot.recoveryAvailable = true;
  savePersisted();
  studio::PartitionRecoveryJournalBackend backend;
  studio::RecoveryJournal journal(backend);
  journal.clear();
  recoveryRefreshPending = false;
  snapshot.recoveryUpdatePending = false;
  recoveryRefreshFailures = 0;
  operationKind = OperationKind::None;
  snapshot.progressPercent = 100;
  recordCheckResult("Firmware and recovery updated");
  releaseWifi();
  setMessage(Status::Idle, "Firmware and recovery updated");
}

void prepareRecoveryRefresh() {
  studio::PartitionRecoveryJournalBackend backend;
  studio::RecoveryJournal journal(backend);
  std::unique_ptr<studio::RecoveryRecord> record(
      new (std::nothrow) studio::RecoveryRecord());
  if (!record || !journal.load(*record)) return;
  if (record->operation != studio::RecoveryOperation::InstallRequested &&
      record->operation != studio::RecoveryOperation::ResetComplete) return;
  const RecoveryManifestResult result = parseRecoveryManifest(*record);
  if (result == RecoveryManifestResult::Pending) {
    recoveryRefreshPending = true;
    recoveryRetryAt = millis();
    snapshot.recoveryUpdatePending = true;
    setMessage(Status::Deferred, "Finishing recovery update");
    return;
  }
  journal.clear();
  snapshot.recoveryUpdatePending = false;
  if (result == RecoveryManifestResult::Invalid) {
    recordCheckResult("Recovery metadata invalid");
    setMessage(Status::Failed, "Recovery metadata invalid");
  }
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
  operationKind = OperationKind::None;
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
    if (finished == TransferKind::RecoveryImage) {
      scheduleRecoveryRetry("Recovery redirect was not allowed", false);
    } else {
      fail("Update redirect was not allowed");
    }
    return;
  }
  if (code != 200) {
    if (finished == TransferKind::RecoveryImage) {
      scheduleRecoveryRetry(code == 404 ? "Recovery image was not published"
                                         : "Recovery server error", true);
    } else if (code == 404) {
      fail("No signed release published");
    } else {
      fail("Update server error");
    }
    return;
  }
  if (finished == TransferKind::RecoveryImage) {
    finishRecoveryRequest();
  } else if (finished == TransferKind::Manifest) {
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
      operationKind = OperationKind::None;
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
  // The reset itself stops Wi-Fi. Deinitializing it here can race the tcpip
  // task while it is still processing disconnect and DHCP events.
  cleanupClient();
  ownsWifi = false;
  wifiShutdownPending = false;
  ESP.restart();
  return true;
}

bool recoveryPartitionValid() {
  const esp_partition_t* recovery = esp_partition_find_first(
      ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, "recovery");
  esp_app_desc_t description = {};
  return recovery != nullptr &&
         esp_ota_get_partition_description(recovery, &description) == ESP_OK;
}

}  // namespace

void FirmwareUpdateService::begin() {
  policy.begin(millis());
  snapshot = {};
  operationKind = OperationKind::None;
  recoveryRefreshPending = false;
  recoveryRefreshFailures = 0;
  cleanupRecoveryWrite(true);
  loadPersisted();
  wifiConfigured();
  const esp_partition_t* running = esp_ota_get_running_partition();
  esp_ota_img_states_t state;
  bootPending = running != nullptr &&
                esp_ota_get_state_partition(running, &state) == ESP_OK &&
                state == ESP_OTA_IMG_PENDING_VERIFY;
  bootValidationStarted = millis();
  snapshot.recoveryAvailable = recoveryPartitionValid();
  if (!bootPending) prepareRecoveryRefresh();
}

void FirmwareUpdateService::loop() {
  const uint32_t now = millis();
  finishWifiShutdown(now);
  if (bootPending && now - bootValidationStarted >= kBootValidationMs) {
    if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) {
      studio::PartitionRecoveryJournalBackend backend;
      studio::RecoveryJournal journal(backend);
      std::unique_ptr<studio::RecoveryRecord> completed(
          new (std::nothrow) studio::RecoveryRecord());
      if (completed && journal.load(*completed) &&
          completed->releaseSequence > persisted.installedSequence) {
        persisted.installedSequence = completed->releaseSequence;
        savePersisted();
      }
      bootPending = false;
      prepareRecoveryRefresh();
    }
  }
  if (snapshot.status == Status::Connecting) {
    if (operationKind == OperationKind::RecoveryRefresh && !runtimeIdle) {
      scheduleRecoveryRetry("Recovery update deferred", true);
      return;
    }
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
      if (operationKind == OperationKind::RecoveryRefresh) {
        if (!startRecoveryRequest()) {
          scheduleRecoveryRetry("Could not start recovery update", true);
        }
      } else {
        startManifestRequest();
      }
    } else if (now - operationStarted >= kConnectTimeoutMs) {
      if (operationKind == OperationKind::RecoveryRefresh) {
        scheduleRecoveryRetry("Recovery Wi-Fi timed out", true);
      } else {
        fail("Wi-Fi connection timed out");
      }
    }
    return;
  }
  if (client != nullptr) {
    if (!runtimeIdle && operationKind != OperationKind::RecoveryRefresh) {
      policy.deferStartup();
      releaseWifi();
      operationKind = OperationKind::None;
      setMessage(Status::Deferred, "Update check deferred");
      return;
    }
    const esp_err_t result = esp_http_client_perform(client);
    if (result == ESP_OK) handleCompletedTransfer();
    else if (result != ESP_ERR_HTTP_EAGAIN && result != ESP_ERR_HTTP_CONNECTING) {
      Serial.printf("[fw-update] transfer failed: %s (0x%x)\n",
                    esp_err_to_name(result), static_cast<unsigned>(result));
      if (operationKind == OperationKind::RecoveryRefresh) {
        scheduleRecoveryRetry("Recovery transfer failed", true);
      } else {
        fail("Network transfer failed");
      }
    } else if (now - operationStarted >=
               (operationKind == OperationKind::RecoveryRefresh
                    ? kRecoveryRequestTimeoutMs : kRequestTimeoutMs)) {
      if (operationKind == OperationKind::RecoveryRefresh) {
        scheduleRecoveryRetry("Recovery request timed out", true);
      } else {
        fail("Update request timed out");
      }
    }
    return;
  }
  const bool heapSafe = ESP.getFreeHeap() >= kMinimumFreeHeap &&
      heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) >= kMinimumLargestBlock;
  if (recoveryRefreshPending) {
    snapshot.recoveryUpdatePending = true;
    if (!bootPending && runtimeIdle && heapSafe &&
        static_cast<int32_t>(now - recoveryRetryAt) >= 0) {
      operationKind = OperationKind::RecoveryRefresh;
      if (!beginWifi()) {
        scheduleRecoveryRetry("Recovery Wi-Fi unavailable", true);
      }
    }
    return;
  }
  if (snapshot.status == Status::Available ||
      snapshot.status == Status::RebootPending) return;
  if (!runtimeIdle) policy.deferStartup();
  if (policy.immediateRequested() && runtimeIdle && !heapSafe) {
    setMessage(Status::Deferred, "Waiting for available memory");
  }
  const bool checkEligible = runtimeIdle && heapSafe &&
      (automaticEligible || policy.immediateRequested());
  if (policy.shouldCheck(now, checkEligible, snapshot.wifiConfigured)) {
    operationKind = OperationKind::Check;
    if (!beginWifi()) {
      operationKind = OperationKind::None;
      policy.checked(now, false);
    }
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
  if (recoveryRefreshPending) {
    recoveryRetryAt = millis();
    setMessage(Status::Deferred, "Recovery update queued");
    return;
  }
  snapshot.disconnectRequired = false;
  policy.requestImmediate();
}

bool FirmwareUpdateService::installAvailable() {
  if (!snapshot.updateAvailable || payloadUrl[0] == '\0' ||
      expectedImageSize == 0 || manifestLength == 0 || signatureLength == 0) return false;
  std::unique_ptr<studio::RecoveryRecord> record(
      new (std::nothrow) studio::RecoveryRecord());
  if (!record) return false;
  studio::PartitionRecoveryJournalBackend backend;
  studio::RecoveryJournal journal(backend);
  journal.load(*record);
  record->operation = studio::RecoveryOperation::InstallRequested;
  record->channel = static_cast<uint8_t>(
      studio::panelSettings().get().firmwareUpdateChannel);
  record->releaseSequence = snapshot.releaseSequence;
  record->manifestLength = manifestLength;
  record->signatureLength = signatureLength;
  std::memcpy(record->manifest, manifest, manifestLength);
  std::memcpy(record->signature, signature, signatureLength);
  if (!journal.save(*record)) return false;
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
  std::unique_ptr<studio::RecoveryRecord> record(
      new (std::nothrow) studio::RecoveryRecord());
  if (!record) {
    setMessage(Status::Failed, "Not enough memory for recovery");
    return false;
  }
  journal.load(*record);
  *record = {};
  record->operation = studio::RecoveryOperation::RecoveryModeRequested;
  if (!journal.save(*record)) {
    setMessage(Status::Failed, "Could not prepare recovery");
    return false;
  }
  setMessage(Status::RebootPending, "Entering recovery");
  if (selectRecoveryAndRestart()) return true;
  setMessage(Status::Failed, "Recovery handoff failed");
  return false;
}

bool FirmwareUpdateService::requestFactoryReset() {
  std::unique_ptr<studio::RecoveryRecord> record(
      new (std::nothrow) studio::RecoveryRecord());
  if (!record) return false;
  studio::PartitionRecoveryJournalBackend backend;
  studio::RecoveryJournal journal(backend);
  journal.load(*record);
  *record = {};
  record->operation = studio::RecoveryOperation::FactoryResetRequested;
  record->channel = static_cast<uint8_t>(studio::FirmwareUpdateChannel::Stable);
  return journal.save(*record) && selectRecoveryAndRestart();
}

Snapshot FirmwareUpdateService::status() const { return snapshot; }

FirmwareUpdateService& service() {
  static FirmwareUpdateService instance;
  return instance;
}

}  // namespace firmware_update

#endif
