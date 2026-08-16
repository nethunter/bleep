#define LGFX_USE_V1

#include <Arduino.h>
#include <SPI.h>
#include <WiFi.h>
#include <Wire.h>
#include <esp_http_client.h>
#include <esp_image_format.h>
#include <esp_ota_ops.h>
#include <mbedtls/pk.h>
#include <mbedtls/sha256.h>
#include <nvs_flash.h>
#include <time.h>

#include "core/home_assistant_config.h"
#include "core/partition_recovery_backend.h"
#include "core/preferences_store.h"
#include "core/recovery_journal.h"
#include "core/recovery_touch_gate.h"
#include "firmware_update_keys.h"

namespace {

constexpr int kHttpBufferSize = 8192;

constexpr char kStableManifestUrl[] =
    "https://github.com/nethunter/bleep/releases/latest/download/bleep-update.json";
constexpr size_t kMaximumImageSize = 0x2C0000;
constexpr uint32_t kConnectTimeoutMs = 15000;
constexpr char kRootCa[] = R"CERT(-----BEGIN CERTIFICATE-----
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

namespace board {
constexpr int lcdDc = 2, lcdCs = 10, lcdSck = 6, lcdMosi = 7;
constexpr int i2cSda = 4, i2cScl = 5, touchInt = 0;
constexpr uint8_t ioAddr = 0x43, touchAddr = 0x15;
constexpr uint8_t directionReg = 0x03, outputReg = 0x05, highZReg = 0x07;
}

constexpr uint16_t TFT_BLACK = 0x0000;
constexpr uint16_t TFT_WHITE = 0xFFFF;
constexpr uint16_t TFT_CYAN = 0x07FF;
constexpr uint16_t TFT_DARKCYAN = 0x03EF;
constexpr uint16_t TFT_DARKGREEN = 0x03E0;
constexpr uint16_t TFT_DARKGREY = 0x7BEF;
constexpr uint16_t TFT_MAROON = 0x7800;
constexpr int middle_center = 0;

class RecoveryDisplay {
 public:
  void init() {
    pinMode(board::lcdDc, OUTPUT);
    pinMode(board::lcdCs, OUTPUT);
    digitalWrite(board::lcdCs, HIGH);
    SPI.begin(board::lcdSck, -1, board::lcdMosi, board::lcdCs);

    static constexpr uint8_t initCommands[] = {
      0xEF,0, 0xEB,1,0x14, 0xFE,0, 0xEF,0, 0xEB,1,0x14,
      0x84,1,0x40, 0x85,1,0xFF, 0x86,1,0xFF, 0x87,1,0xFF,
      0x8E,1,0xFF, 0x8F,1,0xFF, 0x88,1,0x0A, 0x89,1,0x21,
      0x8A,1,0x00, 0x8B,1,0x80, 0x8C,1,0x01, 0x8D,1,0x01,
      0xB6,2,0x00,0x20, 0x90,4,0x08,0x08,0x08,0x08,
      0xBD,1,0x06, 0xBC,1,0x00, 0xFF,3,0x60,0x01,0x04,
      0xC3,1,0x13, 0xC4,1,0x13, 0xC9,1,0x22, 0xBE,1,0x11,
      0xE1,2,0x10,0x0E, 0xDF,3,0x21,0x0C,0x02,
      0xF0,6,0x45,0x09,0x08,0x08,0x26,0x2A,
      0xF1,6,0x43,0x70,0x72,0x36,0x37,0x6F,
      0xF2,6,0x45,0x09,0x08,0x08,0x26,0x2A,
      0xF3,6,0x43,0x70,0x72,0x36,0x37,0x6F,
      0xED,2,0x1B,0x0B, 0xAE,1,0x77, 0xCD,1,0x63,
      0x70,9,0x07,0x07,0x04,0x0E,0x0F,0x09,0x07,0x08,0x03,
      0xE8,1,0x34,
      0x62,12,0x18,0x0D,0x71,0xED,0x70,0x70,0x18,0x0F,0x71,0xEF,0x70,0x70,
      0x63,12,0x18,0x11,0x71,0xF1,0x70,0x70,0x18,0x13,0x71,0xF3,0x70,0x70,
      0x64,7,0x28,0x29,0xF1,0x01,0xF1,0x00,0x07,
      0x66,10,0x3C,0x00,0xCD,0x67,0x45,0x45,0x10,0x00,0x00,0x00,
      0x67,10,0x00,0x3C,0x00,0x00,0x00,0x01,0x54,0x10,0x32,0x98,
      0x74,7,0x10,0x68,0x80,0x00,0x00,0x4E,0x00,
      0x98,2,0x3E,0x07, 0x35,1,0x00,
      0x11,0x80,120, 0x29,0, 0xFF,0xFF,
    };
    const uint8_t* cursor = initCommands;
    while (!(cursor[0] == 0xFF && cursor[1] == 0xFF)) {
      const uint8_t value = *cursor++;
      uint8_t length = *cursor++;
      const bool delayed = (length & 0x80U) != 0;
      length &= 0x7FU;
      command(value, cursor, length);
      cursor += length;
      if (delayed) delay(*cursor++);
    }
    const uint8_t pixelFormat = 0x55;
    command(0x3A, &pixelFormat, 1);
    const uint8_t orientation = DISPLAY_RGB_ORDER != 0 ? 0x00 : 0x08;
    command(0x36, &orientation, 1);
    command(0x21);
  }
  void setRotation(int) {}
  void setTextDatum(int) {}
  void setTextSize(int) {}
  void setFont(const void*) {}
  void setTextColor(uint16_t foreground, uint16_t background) {
    foreground_ = foreground;
    background_ = background;
  }
  void fillScreen(uint16_t color) { fillRect(0, 0, 240, 240, color); }
  void fillRoundRect(int x, int y, int width, int height, int, uint16_t color) {
    fillRect(x, y, width, height, color);
  }
  void drawRoundRect(int x, int y, int width, int height, int, uint16_t color) {
    fillRect(x, y, width, 1, color);
    fillRect(x, y + height - 1, width, 1, color);
    fillRect(x, y, 1, height, color);
    fillRect(x + width - 1, y, 1, height, color);
  }
  void drawString(const char* text, int centerX, int centerY) {
    const int length = static_cast<int>(strlen(text));
    int x = centerX - length * 3;
    for (int i = 0; text[i] != '\0'; ++i, x += 6) drawChar(x, centerY - 4, text[i]);
  }

