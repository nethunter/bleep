#define LGFX_USE_V1

#include <Arduino.h>
#include <Wire.h>
#include <lvgl.h>
#include <LovyanGFX.hpp>

#if ARDUINO_USB_CDC_ON_BOOT
#define DEBUG_PORT Serial0
#else
#define DEBUG_PORT Serial
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
constexpr uint8_t rtcAddr = 0x51;
constexpr uint8_t ioDirectionReg = 0x03;
constexpr uint8_t ioOutputReg = 0x05;
constexpr uint8_t ioHighZReg = 0x07;
constexpr uint8_t ioPanelPower = 4;
constexpr uint8_t ioTouchPower = 3;
constexpr uint8_t ioBacklight = 2;

// Verify this against your CrowPanel schematic. The original vendor demo did
// not include a battery example, so the ADC pin and divider are intentionally
// easy to override from platformio.ini build_flags.
#ifndef BATTERY_ADC_PIN
constexpr int batteryAdc = 3;
#else
constexpr int batteryAdc = BATTERY_ADC_PIN;
#endif

#ifndef BATTERY_DIVIDER
constexpr float batteryDivider = 6.0f;
#else
constexpr float batteryDivider = BATTERY_DIVIDER;
#endif

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
constexpr uint32_t buttonDebounceMs = 35;
constexpr uint32_t longPressMs = 1200;
constexpr uint32_t batteryRefreshMs = 5000;
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
    panelCfg.rgb_order = false;
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
static lv_color_t lvBuf1[board::screenWidth * 32];
static lv_color_t lvBuf2[board::screenWidth * 32];
static lv_disp_drv_t dispDrv;
static lv_indev_drv_t touchDrv;

static lv_obj_t* batteryLabel = nullptr;
static lv_obj_t* batteryArc = nullptr;
static lv_obj_t* statusLabel = nullptr;
static lv_obj_t* detailLabel = nullptr;
static lv_obj_t* menuButtons[3] = {nullptr, nullptr, nullptr};
static int selectedMenu = 0;
static bool screenOn = true;
static bool touchPresent = false;
static bool lastFallbackTouch = false;
static uint8_t activeTouchAddr = board::touchAddr;
static uint8_t touchChipId = 0;
static char i2cSummary[80] = "I2C: not scanned";
static char touchSummary[80] = "Touch: not probed";
static uint8_t lastTouchRawBytes[4] = {};
static size_t lastTouchRawByteCount = 0;
static float lastBatteryVoltage = 0.0f;
static int lastBatteryPercent = 0;
static uint32_t lastBatteryReadMs = 0;
static uint32_t lastTouchLogMs = 0;

namespace theme {
constexpr uint32_t black = 0x000000;
constexpr uint32_t panel = 0x0E1720;
constexpr uint32_t panelHot = 0x132638;
constexpr uint32_t rail = 0x1B2630;
constexpr uint32_t cyan = 0x00C8FF;
constexpr uint32_t amber = 0xFFB000;
constexpr uint32_t text = 0xF3F7FA;
constexpr uint32_t muted = 0x9AA8B3;
}  // namespace theme

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
  size_t used = snprintf(i2cSummary, sizeof(i2cSummary), "I2C:");
  int found = 0;

  for (uint8_t addr = 1; addr < 0x7F; ++addr) {
    if (!i2cDevicePresent(addr)) {
      continue;
    }
    found++;
    if (used < sizeof(i2cSummary)) {
      used += snprintf(i2cSummary + used, sizeof(i2cSummary) - used, " %02X", addr);
    }
  }

  if (found == 0) {
    snprintf(i2cSummary, sizeof(i2cSummary), "I2C: none");
  }
}

