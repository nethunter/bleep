#define LGFX_USE_V1

#include <Arduino.h>
#include <Wire.h>
#include <esp_sleep.h>
#include <lvgl.h>
#include <LovyanGFX.hpp>

#include "core/device_manager.h"
#include "ui.h"

#if ARDUINO_USB_CDC_ON_BOOT
#define DEBUG_PORT Serial0
#else
#define DEBUG_PORT Serial
#endif

#ifndef DISPLAY_RGB_ORDER
#define DISPLAY_RGB_ORDER 0
#endif

#ifndef DISPLAY_FLUSH_SWAP565
#define DISPLAY_FLUSH_SWAP565 1
#endif

namespace board {
constexpr int lcdDc = 2;
constexpr int lcdCs = 10;
constexpr int lcdSck = 6;
constexpr int lcdMosi = 7;
constexpr int i2cSda = 4;
constexpr int i2cScl = 5;
constexpr int button = 1;
constexpr int touchInt = 0;

constexpr uint8_t ioExpanderAddr = 0x43;
constexpr uint8_t touchAddr = 0x15;
constexpr uint8_t ioDirectionReg = 0x03;
constexpr uint8_t ioOutputReg = 0x05;
constexpr uint8_t ioHighZReg = 0x07;
constexpr uint8_t ioPanelPower = 4;
constexpr uint8_t ioTouchPower = 3;
constexpr uint8_t ioBacklight = 2;

#ifndef TOUCH_SWAP_XY
constexpr bool touchSwapXy = false;
#else
constexpr bool touchSwapXy = TOUCH_SWAP_XY;
#endif

#ifndef TOUCH_INVERT_X
constexpr bool touchInvertX = false;
#else
constexpr bool touchInvertX = TOUCH_INVERT_X;
#endif

#ifndef TOUCH_INVERT_Y
constexpr bool touchInvertY = false;
#else
constexpr bool touchInvertY = TOUCH_INVERT_Y;
#endif

constexpr uint32_t i2cFreq = 400000;
constexpr uint16_t screenWidth = 240;
constexpr uint16_t screenHeight = 240;
// Smaller partial-render buffers leave SRAM headroom for the BLE host stack.
constexpr uint16_t drawBufferRows = 40;
constexpr uint32_t buttonDebounceMs = 35;
constexpr uint32_t buttonLongPressMs = 1600;
}  // namespace board

class CrowPanelDisplay : public lgfx::LGFX_Device {
 public:
  CrowPanelDisplay() {
    auto busCfg = bus_.config();
    busCfg.spi_host = SPI2_HOST;
    busCfg.spi_mode = 0;
    busCfg.freq_write = 80000000;
    busCfg.freq_read = 20000000;
    busCfg.spi_3wire = true;
    busCfg.use_lock = true;
    busCfg.dma_channel = SPI_DMA_CH_AUTO;
    busCfg.pin_sclk = board::lcdSck;
    busCfg.pin_mosi = board::lcdMosi;
    busCfg.pin_miso = -1;
    busCfg.pin_dc = board::lcdDc;
    bus_.config(busCfg);
    panel_.setBus(&bus_);

    auto panelCfg = panel_.config();
    panelCfg.pin_cs = board::lcdCs;
    panelCfg.pin_rst = -1;
    panelCfg.pin_busy = -1;
    panelCfg.memory_width = board::screenWidth;
    panelCfg.memory_height = board::screenHeight;
    panelCfg.panel_width = board::screenWidth;
    panelCfg.panel_height = board::screenHeight;
    panelCfg.offset_x = 0;
    panelCfg.offset_y = 0;
    panelCfg.offset_rotation = 0;
    panelCfg.dummy_read_pixel = 8;
    panelCfg.dummy_read_bits = 1;
    panelCfg.readable = false;
    panelCfg.invert = true;
    panelCfg.rgb_order = DISPLAY_RGB_ORDER != 0;
    panelCfg.dlen_16bit = false;
    panelCfg.bus_shared = false;
    panel_.config(panelCfg);
    setPanel(&panel_);
  }