 private:
  static const uint8_t* glyph(char value) {
    if (value >= 'a' && value <= 'z') value -= 'a' - 'A';
    static constexpr uint8_t letters[][5] = {
      {0x7E,0x11,0x11,0x11,0x7E}, {0x7F,0x49,0x49,0x49,0x36},
      {0x3E,0x41,0x41,0x41,0x22}, {0x7F,0x41,0x41,0x22,0x1C},
      {0x7F,0x49,0x49,0x49,0x41}, {0x7F,0x09,0x09,0x09,0x01},
      {0x3E,0x41,0x49,0x49,0x7A}, {0x7F,0x08,0x08,0x08,0x7F},
      {0x00,0x41,0x7F,0x41,0x00}, {0x20,0x40,0x41,0x3F,0x01},
      {0x7F,0x08,0x14,0x22,0x41}, {0x7F,0x40,0x40,0x40,0x40},
      {0x7F,0x02,0x0C,0x02,0x7F}, {0x7F,0x04,0x08,0x10,0x7F},
      {0x3E,0x41,0x41,0x41,0x3E}, {0x7F,0x09,0x09,0x09,0x06},
      {0x3E,0x41,0x51,0x21,0x5E}, {0x7F,0x09,0x19,0x29,0x46},
      {0x46,0x49,0x49,0x49,0x31}, {0x01,0x01,0x7F,0x01,0x01},
      {0x3F,0x40,0x40,0x40,0x3F}, {0x1F,0x20,0x40,0x20,0x1F},
      {0x3F,0x40,0x38,0x40,0x3F}, {0x63,0x14,0x08,0x14,0x63},
      {0x07,0x08,0x70,0x08,0x07}, {0x61,0x51,0x49,0x45,0x43}};
    static constexpr uint8_t digits[][5] = {
      {0x3E,0x51,0x49,0x45,0x3E}, {0x00,0x42,0x7F,0x40,0x00},
      {0x42,0x61,0x51,0x49,0x46}, {0x21,0x41,0x45,0x4B,0x31},
      {0x18,0x14,0x12,0x7F,0x10}, {0x27,0x45,0x45,0x45,0x39},
      {0x3C,0x4A,0x49,0x49,0x30}, {0x01,0x71,0x09,0x05,0x03},
      {0x36,0x49,0x49,0x49,0x36}, {0x06,0x49,0x49,0x29,0x1E}};
    static constexpr uint8_t space[] = {0,0,0,0,0};
    static constexpr uint8_t dash[] = {0x08,0x08,0x08,0x08,0x08};
    static constexpr uint8_t period[] = {0x00,0x60,0x60,0x00,0x00};
    static constexpr uint8_t colon[] = {0x00,0x36,0x36,0x00,0x00};
    static constexpr uint8_t slash[] = {0x20,0x10,0x08,0x04,0x02};
    static constexpr uint8_t leftParen[] = {0x00,0x1C,0x22,0x41,0x00};
    static constexpr uint8_t rightParen[] = {0x00,0x41,0x22,0x1C,0x00};
    static constexpr uint8_t percent[] = {0x63,0x13,0x08,0x64,0x63};
    if (value >= 'A' && value <= 'Z') return letters[value - 'A'];
    if (value >= '0' && value <= '9') return digits[value - '0'];
    if (value == '-') return dash;
    if (value == '.') return period;
    if (value == ':') return colon;
    if (value == '/') return slash;
    if (value == '(') return leftParen;
    if (value == ')') return rightParen;
    if (value == '%') return percent;
    return space;
  }
  void drawChar(int x, int y, char value) {
    const uint8_t* columns = glyph(value);
    fillRect(x, y, 6, 8, background_);
    for (int column = 0; column < 5; ++column) {
      for (int row = 0; row < 7; ++row) {
        if ((columns[column] & (1U << row)) != 0) {
          fillRect(x + column, y + row, 1, 1, foreground_);
        }
      }
    }
  }
  void command(uint8_t value, const uint8_t* data = nullptr, size_t length = 0) {
    SPI.beginTransaction(SPISettings(40000000, MSBFIRST, SPI_MODE0));
    digitalWrite(board::lcdCs, LOW);
    digitalWrite(board::lcdDc, LOW);
    SPI.transfer(value);
    digitalWrite(board::lcdDc, HIGH);
    for (size_t i = 0; i < length; ++i) SPI.transfer(data[i]);
    digitalWrite(board::lcdCs, HIGH);
    SPI.endTransaction();
  }
  void fillRect(int x, int y, int width, int height, uint16_t color) {
    if (width <= 0 || height <= 0) return;
    const uint8_t columns[] = {
      static_cast<uint8_t>(x >> 8), static_cast<uint8_t>(x),
      static_cast<uint8_t>((x + width - 1) >> 8), static_cast<uint8_t>(x + width - 1)};
    const uint8_t rows[] = {
      static_cast<uint8_t>(y >> 8), static_cast<uint8_t>(y),
      static_cast<uint8_t>((y + height - 1) >> 8), static_cast<uint8_t>(y + height - 1)};
    command(0x2A, columns, sizeof(columns));
    command(0x2B, rows, sizeof(rows));
    SPI.beginTransaction(SPISettings(40000000, MSBFIRST, SPI_MODE0));
    digitalWrite(board::lcdCs, LOW);
    digitalWrite(board::lcdDc, LOW);
    SPI.transfer(0x2C);
    digitalWrite(board::lcdDc, HIGH);
    const uint8_t high = color >> 8;
    const uint8_t low = color;
    for (int count = width * height; count > 0; --count) {
      SPI.transfer(high);
      SPI.transfer(low);
    }
    digitalWrite(board::lcdCs, HIGH);
    SPI.endTransaction();
  }