uint8_t ioOutputState = 0;

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
    snprintf(touchSummary, sizeof(touchSummary), "No CST816D at 0x%02X", activeTouchAddr);
    DEBUG_PORT.printf("%s\n", touchSummary);
    return false;
  }

  i2cRead(activeTouchAddr, 0xA7, &touchChipId, 1);
  i2cWrite8(activeTouchAddr, 0xFE, 0xFF);
  snprintf(touchSummary, sizeof(touchSummary), "CST816D 0x%02X id 0x%02X", activeTouchAddr, touchChipId);
  DEBUG_PORT.printf("%s\n", touchSummary);
  return true;
}

bool readTouchRaw(uint16_t& rawX, uint16_t& rawY) {
  uint8_t points = 0;
  if (!i2cRead(activeTouchAddr, 0x02, &points, 1) || points == 0) {
    lastTouchRawByteCount = 0;
    return false;
  }

  uint8_t data[4] = {};
  if (!i2cRead(activeTouchAddr, 0x03, data, sizeof(data))) {
    return false;
  }

  rawX = static_cast<uint16_t>(((data[0] & 0x0F) << 8) | data[1]);
  rawY = static_cast<uint16_t>(((data[2] & 0x0F) << 8) | data[3]);
  memcpy(lastTouchRawBytes, data, sizeof(data));
  lastTouchRawByteCount = sizeof(data);
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

void formatTouchRawBytes(char* output, size_t outputSize) {
  size_t used = snprintf(output, outputSize, "bytes:");
  for (size_t i = 0; i < lastTouchRawByteCount && used < outputSize; ++i) {
    used += snprintf(output + used, outputSize - used, " %02X", lastTouchRawBytes[i]);
  }
  if (lastTouchRawByteCount == 0) {
    snprintf(output, outputSize, "bytes: none");
  }
}

bool readTouchPoint(uint16_t& x, uint16_t& y) {
  uint16_t rawX = 0;
  uint16_t rawY = 0;
  if (!readTouchRaw(rawX, rawY)) {
    return false;
  }

  if (!mapTouchPoint(rawX, rawY, x, y)) {
    return false;
  }

  const uint32_t now = millis();
  if ((now - lastTouchLogMs) > 250) {
    lastTouchLogMs = now;
    DEBUG_PORT.printf("touch raw=(%u,%u) mapped=(%u,%u) int=%d\n",
                      static_cast<unsigned>(rawX), static_cast<unsigned>(rawY),
                      static_cast<unsigned>(x), static_cast<unsigned>(y),
                      digitalRead(board::touchInt));
  }

  return true;
}

void lvFlush(lv_disp_drv_t* disp, const lv_area_t* area, lv_color_t* colorP) {
  const int32_t width = area->x2 - area->x1 + 1;
  const int32_t height = area->y2 - area->y1 + 1;
  if (display.getStartCount() == 0) {
    display.endWrite();
  }
  display.pushImageDMA(area->x1, area->y1, width, height,
                       reinterpret_cast<lgfx::swap565_t*>(&colorP->full));
  lv_disp_flush_ready(disp);
}

void lvTouchRead(lv_indev_drv_t*, lv_indev_data_t* data) {
  static uint16_t lastX = board::screenWidth / 2;
  static uint16_t lastY = board::screenHeight / 2;
  uint16_t x = 0;
  uint16_t y = 0;

  if (screenOn && touchPresent && readTouchPoint(x, y)) {
    lastX = x;
    lastY = y;
    data->state = LV_INDEV_STATE_PR;
  } else {
    data->state = LV_INDEV_STATE_REL;
  }

  data->point.x = lastX;
  data->point.y = lastY;
}

int batteryPercentFromVoltage(float voltage) {
  struct Point {
    float voltage;
    int percent;
  };

  static constexpr Point curve[] = {
      {4.20f, 100}, {4.10f, 90}, {4.00f, 80}, {3.92f, 70},
      {3.85f, 60},  {3.79f, 50}, {3.74f, 40}, {3.70f, 30},
      {3.64f, 20},  {3.55f, 10}, {3.30f, 0},
  };

  if (voltage >= curve[0].voltage) {
    return 100;
  }
  if (voltage <= curve[sizeof(curve) / sizeof(curve[0]) - 1].voltage) {
    return 0;
  }

  for (size_t i = 1; i < sizeof(curve) / sizeof(curve[0]); ++i) {
    if (voltage >= curve[i].voltage) {
      const float highV = curve[i - 1].voltage;
      const float lowV = curve[i].voltage;
      const int highP = curve[i - 1].percent;
      const int lowP = curve[i].percent;
      const float t = (voltage - lowV) / (highV - lowV);
      return lowP + static_cast<int>((highP - lowP) * t + 0.5f);
    }
  }
  return 0;
}

void readBattery() {
  uint32_t totalMv = 0;
  constexpr int samples = 16;
  for (int i = 0; i < samples; ++i) {
    totalMv += analogReadMilliVolts(board::batteryAdc);
    delay(2);
  }

  const float adcMv = static_cast<float>(totalMv) / samples;
  lastBatteryVoltage = (adcMv * board::batteryDivider) / 1000.0f;
  lastBatteryPercent = batteryPercentFromVoltage(lastBatteryVoltage);
  lastBatteryReadMs = millis();
}

void updateBatteryUi() {
  if (!batteryLabel || !batteryArc) {
    return;
  }

  char text[48];
  if (lastBatteryVoltage < 2.5f || lastBatteryVoltage > 4.5f) {
    snprintf(text, sizeof(text), "--  %.2fV", lastBatteryVoltage);
  } else {
    snprintf(text, sizeof(text), "%d%%  %.2fV", lastBatteryPercent, lastBatteryVoltage);
  }
  lv_label_set_text(batteryLabel, text);
  lv_arc_set_value(batteryArc, constrain(lastBatteryPercent, 0, 100));
}

void setSelectedMenu(int index) {
  selectedMenu = index;
  for (int i = 0; i < 3; ++i) {
    if (!menuButtons[i]) {
      continue;
    }
    if (i == selectedMenu) {
      lv_obj_add_state(menuButtons[i], LV_STATE_CHECKED);
    } else {
      lv_obj_clear_state(menuButtons[i], LV_STATE_CHECKED);
    }
  }
}

void activateMenu(int index) {
  setSelectedMenu(index);
  if (index == 0) {
    lv_label_set_text(statusLabel, "Ready");
    if (touchPresent) {
      char text[128];
      snprintf(text, sizeof(text), "%s\n%s", touchSummary, i2cSummary);
      lv_label_set_text(detailLabel, text);
    } else {
      char text[128];
      snprintf(text, sizeof(text), "%s\n%s", touchSummary, i2cSummary);
      lv_label_set_text(detailLabel, text);
    }
  } else if (index == 1) {
    readBattery();
    updateBatteryUi();
    char text[96];
    if (lastBatteryVoltage < 2.5f || lastBatteryVoltage > 4.5f) {
      snprintf(text, sizeof(text), "ADC GPIO %d\n%.2f V check divider", board::batteryAdc, lastBatteryVoltage);
    } else {
      snprintf(text, sizeof(text), "1100 mAh LiPo\n%.2f V estimated at %d%%", lastBatteryVoltage,
               lastBatteryPercent);
    }
    lv_label_set_text(statusLabel, "Battery");
    lv_label_set_text(detailLabel, text);
  } else {
    lv_label_set_text(statusLabel, "CrowPanel 1.28");
    char text[128];
    snprintf(text, sizeof(text), "%s\n%s", touchSummary, i2cSummary);
    lv_label_set_text(detailLabel, text);
  }
}

void menuEvent(lv_event_t* event) {
  const int index = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(event)));
  activateMenu(index);
}