 private:
  lgfx::Bus_SPI bus_;
  lgfx::Panel_GC9A01 panel_;
};

CrowPanelDisplay display;

static lv_disp_draw_buf_t drawBuf;
static lv_color_t lvBuf1[board::screenWidth * board::drawBufferRows];
static lv_color_t lvBuf2[board::screenWidth * board::drawBufferRows];
static lv_disp_drv_t dispDrv;
static lv_indev_drv_t touchDrv;

static bool touchPresent = false;
static uint8_t activeTouchAddr = board::touchAddr;
static uint8_t touchChipId = 0;
static uint8_t ioOutputState = 0;
static bool ignoreWakeButtonUntilRelease = false;

studio::LinkState currentLink() {
  return studio::devices().runtimeState(studio::devices().activeInstance()).link;
}

const char* linkLabel(studio::LinkState link) {
  switch (link) {
    case studio::LinkState::Disconnected:
      return "disconnected";
    case studio::LinkState::Scanning:
      return "scanning";
    case studio::LinkState::Connecting:
      return "connecting";
    case studio::LinkState::Connected:
      return "connected";
  }
  return "unknown";
}

void logRuntimeStats(const char* event) {
  DEBUG_PORT.printf("runtime event=%s uptime_ms=%lu link=%s free_heap=%lu min_free_heap=%lu\n",
                    event, static_cast<unsigned long>(millis()),
                    linkLabel(currentLink()),
                    static_cast<unsigned long>(ESP.getFreeHeap()),
                    static_cast<unsigned long>(ESP.getMinFreeHeap()));
}

bool i2cWrite8(uint8_t addr, uint8_t reg, uint8_t value) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

bool i2cRead(uint8_t addr, uint8_t reg, uint8_t* data, size_t len) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }
  if (Wire.requestFrom(static_cast<int>(addr), static_cast<int>(len)) != len) {
    return false;
  }
  for (size_t i = 0; i < len; ++i) {
    data[i] = Wire.read();
  }
  return true;
}