  uint16_t foreground_ = TFT_WHITE;
  uint16_t background_ = TFT_BLACK;
};

RecoveryDisplay display;
uint8_t ioState = 0;
studio::PartitionRecoveryJournalBackend journalBackend;
studio::RecoveryJournal journal(journalBackend);
studio::RecoveryRecord request;
bool touchDown = false;
uint32_t factoryHoldStarted = 0;
studio::RecoveryTouchGate touchGate;

bool i2cWrite(uint8_t address, uint8_t reg, uint8_t value) {
  Wire.beginTransmission(address); Wire.write(reg); Wire.write(value);
  return Wire.endTransmission() == 0;
}

bool i2cRead(uint8_t address, uint8_t reg, uint8_t* output, size_t length) {
  Wire.beginTransmission(address); Wire.write(reg);
  if (Wire.endTransmission(false) != 0 ||
      Wire.requestFrom(static_cast<int>(address), static_cast<int>(length)) != length) return false;
  for (size_t i = 0; i < length; ++i) output[i] = Wire.read();
  return true;
}

void ioPin(uint8_t pin, bool high) {
  if (high) ioState |= 1U << pin; else ioState &= ~(1U << pin);
  i2cWrite(board::ioAddr, board::outputReg, ioState);
}

void initPanel() {
  Wire.begin(board::i2cSda, board::i2cScl, 400000);
  constexpr uint8_t outputs = 0x1F;
  i2cWrite(board::ioAddr, board::outputReg, 0);
  i2cWrite(board::ioAddr, board::directionReg, outputs);
  i2cWrite(board::ioAddr, board::highZReg, static_cast<uint8_t>(~outputs));
  ioPin(3, true); delay(80); ioPin(4, true); delay(80); ioPin(2, true);
  pinMode(board::touchInt, OUTPUT); digitalWrite(board::touchInt, HIGH); delay(1);
  digitalWrite(board::touchInt, LOW); delay(1);
  i2cWrite(board::touchAddr, 0xFE, 0xFF);
  display.init(); display.setRotation(0); display.fillScreen(TFT_BLACK);
  display.setTextDatum(middle_center); display.setTextColor(TFT_WHITE, TFT_BLACK);
}

bool readTouch(uint16_t& x, uint16_t& y) {
  uint8_t points = 0, data[4] = {};
  if (!i2cRead(board::touchAddr, 0x02, &points, 1) || points == 0 ||
      !i2cRead(board::touchAddr, 0x03, data, sizeof(data))) return false;
  x = ((data[0] & 0x0F) << 8) | data[1]; y = ((data[2] & 0x0F) << 8) | data[3];
  return x < 240 && y < 240;
}

void button(int y, const char* label, uint16_t color) {
  display.fillRoundRect(42, y, 156, 26, 7, color);
  display.setTextColor(TFT_WHITE, color); display.drawString(label, 120, y + 13);
  display.setTextColor(TFT_WHITE, TFT_BLACK);
}

void screen(const char* title, const char* detail = nullptr, int progress = -1) {
  display.fillScreen(TFT_BLACK); display.setTextSize(1); display.setFont(nullptr);
  display.drawString("BLE(E)P RECOVERY", 120, 38);
  display.setTextColor(TFT_CYAN, TFT_BLACK); display.drawString(title, 120, 72);
  display.setTextColor(TFT_WHITE, TFT_BLACK);
  if (detail != nullptr) display.drawString(detail, 120, 100);
  if (progress >= 0) {
    display.drawRoundRect(42, 120, 156, 12, 4, TFT_DARKGREY);
    display.fillRoundRect(44, 122, progress * 152 / 100, 8, 3, TFT_CYAN);
  }
}

const esp_partition_t* mainPartition() {
  return esp_partition_find_first(ESP_PARTITION_TYPE_APP,
                                  ESP_PARTITION_SUBTYPE_APP_OTA_0, "ota_0");
}

bool mainValid() {
  esp_app_desc_t description;
  const esp_partition_t* target = mainPartition();
  if (target == nullptr ||
      esp_ota_get_partition_description(target, &description) != ESP_OK) return false;
  esp_ota_img_states_t state;
  return esp_ota_get_state_partition(target, &state) != ESP_OK ||
      (state != ESP_OTA_IMG_INVALID && state != ESP_OTA_IMG_ABORTED);
}

void readyScreen(const char* error = nullptr) {
  screen(error == nullptr ? "READY" : "RECOVERY FAILED", error);
  const bool retry = request.operation == studio::RecoveryOperation::FactoryResetRequested ||
      (request.operation == studio::RecoveryOperation::InstallRequested &&
       request.manifestLength > 0);
  const int gap = retry ? 28 : 30;
  int y = retry ? 86 : 110;
  if (mainValid()) { button(y, "BOOT FIRMWARE", TFT_DARKGREEN); y += gap; }
  if (retry) { button(y, "RETRY REQUEST", TFT_MAROON); y += gap; }
  button(y, "INSTALL STABLE", TFT_DARKCYAN); y += gap;
  button(y, "FACTORY RESET", TFT_MAROON);
}

bool loadWifi(studio::HomeAssistantConfig& config) {
  studio::PreferencesHomeAssistantBackend backend;
  studio::HomeAssistantConfigStore store(backend);
  return store.load(config) != studio::ConfigLoadStatus::Corrupt && config.wifiSsid[0];
}

bool connectWifi() {
  studio::HomeAssistantConfig config;
  if (!loadWifi(config)) return false;
  screen("CONNECTING", config.wifiSsid);
  WiFi.mode(WIFI_STA); WiFi.begin(config.wifiSsid, config.wifiPassword);
  const uint32_t started = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - started < kConnectTimeoutMs) delay(25);
  if (WiFi.status() != WL_CONNECTED) return false;
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  const uint32_t clockStarted = millis();
  while (time(nullptr) < 1700000000 && millis() - clockStarted < kConnectTimeoutMs) delay(25);
  return time(nullptr) >= 1700000000;
}