void buildUi() {
  lv_obj_t* screen = lv_scr_act();
  lv_obj_set_style_bg_color(screen, lv_color_hex(theme::black), 0);
  lv_obj_set_style_text_color(screen, lv_color_hex(theme::text), 0);

  batteryArc = lv_arc_create(screen);
  lv_obj_set_size(batteryArc, 218, 218);
  lv_obj_center(batteryArc);
  lv_arc_set_bg_angles(batteryArc, 205, 335);
  lv_arc_set_rotation(batteryArc, 0);
  lv_arc_set_range(batteryArc, 0, 100);
  lv_obj_remove_style(batteryArc, nullptr, LV_PART_KNOB);
  lv_obj_clear_flag(batteryArc, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_arc_width(batteryArc, 9, LV_PART_MAIN);
  lv_obj_set_style_arc_width(batteryArc, 9, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(batteryArc, lv_color_hex(theme::rail), LV_PART_MAIN);
  lv_obj_set_style_arc_color(batteryArc, lv_color_hex(theme::cyan), LV_PART_INDICATOR);

  batteryLabel = lv_label_create(screen);
  lv_obj_set_style_text_font(batteryLabel, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(batteryLabel, lv_color_hex(theme::amber), 0);
  lv_obj_align(batteryLabel, LV_ALIGN_TOP_MID, 0, 18);

  statusLabel = lv_label_create(screen);
  lv_obj_set_style_text_font(statusLabel, &lv_font_montserrat_24, 0);
  lv_obj_set_style_text_color(statusLabel, lv_color_hex(theme::cyan), 0);
  lv_obj_align(statusLabel, LV_ALIGN_TOP_MID, 0, 50);

  detailLabel = lv_label_create(screen);
  lv_obj_set_width(detailLabel, 198);
  lv_label_set_long_mode(detailLabel, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(detailLabel, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(detailLabel, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(detailLabel, lv_color_hex(theme::muted), 0);
  lv_obj_align(detailLabel, LV_ALIGN_TOP_MID, 0, 82);

  lv_obj_t* list = lv_list_create(screen);
  lv_obj_set_size(list, 194, 82);
  lv_obj_align(list, LV_ALIGN_BOTTOM_MID, 0, -16);
  lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(list, 0, 0);
  lv_obj_set_style_pad_all(list, 0, 0);
  lv_obj_set_style_pad_row(list, 3, 0);

  const char* labels[] = {"Dashboard", "Battery", "About"};
  for (int i = 0; i < 3; ++i) {
    menuButtons[i] = lv_list_add_btn(list, nullptr, labels[i]);
    lv_obj_set_height(menuButtons[i], 24);
    lv_obj_set_style_radius(menuButtons[i], 5, 0);
    lv_obj_set_style_border_width(menuButtons[i], 1, 0);
    lv_obj_set_style_border_color(menuButtons[i], lv_color_hex(0x233444), 0);
    lv_obj_set_style_border_color(menuButtons[i], lv_color_hex(theme::amber), LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(menuButtons[i], lv_color_hex(theme::panel), 0);
    lv_obj_set_style_bg_color(menuButtons[i], lv_color_hex(theme::panelHot), LV_STATE_CHECKED);
    lv_obj_set_style_text_color(menuButtons[i], lv_color_hex(theme::text), 0);
    lv_obj_set_style_text_color(menuButtons[i], lv_color_hex(theme::amber), LV_STATE_CHECKED);
    lv_obj_set_style_text_font(menuButtons[i], &lv_font_montserrat_14, 0);
    lv_obj_add_event_cb(menuButtons[i], menuEvent, LV_EVENT_CLICKED,
                        reinterpret_cast<void*>(static_cast<intptr_t>(i)));
  }

  readBattery();
  updateBatteryUi();
  activateMenu(0);
}

void setScreenPower(bool enabled) {
  if (screenOn == enabled) {
    return;
  }

  screenOn = enabled;
  if (enabled) {
    ioSetPin(board::ioPanelPower, true);
    delay(60);
    display.wakeup();
    display.fillScreen(TFT_BLACK);
    ioSetPin(board::ioBacklight, true);
    lv_obj_invalidate(lv_scr_act());
  } else {
    lv_label_set_text(statusLabel, "Off");
    lv_label_set_text(detailLabel, "Long press to wake");
    lv_timer_handler();
    delay(120);
    ioSetPin(board::ioBacklight, false);
    display.sleep();
  }
}

void handleShortPress() {
  if (!screenOn) {
    return;
  }
  activateMenu(selectedMenu);
  setSelectedMenu((selectedMenu + 1) % 3);
}

void pollButton() {
  static bool lastRawPressed = false;
  static bool stablePressed = false;
  static bool longPressHandled = false;
  static uint32_t lastChangeMs = 0;
  static uint32_t pressStartMs = 0;

  const bool rawPressed = digitalRead(board::button) == LOW;
  const uint32_t now = millis();

  if (rawPressed != lastRawPressed) {
    lastRawPressed = rawPressed;
    lastChangeMs = now;
  }

  if ((now - lastChangeMs) < board::buttonDebounceMs || rawPressed == stablePressed) {
    if (stablePressed && !longPressHandled && (now - pressStartMs) >= board::longPressMs) {
      longPressHandled = true;
      setScreenPower(!screenOn);
    }
    return;
  }

  stablePressed = rawPressed;
  if (stablePressed) {
    pressStartMs = now;
    longPressHandled = false;
  } else if (!longPressHandled && (now - pressStartMs) >= board::buttonDebounceMs) {
    handleShortPress();
  }
}

void pollTouchFallback() {
  if (!screenOn || !touchPresent) {
    return;
  }

  uint16_t rawX = 0;
  uint16_t rawY = 0;
  uint16_t x = 0;
  uint16_t y = 0;
  const bool rawRead = readTouchRaw(rawX, rawY);
  const bool touched = rawRead && mapTouchPoint(rawX, rawY, x, y);
  const bool rawNonZero = rawRead && (rawX != 0 || rawY != 0);
  const bool shouldShowRaw = rawNonZero;

  if (touched && !lastFallbackTouch) {
    char bytes[48];
    formatTouchRawBytes(bytes, sizeof(bytes));
    char text[96];
    snprintf(text, sizeof(text), "%u,%u raw %u,%u\nint %d %s", static_cast<unsigned>(x),
             static_cast<unsigned>(y), static_cast<unsigned>(rawX), static_cast<unsigned>(rawY),
             digitalRead(board::touchInt), bytes);
    lv_label_set_text(statusLabel, "Touch");
    lv_label_set_text(detailLabel, text);

    if (y >= 150) {
      const int row = min(2, max(0, static_cast<int>((y - 150) / 30)));
      activateMenu(row);
    }
  } else if (!touched && shouldShowRaw) {
    char bytes[48];
    formatTouchRawBytes(bytes, sizeof(bytes));
    char text[96];
    snprintf(text, sizeof(text), "raw %u,%u int %d\n%s", static_cast<unsigned>(rawX),
             static_cast<unsigned>(rawY), digitalRead(board::touchInt), bytes);
    lv_label_set_text(statusLabel, "Touch?");
    lv_label_set_text(detailLabel, text);
  }

  lastFallbackTouch = touched;
}

void setupLvgl() {
  lv_init();
  lv_disp_draw_buf_init(&drawBuf, lvBuf1, lvBuf2, board::screenWidth * 32);

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
  Wire.begin(board::i2cSda, board::i2cScl, board::i2cFreq);
  initIoExpander();
  scanI2cBus();

  display.init();
  display.setRotation(0);
  display.fillScreen(TFT_BLACK);

  analogReadResolution(12);
  analogSetPinAttenuation(board::batteryAdc, ADC_11db);

  touchPresent = initTouch();
  setupLvgl();
  buildUi();
}

void loop() {
  static uint32_t lastTickMs = millis();
  const uint32_t now = millis();
  lv_tick_inc(now - lastTickMs);
  lastTickMs = now;

  pollButton();
  pollTouchFallback();

  if (screenOn && (now - lastBatteryReadMs) >= board::batteryRefreshMs) {
    readBattery();
    updateBatteryUi();
  }

  if (screenOn) {
    lv_timer_handler();
  }
  delay(5);
}