bool i2cDevicePresent(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

void scanI2cBus() {
  DEBUG_PORT.print("I2C:");
  int found = 0;
  for (uint8_t addr = 1; addr < 0x7F; ++addr) {
    if (i2cDevicePresent(addr)) {
      found++;
      DEBUG_PORT.printf(" %02X", addr);
    }
  }
  if (found == 0) {
    DEBUG_PORT.print(" none");
  }
  DEBUG_PORT.println();
}

void ioSetPin(uint8_t pin, bool high) {
  if (high) {
    ioOutputState |= (1U << pin);
  } else {
    ioOutputState &= ~(1U << pin);
  }
  i2cWrite8(board::ioExpanderAddr, board::ioOutputReg, ioOutputState);
}

void initIoExpander() {
  constexpr uint8_t enabledPins =
      (1U << 0) | (1U << 1) | (1U << board::ioBacklight) | (1U << board::ioTouchPower) |
      (1U << board::ioPanelPower);

  i2cWrite8(board::ioExpanderAddr, board::ioDirectionReg, enabledPins);
  i2cWrite8(board::ioExpanderAddr, board::ioHighZReg, static_cast<uint8_t>(~enabledPins));
  ioSetPin(board::ioTouchPower, true);
  delay(80);
  ioSetPin(board::ioPanelPower, true);
  delay(80);
  ioSetPin(board::ioBacklight, true);
  delay(80);
}

bool initTouch() {
  activeTouchAddr = board::touchAddr;

  pinMode(board::touchInt, OUTPUT);
  digitalWrite(board::touchInt, HIGH);
  delay(1);
  digitalWrite(board::touchInt, LOW);
  delay(1);

  if (!i2cDevicePresent(activeTouchAddr)) {
    DEBUG_PORT.printf("No CST816D at 0x%02X\n", activeTouchAddr);
    return false;
  }

  i2cRead(activeTouchAddr, 0xA7, &touchChipId, 1);
  i2cWrite8(activeTouchAddr, 0xFE, 0xFF);
  DEBUG_PORT.printf("CST816D 0x%02X id 0x%02X\n", activeTouchAddr, touchChipId);
  return true;
}

bool readTouchRaw(uint16_t& rawX, uint16_t& rawY) {
  uint8_t points = 0;
  if (!i2cRead(activeTouchAddr, 0x02, &points, 1) || points == 0) {
    return false;
  }
  uint8_t data[4] = {};
  if (!i2cRead(activeTouchAddr, 0x03, data, sizeof(data))) {
    return false;
  }
  rawX = static_cast<uint16_t>(((data[0] & 0x0F) << 8) | data[1]);
  rawY = static_cast<uint16_t>(((data[2] & 0x0F) << 8) | data[3]);
  return true;
}

bool mapTouchPoint(uint16_t rawX, uint16_t rawY, uint16_t& x, uint16_t& y) {
  if ((rawX == 0 && rawY == 0) || rawX >= board::screenWidth || rawY >= board::screenHeight) {
    return false;
  }
  if (board::touchSwapXy) {
    const uint16_t temp = rawX;
    rawX = rawY;
    rawY = temp;
  }
  if (board::touchInvertX) {
    rawX = board::screenWidth - 1 - rawX;
  }
  if (board::touchInvertY) {
    rawY = board::screenHeight - 1 - rawY;
  }
  x = rawX;
  y = rawY;
  return true;
}

bool readTouchPoint(uint16_t& x, uint16_t& y) {
  uint16_t rawX = 0;
  uint16_t rawY = 0;
  if (!readTouchRaw(rawX, rawY)) {
    return false;
  }
  return mapTouchPoint(rawX, rawY, x, y);
}

void lvFlush(lv_disp_drv_t* disp, const lv_area_t* area, lv_color_t* colorP) {
  const int32_t width = area->x2 - area->x1 + 1;
  const int32_t height = area->y2 - area->y1 + 1;
  if (display.getStartCount() == 0) {
    display.endWrite();
  }
#if DISPLAY_FLUSH_SWAP565
  display.pushImageDMA(area->x1, area->y1, width, height,
                       reinterpret_cast<lgfx::swap565_t*>(&colorP->full));
#else
  display.pushImageDMA(area->x1, area->y1, width, height,
                       reinterpret_cast<lgfx::rgb565_t*>(&colorP->full));
#endif
  lv_disp_flush_ready(disp);
}

void lvTouchRead(lv_indev_drv_t*, lv_indev_data_t* data) {
  static uint16_t lastX = board::screenWidth / 2;
  static uint16_t lastY = board::screenHeight / 2;
  uint16_t x = 0;
  uint16_t y = 0;

  if (touchPresent && readTouchPoint(x, y)) {
    lastX = x;
    lastY = y;
    data->state = LV_INDEV_STATE_PR;
  } else {
    data->state = LV_INDEV_STATE_REL;
  }
  data->point.x = lastX;
  data->point.y = lastY;
}

void handleShortPress() {
  ui::handleShortPress();
}

void configureButtonWake() {
  esp_deep_sleep_enable_gpio_wakeup(1ULL << board::button, ESP_GPIO_WAKEUP_GPIO_LOW);
}

[[noreturn]] void enterDeepSleep() {
  configureButtonWake();
  DEBUG_PORT.flush();
  esp_deep_sleep_start();
  while (true) {
    delay(1000);
  }
}

void waitForButtonRelease() {
  while (digitalRead(board::button) == LOW) {
    delay(10);
  }
  delay(board::buttonDebounceMs);
}

bool wakeButtonHeldLongEnough() {
  const uint32_t startedMs = millis();
  while (digitalRead(board::button) == LOW) {
    if ((millis() - startedMs) >= board::buttonLongPressMs) {
      return true;
    }
    delay(10);
  }
  return false;
}

void enforceLongPressWake() {
  if (esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_GPIO) {
    return;
  }
  if (!wakeButtonHeldLongEnough()) {
    enterDeepSleep();
  }
  ignoreWakeButtonUntilRelease = true;
}

[[noreturn]] void handleLongPress() {
  DEBUG_PORT.println("Button long press: power off");
  studio::devices().deactivate();

  ioSetPin(board::ioBacklight, false);
  delay(40);
  ioSetPin(board::ioPanelPower, false);
  ioSetPin(board::ioTouchPower, false);

  waitForButtonRelease();
  enterDeepSleep();
}

void pollButton() {
  static bool lastRawPressed = false;
  static bool stablePressed = false;
  static bool longPressHandled = false;
  static uint32_t lastChangeMs = 0;
  static uint32_t pressStartedMs = 0;

  const bool rawPressed = digitalRead(board::button) == LOW;
  const uint32_t now = millis();

  if (ignoreWakeButtonUntilRelease) {
    lastRawPressed = rawPressed;
    stablePressed = false;
    longPressHandled = false;
    pressStartedMs = 0;
    lastChangeMs = now;
    if (!rawPressed) {
      ignoreWakeButtonUntilRelease = false;
    }
    return;
  }

  if (rawPressed != lastRawPressed) {
    lastRawPressed = rawPressed;
    lastChangeMs = now;
  }

  if (stablePressed && !longPressHandled &&
      (now - pressStartedMs) >= board::buttonLongPressMs) {
    longPressHandled = true;
    handleLongPress();
  }

  if ((now - lastChangeMs) < board::buttonDebounceMs || rawPressed == stablePressed) {
    return;
  }

  stablePressed = rawPressed;
  if (stablePressed) {
    pressStartedMs = now;
    longPressHandled = false;
  } else {
    pressStartedMs = 0;
    handleShortPress();
  }
}

void setupLvgl() {
  lv_init();
  lv_disp_draw_buf_init(&drawBuf, lvBuf1, lvBuf2, board::screenWidth * board::drawBufferRows);

  lv_disp_drv_init(&dispDrv);
  dispDrv.hor_res = board::screenWidth;
  dispDrv.ver_res = board::screenHeight;
  dispDrv.flush_cb = lvFlush;
  dispDrv.draw_buf = &drawBuf;
  lv_disp_drv_register(&dispDrv);

  if (touchPresent) {
    lv_indev_drv_init(&touchDrv);
    touchDrv.type = LV_INDEV_TYPE_POINTER;
    touchDrv.read_cb = lvTouchRead;
    lv_indev_drv_register(&touchDrv);
  }
}

void setup() {
  DEBUG_PORT.begin(115200);
  delay(100);

  pinMode(board::button, INPUT_PULLUP);
  enforceLongPressWake();
  Wire.begin(board::i2cSda, board::i2cScl, board::i2cFreq);
  initIoExpander();
  scanI2cBus();

  display.init();
  display.initDMA();
  display.startWrite();
  display.setColor(0, 0, 0);
  display.setRotation(0);
  display.fillScreen(TFT_BLACK);

  touchPresent = initTouch();
  setupLvgl();

  studio::devices().begin();
  ui::init();
  // Paint Home before any operator-requested device activation.
  lv_timer_handler();
  logRuntimeStats("boot");
}

void loop() {
  static uint32_t lastTickMs = millis();
  static uint32_t lastStatsMs = millis();
  static studio::LinkState lastLink = currentLink();
  const uint32_t now = millis();
  lv_tick_inc(now - lastTickMs);
  lastTickMs = now;

  studio::devices().loop();
  const studio::LinkState link = currentLink();
  if (link != lastLink) {
    lastLink = link;
    logRuntimeStats("link_change");
  }
  if (now - lastStatsMs >= 30000) {
    lastStatsMs = now;
    logRuntimeStats("periodic");
  }
  pollButton();
  ui::tick();

  lv_timer_handler();
  delay(5);
}