void wifiOff() { WiFi.disconnect(true, false); WiFi.mode(WIFI_OFF); }

bool allowedUrl(const char* url) {
  static const char* const prefixes[] = {
      "https://github.com/nethunter/bleep/releases/",
      "https://release-assets.githubusercontent.com/",
      "https://objects.githubusercontent.com/",
      "https://github-releases.githubusercontent.com/",
  };
  for (const char* prefix : prefixes) {
    if (strncmp(url, prefix, strlen(prefix)) == 0) return true;
  }
  return false;
}

struct DownloadContext {
  uint8_t* output = nullptr;
  size_t capacity = 0;
  size_t received = 0;
  esp_ota_handle_t ota = 0;
  mbedtls_sha256_context* sha = nullptr;
  size_t expected = 0;
  bool failed = false;
  bool factoryReset = false;
  int lastProgress = -1;
};

esp_err_t onHttpEvent(esp_http_client_event_t* event) {
  if (event->event_id != HTTP_EVENT_ON_DATA || event->data_len <= 0) return ESP_OK;
  auto* context = static_cast<DownloadContext*>(event->user_data);
  const size_t length = static_cast<size_t>(event->data_len);
  const auto* bytes = static_cast<const uint8_t*>(event->data);
  if (context == nullptr || context->failed || context->received + length > context->capacity) {
    if (context != nullptr) context->failed = true;
    return ESP_FAIL;
  }
  if (context->ota != 0) {
    if ((context->received == 0 && bytes[0] != 0xE9) ||
        esp_ota_write(context->ota, bytes, length) != ESP_OK) {
      context->failed = true;
      return ESP_FAIL;
    }
    mbedtls_sha256_update_ret(context->sha, bytes, length);
    context->received += length;
    const int progress = static_cast<int>(context->received * 100 / context->expected);
    if (progress >= context->lastProgress + 5 || progress == 100) {
      context->lastProgress = progress;
      screen("INSTALLING", context->factoryReset ? "Factory reset" : "Firmware update",
             progress);
    }
  } else {
    memcpy(context->output + context->received, bytes, length);
    context->received += length;
  }
  return ESP_OK;
}

bool performRequest(const char* url, DownloadContext& context) {
  esp_http_client_config_t config = {};
  config.url = url;
  config.cert_pem = kRootCa;
  config.user_agent = "Bleep-Recovery/1";
  config.timeout_ms = 15000;
  config.max_redirection_count = 4;
  config.event_handler = onHttpEvent;
  config.user_data = &context;
  // GitHub release responses carry large security headers, and asset redirects
  // use long signed URLs. ESP-IDF configures RX and TX independently; leaving
  // TX at its 512-byte default makes the redirected request line overflow.
  config.buffer_size = kHttpBufferSize;
  config.buffer_size_tx = kHttpBufferSize;
  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (client == nullptr) return false;
  const esp_err_t result = esp_http_client_perform(client);
  char effectiveUrl[384] = {};
  const bool valid = result == ESP_OK && !context.failed &&
      esp_http_client_get_status_code(client) == 200 &&
      esp_http_client_get_content_length(client) == static_cast<int64_t>(context.received) &&
      esp_http_client_get_url(client, effectiveUrl, sizeof(effectiveUrl)) == ESP_OK &&
      allowedUrl(effectiveUrl);
  esp_http_client_cleanup(client);
  return valid;
}

bool fetch(const char* url, uint8_t* output, size_t capacity, size_t& length) {
  DownloadContext context;
  context.output = output;
  context.capacity = capacity;
  const bool valid = performRequest(url, context);
  length = context.received;
  return valid && length > 0;
}

struct ManifestInfo {
  bool development = false;
  uint64_t releaseSequence = 0;
  size_t imageSize = 0;
  char payloadUrl[320] = {};
  char sha256[65] = {};
};

bool jsonString(const char* json, const char* key, char* output, size_t capacity) {
  char pattern[48];
  const size_t keyLength = strlen(key);
  if (keyLength + 5 > sizeof(pattern)) return false;
  pattern[0] = '"';
  memcpy(pattern + 1, key, keyLength);
  memcpy(pattern + keyLength + 1, "\":\"", 4);
  const char* start = strstr(json, pattern);
  if (start == nullptr) return false;
  start += strlen(pattern);
  const char* end = strchr(start, '"');
  if (end == nullptr || static_cast<size_t>(end - start) >= capacity) return false;
  memcpy(output, start, end - start); output[end - start] = '\0';
  return strchr(output, '\\') == nullptr;
}

bool jsonUnsigned(const char* json, const char* key, uint64_t& output) {
  char pattern[48];
  const size_t keyLength = strlen(key);
  if (keyLength + 4 > sizeof(pattern)) return false;
  pattern[0] = '"';
  memcpy(pattern + 1, key, keyLength);
  memcpy(pattern + keyLength + 1, "\":", 3);
  const char* cursor = strstr(json, pattern);
  if (cursor == nullptr) return false;
  cursor += strlen(pattern); output = 0; size_t digits = 0;
  while (*cursor >= '0' && *cursor <= '9') {
    if (output > (UINT64_MAX - static_cast<uint64_t>(*cursor - '0')) / 10U) return false;
    output = output * 10U + static_cast<uint64_t>(*cursor++ - '0'); ++digits;
  }
  return digits > 0 && (*cursor == ',' || *cursor == '}');
}

bool verifyManifest(const studio::RecoveryRecord& record, ManifestInfo& info) {
  if (record.manifestLength == 0 || record.signatureLength == 0) return false;
  const bool development = record.channel == 1;
  const char* keyId = development ? firmware_update_keys::kDevelopmentKeyId
                                  : firmware_update_keys::kStableKeyId;
  const char* keyPem = development ? firmware_update_keys::kDevelopmentPublicKey
                                   : firmware_update_keys::kStablePublicKey;
  uint8_t digest[32];
  mbedtls_sha256_ret(reinterpret_cast<const uint8_t*>(record.manifest),
                     record.manifestLength, digest, 0);
  mbedtls_pk_context key; mbedtls_pk_init(&key);
  const int parsed = mbedtls_pk_parse_public_key(
      &key, reinterpret_cast<const uint8_t*>(keyPem), strlen(keyPem) + 1);
  const int verified = parsed == 0 ? mbedtls_pk_verify(
      &key, MBEDTLS_MD_SHA256, digest, sizeof(digest), record.signature,
      record.signatureLength) : parsed;
  mbedtls_pk_free(&key);
  if (verified != 0) return false;
  char channel[16] = {}, manifestKey[32] = {}, hardware[24] = {}, profile[16] = {};
  uint64_t schema = 0, partitionSchema = 0, recoverySchema = 0, imageSize = 0;
  if (!jsonString(record.manifest, "channel", channel, sizeof(channel)) ||
      !jsonString(record.manifest, "key_id", manifestKey, sizeof(manifestKey)) ||
      !jsonString(record.manifest, "hardware", hardware, sizeof(hardware)) ||
      !jsonString(record.manifest, "profile", profile, sizeof(profile)) ||
      !jsonString(record.manifest, "payload_url", info.payloadUrl, sizeof(info.payloadUrl)) ||
      !jsonString(record.manifest, "sha256", info.sha256, sizeof(info.sha256)) ||
      !jsonUnsigned(record.manifest, "schema", schema) ||
      !jsonUnsigned(record.manifest, "partition_schema", partitionSchema) ||
      !jsonUnsigned(record.manifest, "recovery_schema", recoverySchema) ||
      !jsonUnsigned(record.manifest, "release_sequence", info.releaseSequence) ||
      !jsonUnsigned(record.manifest, "image_size", imageSize)) return false;
  info.development = development;
  info.imageSize = static_cast<size_t>(imageSize);
  return strcmp(channel, development ? "development" : "stable") == 0 &&
      strcmp(manifestKey, keyId) == 0 && strcmp(hardware, "crowpanel-1.28") == 0 &&
      strcmp(profile, "bleep") == 0 && schema == 1 && partitionSchema == 2 &&
      recoverySchema == 1 && imageSize <= SIZE_MAX;
}

bool fetchStableRequest(studio::RecoveryRecord& record) {
  size_t manifestLength = 0, signatureLength = 0;
  if (!fetch(kStableManifestUrl, reinterpret_cast<uint8_t*>(record.manifest),
             sizeof(record.manifest), manifestLength)) return false;
  constexpr char signatureUrl[] =
      "https://github.com/nethunter/bleep/releases/latest/download/bleep-update.sig";
  if (!fetch(signatureUrl, record.signature, sizeof(record.signature), signatureLength)) return false;
  record.manifestLength = manifestLength; record.signatureLength = signatureLength;
  record.channel = 0;
  ManifestInfo info;
  if (!verifyManifest(record, info) || info.development) return false;
  record.releaseSequence = info.releaseSequence;
  return true;
}

bool decodeSha(const char* text, uint8_t output[32]) {
  if (text == nullptr || strlen(text) != 64) return false;
  for (size_t i = 0; i < 32; ++i) {
    const auto nibble = [](char value, uint8_t& result) {
      if (value >= '0' && value <= '9') result = value - '0';
      else if (value >= 'a' && value <= 'f') result = value - 'a' + 10;
      else if (value >= 'A' && value <= 'F') result = value - 'A' + 10;
      else return false;
      return true;
    };
    uint8_t high = 0, low = 0;
    if (!nibble(text[i * 2], high) || !nibble(text[i * 2 + 1], low)) return false;
    output[i] = static_cast<uint8_t>((high << 4) | low);
  }
  return true;
}

bool installRecord(studio::RecoveryRecord& record, bool fetchLatestStable,
                   bool factoryReset) {
  if (!connectWifi()) { wifiOff(); return false; }
  if (fetchLatestStable && !fetchStableRequest(record)) { wifiOff(); return false; }
  ManifestInfo info;
  if (!verifyManifest(record, info)) { wifiOff(); return false; }
  if (!fetchLatestStable && (record.releaseSequence == 0 ||
      info.releaseSequence != record.releaseSequence)) { wifiOff(); return false; }
  const size_t expected = info.imageSize;
  const char* url = info.payloadUrl;
  uint8_t expectedSha[32];
  if (expected == 0 || expected > kMaximumImageSize ||
      strncmp(url, "https://github.com/nethunter/bleep/releases/download/",
              strlen("https://github.com/nethunter/bleep/releases/download/")) != 0 ||
      !decodeSha(info.sha256, expectedSha)) { wifiOff(); return false; }

  const esp_partition_t* target = mainPartition();
  esp_ota_handle_t handle = 0;
  if (target == nullptr || esp_ota_begin(target, expected, &handle) != ESP_OK) {
    wifiOff(); return false;
  }
  mbedtls_sha256_context sha; mbedtls_sha256_init(&sha); mbedtls_sha256_starts_ret(&sha, 0);
  DownloadContext context;
  context.capacity = expected;
  context.ota = handle;
  context.sha = &sha;
  context.expected = expected;
  context.factoryReset = factoryReset;
  const bool valid = performRequest(url, context);
  uint8_t digest[32]; mbedtls_sha256_finish_ret(&sha, digest); mbedtls_sha256_free(&sha);
  wifiOff();
  if (!valid || context.received != expected || memcmp(digest, expectedSha, sizeof(digest)) != 0 ||
      esp_ota_end(handle) != ESP_OK) { esp_ota_abort(handle); return false; }

  if (factoryReset) {
    record.operation = studio::RecoveryOperation::ImageVerifiedResetPending;
    if (!journal.save(record)) return false;
    screen("RESETTING", "Erasing saved data");
    if (nvs_flash_erase() != ESP_OK) return false;
    record.operation = studio::RecoveryOperation::ResetComplete;
    if (!journal.save(record)) return false;
  }
  if (esp_ota_set_boot_partition(target) != ESP_OK) return false;
  screen("COMPLETE", "Restarting"); delay(300); ESP.restart();
  return true;
}

void startStable(bool factoryReset) {
  request = {};
  request.operation = factoryReset ? studio::RecoveryOperation::FactoryResetRequested
                                   : studio::RecoveryOperation::InstallRequested;
  if (!journal.save(request) || !installRecord(request, true, factoryReset)) {
    readyScreen(factoryReset ? "Factory reset failed" : "Stable install failed");
  }
}

void bootMain() {
  const esp_partition_t* target = mainPartition();
  Serial.printf("RECOVERY: boot main requested operation=%u\n",
                static_cast<unsigned>(request.operation));
  if (request.operation == studio::RecoveryOperation::RecoveryModeRequested &&
      !journal.clear()) {
    readyScreen("Could not leave recovery");
    return;
  }
  if (target != nullptr && mainValid() && esp_ota_set_boot_partition(target) == ESP_OK) {
    ESP.restart();
  }
  readyScreen("Main firmware invalid");
}

}  // namespace

void setup() {
  Serial.begin(115200); initPanel();
  const bool loaded = journal.load(request);
  Serial.printf("RECOVERY: journal loaded=%u operation=%u main_valid=%u\n",
                loaded ? 1U : 0U, static_cast<unsigned>(request.operation),
                mainValid() ? 1U : 0U);
  if (loaded && (request.operation == studio::RecoveryOperation::ImageVerifiedResetPending ||
                 request.operation == studio::RecoveryOperation::ResetComplete)) {
    screen("RESETTING", "Completing reset");
    if (request.operation == studio::RecoveryOperation::ImageVerifiedResetPending &&
        nvs_flash_erase() == ESP_OK) {
      request.operation = studio::RecoveryOperation::ResetComplete;
      journal.save(request);
    }
    if (request.operation == studio::RecoveryOperation::ResetComplete) bootMain();
  } else if (loaded && request.operation == studio::RecoveryOperation::InstallRequested) {
    if (!installRecord(request, false, false)) readyScreen("Update failed");
  } else if (loaded && request.operation == studio::RecoveryOperation::FactoryResetRequested) {
    if (!installRecord(request, true, true)) readyScreen("Factory reset failed");
  } else if (loaded && request.operation == studio::RecoveryOperation::RecoveryModeRequested) {
    readyScreen();
  } else {
    Serial.println("RECOVERY: no actionable request; staying in recovery");
    readyScreen();
  }
}

void loop() {
  uint16_t x = 0, y = 0;
  const bool touched = readTouch(x, y);
  if (!touchGate.update(touched, millis())) {
    delay(10);
    return;
  }
  if (touched && !touchDown) {
    touchDown = true;
    const bool retry = request.operation == studio::RecoveryOperation::FactoryResetRequested ||
        (request.operation == studio::RecoveryOperation::InstallRequested &&
         request.manifestLength > 0);
    const int gap = retry ? 28 : 30;
    int row = retry ? 86 : 110;
    if (mainValid()) {
      if (y >= row && y < row + 26) { bootMain(); return; }
      row += gap;
    }
    if (retry) {
      if (y >= row && y < row + 26) {
        const bool reset = request.operation == studio::RecoveryOperation::FactoryResetRequested;
        if (!installRecord(request, reset, reset)) readyScreen("Retry failed");
        return;
      }
      row += gap;
    }
    if (y >= row && y < row + 26) { startStable(false); return; }
    row += gap;
    if (y >= row && y < row + 26) factoryHoldStarted = millis();
  } else if (!touched && touchDown) {
    touchDown = false; factoryHoldStarted = 0;
    readyScreen();
  }
  if (touched && factoryHoldStarted != 0) {
    const uint32_t elapsed = millis() - factoryHoldStarted;
    screen("FACTORY RESET", elapsed >= 3000 ? "Starting" : "Hold for 3 seconds");
    if (elapsed >= 3000) { factoryHoldStarted = 0; startStable(true); }
  }
  delay(10);
}
