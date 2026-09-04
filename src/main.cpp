#define LGFX_USE_V1

#include <Arduino.h>
#include <Wire.h>
#include <lvgl.h>
#include <LovyanGFX.hpp>

#include <cstdlib>
#include <cstring>

#include "build_info.h"
#include "driver_config.h"
#if CONFIG_BLE_RUNTIME_ENABLED
#include "core/ble/ble_runtime.h"
#endif
#include "core/device_manager.h"
#include "core/scene_service.h"
#include "core/system_info.h"
#include "fonts/ui_fonts.h"
#include "haptic_feedback.h"
#include "firmware_update.h"
#include "wifi_configuration.h"
#include "ui.h"
#include "portal_service.h"

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
constexpr uint8_t ioVibrationMotor = 0;
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
// Keep DMA double buffering, but use narrow partial-render strips so the
// ESP32-C3 can initialize Wi-Fi while concurrent BLE links remain active.
// Fifteen rows divides the 240-line panel evenly into 16 flushes.
constexpr uint16_t drawBufferRows = 15;
constexpr uint32_t buttonDebounceMs = 35;
constexpr uint32_t buttonLongPressMs = 700;
constexpr uint32_t buttonHomePressMs = 2000;
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

studio::LinkState currentLink() {
  return studio::devices()
      .runtimeState(studio::devices().foregroundInstance())
      .link;
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
  const studio::SystemInfo info = studio::systemInfo();
  DEBUG_PORT.printf("runtime event=%s uptime_ms=%lu link=%s free_heap=%lu min_free_heap=%lu max_alloc=%lu wifi=%s\n",
                    event, static_cast<unsigned long>(millis()),
                    linkLabel(currentLink()),
                    static_cast<unsigned long>(info.freeHeap),
                    static_cast<unsigned long>(info.minimumFreeHeap),
                    static_cast<unsigned long>(info.largestFreeBlock),
                    info.wifiState);
}

enum class DebugDevicePhase : uint8_t {
  Idle,
  WaitingForReady,
  WaitingForDispatch,
  WaitingForConfirmation,
};

struct DebugDeviceCommand {
  DebugDevicePhase phase = DebugDevicePhase::Idle;
  studio::InstanceId instanceId = studio::kInvalidInstanceId;
  studio::CommandType command = studio::CommandType::Refresh;
  int32_t value0 = 0;
  int32_t value1 = 0;
  int32_t value2 = 0;
  uint32_t requestId = 0;
  uint32_t deadlineMs = 0;
};

DebugDeviceCommand debugDeviceCommand;

const char* debugDeviceAction(studio::CommandType command) {
  switch (command) {
    case studio::CommandType::TurnOn: return "on";
    case studio::CommandType::TurnOff: return "off";
    case studio::CommandType::SetLightCct: return "cct";
    case studio::CommandType::SetLightRgb: return "rgb";
    default: return "other";
  }
}

studio::Capability debugDeviceCapability(studio::CommandType command) {
  switch (command) {
    case studio::CommandType::TurnOn: return studio::Capability::TurnOn;
    case studio::CommandType::SetLightCct: return studio::Capability::SetLightCct;
    case studio::CommandType::SetLightRgb: return studio::Capability::SetLightRgb;
    default: return studio::Capability::TurnOff;
  }
}

void finishDebugDeviceCommand(const char* result) {
  DEBUG_PORT.printf(
      "debug_device event=complete instance=%lu action=%s result=%s\n",
      static_cast<unsigned long>(debugDeviceCommand.instanceId),
      debugDeviceAction(debugDeviceCommand.command), result);
  studio::devices().release(debugDeviceCommand.instanceId,
                            studio::ConnectionOwner::Sequence);
  debugDeviceCommand = DebugDeviceCommand{};
}

void beginDebugDeviceCommand(studio::InstanceId instanceId,
                             studio::CommandType command, int32_t value0 = 0,
                             int32_t value1 = 0, int32_t value2 = 0) {
  if (debugDeviceCommand.phase != DebugDevicePhase::Idle) {
    DEBUG_PORT.println("debug_device event=begin result=busy");
    return;
  }
  const studio::DeviceRecord* record = studio::devices().find(instanceId);
  const studio::Capability capability = debugDeviceCapability(command);
  if (record == nullptr ||
      (studio::devices().profile(instanceId).capabilities &
       studio::capabilityBit(capability)) == 0) {
    DEBUG_PORT.printf(
        "debug_device event=begin instance=%lu action=%s result=invalid_target\n",
        static_cast<unsigned long>(instanceId), debugDeviceAction(command));
    return;
  }
  if (studio::scenes().busy() ||
      studio::devices().ownedBy(instanceId, studio::ConnectionOwner::Sequence)) {
    DEBUG_PORT.printf(
        "debug_device event=begin instance=%lu action=%s result=scene_busy\n",
        static_cast<unsigned long>(instanceId), debugDeviceAction(command));
    return;
  }
  if (!studio::devices().acquire(instanceId,
                                 studio::ConnectionOwner::Sequence)) {
    DEBUG_PORT.printf(
        "debug_device event=begin instance=%lu action=%s result=acquire_failed\n",
        static_cast<unsigned long>(instanceId), debugDeviceAction(command));
    return;
  }
  debugDeviceCommand.phase = DebugDevicePhase::WaitingForReady;
  debugDeviceCommand.instanceId = instanceId;
  debugDeviceCommand.command = command;
  debugDeviceCommand.value0 = value0;
  debugDeviceCommand.value1 = value1;
  debugDeviceCommand.value2 = value2;
  debugDeviceCommand.deadlineMs = millis() + 30000;
  DEBUG_PORT.printf(
      "debug_device event=begin instance=%lu action=%s result=waiting\n",
      static_cast<unsigned long>(instanceId), debugDeviceAction(command));
}

void pollDebugDeviceCommand() {
  if (std::strcmp(build_info::kReleaseChannel, "local") != 0 ||
      debugDeviceCommand.phase == DebugDevicePhase::Idle)
    return;
  if (static_cast<int32_t>(millis() - debugDeviceCommand.deadlineMs) >= 0) {
    finishDebugDeviceCommand("timeout");
    return;
  }

  const studio::DeviceRuntimeState runtime =
      studio::devices().runtimeState(debugDeviceCommand.instanceId);
  if (debugDeviceCommand.phase == DebugDevicePhase::WaitingForReady) {
    if (!runtime.protocolReady) return;
    studio::DeviceCommand command;
    command.instanceId = debugDeviceCommand.instanceId;
    command.type = debugDeviceCommand.command;
    command.value0 = debugDeviceCommand.value0;
    command.value1 = debugDeviceCommand.value1;
    command.value2 = debugDeviceCommand.value2;
    if (!studio::devices().enqueue(command, &debugDeviceCommand.requestId)) {
      finishDebugDeviceCommand("queue_full");
      return;
    }
    debugDeviceCommand.phase = DebugDevicePhase::WaitingForDispatch;
    DEBUG_PORT.printf(
        "debug_device event=dispatch instance=%lu action=%s request=%lu\n",
        static_cast<unsigned long>(debugDeviceCommand.instanceId),
        debugDeviceAction(debugDeviceCommand.command),
        static_cast<unsigned long>(debugDeviceCommand.requestId));
    return;
  }

  if (debugDeviceCommand.phase == DebugDevicePhase::WaitingForDispatch) {
    studio::CommandResult result;
    if (!studio::devices().takeResult(debugDeviceCommand.requestId, result))
      return;
    if (result.status != studio::CommandStatus::Succeeded) {
      DEBUG_PORT.printf(
          "debug_device event=dispatch_result instance=%lu action=%s status=%u\n",
          static_cast<unsigned long>(debugDeviceCommand.instanceId),
          debugDeviceAction(debugDeviceCommand.command),
          static_cast<unsigned>(result.status));
      finishDebugDeviceCommand("dispatch_failed");
      return;
    }
    debugDeviceCommand.phase = DebugDevicePhase::WaitingForConfirmation;
    return;
  }

  if (!runtime.commandPending) {
    finishDebugDeviceCommand(runtime.commandFailed ? "confirmation_failed"
                                                   : "confirmed");
  }
}

void runDebugCommand(char* command) {
  if (std::strcmp(build_info::kReleaseChannel, "local") != 0) return;

  static constexpr char kDeviceOnPrefix[] = "device on ";
  static constexpr char kDeviceOffPrefix[] = "device off ";
  const char* deviceIdText = nullptr;
  studio::CommandType deviceCommand = studio::CommandType::Refresh;
  if (std::strncmp(command, kDeviceOnPrefix, sizeof(kDeviceOnPrefix) - 1) == 0) {
    deviceIdText = command + sizeof(kDeviceOnPrefix) - 1;
    deviceCommand = studio::CommandType::TurnOn;
  } else if (std::strncmp(command, kDeviceOffPrefix,
                          sizeof(kDeviceOffPrefix) - 1) == 0) {
    deviceIdText = command + sizeof(kDeviceOffPrefix) - 1;
    deviceCommand = studio::CommandType::TurnOff;
  }
  if (deviceIdText != nullptr) {
    char* end = nullptr;
    const unsigned long value = std::strtoul(deviceIdText, &end, 10);
    if (end == deviceIdText || *end != '\0' || value == 0) {
      DEBUG_PORT.println("debug_device event=begin result=invalid_id");
      return;
    }
    beginDebugDeviceCommand(static_cast<studio::InstanceId>(value),
                            deviceCommand);
    return;
  }

  static constexpr char kCctPrefix[] = "device cct ";
  static constexpr char kRgbPrefix[] = "device rgb ";
  const bool isCct =
      std::strncmp(command, kCctPrefix, sizeof(kCctPrefix) - 1) == 0;
  const bool isRgb =
      std::strncmp(command, kRgbPrefix, sizeof(kRgbPrefix) - 1) == 0;
  if (isCct || isRgb) {
    // "device cct <instance> <kelvin> <brightness>" and
    // "device rgb <instance> <rrggbb> <brightness>" exercise the look paths
    // that scenes use, so colour behaviour can be measured without the panel.
    const char* cursor = command + (isCct ? sizeof(kCctPrefix) - 1
                                          : sizeof(kRgbPrefix) - 1);
    char* end = nullptr;
    const unsigned long instance = std::strtoul(cursor, &end, 10);
    if (end == cursor || instance == 0) {
      DEBUG_PORT.println("debug_device event=begin result=invalid_id");
      return;
    }
    cursor = end;
    const unsigned long value = std::strtoul(cursor, &end, isRgb ? 16 : 10);
    if (end == cursor) {
      DEBUG_PORT.println("debug_device event=begin result=invalid_value");
      return;
    }
    cursor = end;
    const unsigned long brightness = std::strtoul(cursor, &end, 10);
    if (end == cursor || *end != '\0' || brightness > 100) {
      DEBUG_PORT.println("debug_device event=begin result=invalid_brightness");
      return;
    }
    beginDebugDeviceCommand(static_cast<studio::InstanceId>(instance),
                            isRgb ? studio::CommandType::SetLightRgb
                                  : studio::CommandType::SetLightCct,
                            static_cast<int32_t>(value),
                            static_cast<int32_t>(brightness), 0);
    return;
  }

  static constexpr char kStartPrefix[] = "scene start ";
  if (std::strncmp(command, kStartPrefix, sizeof(kStartPrefix) - 1) == 0) {
    char* end = nullptr;
    const unsigned long value =
        std::strtoul(command + sizeof(kStartPrefix) - 1, &end, 10);
    if (end == command + sizeof(kStartPrefix) - 1 || *end != '\0') {
      DEBUG_PORT.println("debug_scene event=start result=invalid_id");
      return;
    }
    const studio::SceneId sceneId = static_cast<studio::SceneId>(value);
    const studio::SceneRunStatus status = studio::scenes().start(sceneId);
    DEBUG_PORT.printf("debug_scene event=start scene=%lu status=%u\n", value,
                      static_cast<unsigned>(status));
    return;
  }
  if (std::strcmp(command, "scene stop") == 0) {
    const studio::SceneRunStatus status = studio::scenes().stop();
    DEBUG_PORT.printf("debug_scene event=stop status=%u\n",
                      static_cast<unsigned>(status));
    return;
  }
  if (std::strcmp(command, "scene cancel") == 0) {
    studio::scenes().cancel();
    DEBUG_PORT.println("debug_scene event=cancel result=ok");
    return;
  }
  DEBUG_PORT.println("debug event=command result=unknown");
}

void pollDebugSerial() {
  if (std::strcmp(build_info::kReleaseChannel, "local") != 0) return;
  static char command[48] = {};
  static size_t length = 0;
  while (DEBUG_PORT.available() > 0) {
    const char value = static_cast<char>(DEBUG_PORT.read());
    if (value == '\r') continue;
    if (value == '\n') {
      command[length] = '\0';
      if (length > 0) runDebugCommand(command);
      length = 0;
      continue;
    }
    if (length + 1 < sizeof(command)) {
      command[length++] = value;
    } else {
      length = 0;
      DEBUG_PORT.println("debug event=command result=too_long");
    }
  }
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
      (1U << board::ioVibrationMotor) | (1U << 1) | (1U << board::ioBacklight) |
      (1U << board::ioTouchPower) | (1U << board::ioPanelPower);

  ioOutputState = 0;
  i2cWrite8(board::ioExpanderAddr, board::ioOutputReg, ioOutputState);
  i2cWrite8(board::ioExpanderAddr, board::ioDirectionReg, enabledPins);
  i2cWrite8(board::ioExpanderAddr, board::ioHighZReg, static_cast<uint8_t>(~enabledPins));
  ioSetPin(board::ioTouchPower, true);
  delay(80);
  ioSetPin(board::ioPanelPower, true);
  delay(80);
  ioSetPin(board::ioBacklight, true);
  delay(80);
}

void setHapticMotor(bool enabled) {
  ioSetPin(board::ioVibrationMotor, enabled);
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
    firmware_update::service().noteUserActivity();
    lastX = x;
    lastY = y;
    data->state = LV_INDEV_STATE_PR;
  } else {
    data->state = LV_INDEV_STATE_REL;
  }
  data->point.x = lastX;
  data->point.y = lastY;
}

void lvTouchFeedback(lv_indev_drv_t*, uint8_t eventCode) {
  if (eventCode == LV_EVENT_CLICKED) {
    haptic_feedback::request(haptic_feedback::Pattern::Press);
  }
}

void handleShortPress() {
  haptic_feedback::request(haptic_feedback::Pattern::Press);
  ui::handleShortPress();
}

void handleLongPress() {
  if (ui::handleLongPress()) {
    haptic_feedback::request(haptic_feedback::Pattern::Back);
  }
}

void handleHomePress() {
  if (ui::handleLongPressToHome()) {
    haptic_feedback::request(haptic_feedback::Pattern::Back);
  }
}

void pollButton() {
  static bool lastRawPressed = false;
  static bool stablePressed = false;
  static bool longPressHandled = false;
  static bool homePressHandled = false;
  static uint32_t lastChangeMs = 0;
  static uint32_t pressStartedMs = 0;

  const bool rawPressed = digitalRead(board::button) == LOW;
  const uint32_t now = millis();

  if (rawPressed != lastRawPressed) {
    lastRawPressed = rawPressed;
    lastChangeMs = now;
  }

  if (rawPressed && stablePressed) {
    if (!longPressHandled &&
        (now - pressStartedMs) >= board::buttonLongPressMs) {
      longPressHandled = true;
      handleLongPress();
    } else if (longPressHandled && !homePressHandled &&
               (now - pressStartedMs) >= board::buttonHomePressMs) {
      homePressHandled = true;
      handleHomePress();
    }
  }

  if ((now - lastChangeMs) < board::buttonDebounceMs || rawPressed == stablePressed) {
    return;
  }

  stablePressed = rawPressed;
  if (stablePressed) {
    firmware_update::service().noteUserActivity();
    pressStartedMs = now;
    longPressHandled = false;
    homePressHandled = false;
  } else {
    pressStartedMs = 0;
    if (!longPressHandled) {
      handleShortPress();
    }
    longPressHandled = false;
    homePressHandled = false;
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
    touchDrv.feedback_cb = lvTouchFeedback;
    lv_indev_drv_register(&touchDrv);
  }
}

lv_obj_t* earlyRecoveryScreen = nullptr;
lv_obj_t* earlyRecoveryStatus = nullptr;
lv_obj_t* earlyRecoveryProgress = nullptr;
uint32_t earlyRecoveryUiTickMs = 0;
firmware_update::Status earlyRecoveryLastStatus = firmware_update::Status::Idle;
uint8_t earlyRecoveryLastProgress = 0xff;
bool earlyRecoveryStatusShown = false;

void showEarlyRecoveryStatus(const firmware_update::Snapshot& status) {
  const uint32_t now = millis();
  if (earlyRecoveryUiTickMs == 0) earlyRecoveryUiTickMs = now;
  lv_tick_inc(now - earlyRecoveryUiTickMs);
  earlyRecoveryUiTickMs = now;

  if (earlyRecoveryScreen == nullptr) {
    earlyRecoveryScreen = lv_obj_create(lv_scr_act());
    lv_obj_set_size(earlyRecoveryScreen, 238, 238);
    lv_obj_center(earlyRecoveryScreen);
    lv_obj_set_style_radius(earlyRecoveryScreen, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(earlyRecoveryScreen, lv_color_hex(0x080B0F), 0);
    lv_obj_set_style_border_color(earlyRecoveryScreen, lv_color_hex(0xFF9F1C), 0);
    lv_obj_set_style_border_width(earlyRecoveryScreen, 2, 0);
    lv_obj_set_style_pad_all(earlyRecoveryScreen, 0, 0);
    lv_obj_clear_flag(earlyRecoveryScreen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* title = lv_label_create(earlyRecoveryScreen);
    lv_label_set_text(title, "PREPARING UPDATE");
    lv_obj_set_style_text_font(title, UI_FONT_16, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFB347), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 55);

    earlyRecoveryStatus = lv_label_create(earlyRecoveryScreen);
    lv_obj_set_width(earlyRecoveryStatus, 176);
    lv_obj_set_style_text_align(earlyRecoveryStatus, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(earlyRecoveryStatus, UI_FONT_14, 0);
    lv_obj_set_style_text_color(earlyRecoveryStatus, lv_color_hex(0xF5F7FA), 0);
    lv_obj_align(earlyRecoveryStatus, LV_ALIGN_TOP_MID, 0, 91);

    earlyRecoveryProgress = lv_bar_create(earlyRecoveryScreen);
    lv_obj_set_size(earlyRecoveryProgress, 150, 8);
    lv_obj_align(earlyRecoveryProgress, LV_ALIGN_TOP_MID, 0, 133);
    lv_bar_set_range(earlyRecoveryProgress, 0, 100);
    lv_obj_set_style_bg_color(earlyRecoveryProgress, lv_color_hex(0x252B33), 0);
    lv_obj_set_style_bg_color(earlyRecoveryProgress, lv_color_hex(0xFF7A00),
                              LV_PART_INDICATOR);

    lv_obj_t* detail = lv_label_create(earlyRecoveryScreen);
    lv_label_set_text(detail, "Keep USB power connected");
    lv_obj_set_style_text_font(detail, UI_FONT_12, 0);
    lv_obj_set_style_text_color(detail, lv_color_hex(0xAAB2BD), 0);
    lv_obj_align(detail, LV_ALIGN_TOP_MID, 0, 157);
  }

  if (!earlyRecoveryStatusShown || status.status != earlyRecoveryLastStatus ||
      status.progressPercent != earlyRecoveryLastProgress) {
    char message[48];
    if (status.status == firmware_update::Status::Downloading) {
      std::snprintf(message, sizeof(message), "Updating recovery\n%u%%",
                    status.progressPercent);
    } else if (status.status == firmware_update::Status::Failed) {
      std::snprintf(message, sizeof(message),
                    "Update paused\nReturning to firmware");
    } else {
      std::snprintf(message, sizeof(message), "Connecting to Wi-Fi");
    }
    lv_label_set_text(earlyRecoveryStatus, message);
    lv_bar_set_value(earlyRecoveryProgress, status.progressPercent, LV_ANIM_OFF);
    earlyRecoveryLastStatus = status.status;
    earlyRecoveryLastProgress = status.progressPercent;
    earlyRecoveryStatusShown = true;
  }
  lv_timer_handler();
}

void setup() {
  DEBUG_PORT.begin(115200);
  delay(100);

  pinMode(board::button, INPUT_PULLUP);
  Wire.begin(board::i2cSda, board::i2cScl, board::i2cFreq);
  initIoExpander();
  haptic_feedback::begin(setHapticMotor);
  scanI2cBus();

  display.init();
  display.initDMA();
  display.startWrite();
  display.setColor(0, 0, 0);
  display.setRotation(0);
  display.fillScreen(TFT_BLACK);

  touchPresent = initTouch();
  setupLvgl();

  // A journaled recovery replacement runs with only display, touch, LVGL, and
  // this bounded progress screen initialized. BLE, devices, scenes, Portal,
  // Home, and the rest of the UI remain dormant so TLS and flash verification
  // retain a large contiguous heap. Recovery is selected only after its image
  // verifies; a recoverable failure falls through to the normal firmware UI.
  firmware_update::service().runEarlyRecoveryUpdate(showEarlyRecoveryStatus);
  if (earlyRecoveryScreen != nullptr) {
    lv_obj_del(earlyRecoveryScreen);
    earlyRecoveryScreen = nullptr;
    earlyRecoveryStatus = nullptr;
    earlyRecoveryProgress = nullptr;
    earlyRecoveryStatusShown = false;
  }

  studio::devices().begin();
  studio::scenes().begin();
  wifi_configuration::service().begin();
  ui::init();
  // Paint Home before the updater starts its five-second boot grace period.
  lv_timer_handler();
  firmware_update::service().begin();
  logRuntimeStats("boot");
}

void loop() {
  static uint32_t lastTickMs = millis();
  static uint32_t lastStatsMs = millis();
  static studio::LinkState lastLink = currentLink();
  const uint32_t now = millis();
  lv_tick_inc(now - lastTickMs);
  lastTickMs = now;

#if CONFIG_BLE_RUNTIME_ENABLED
  studio::ble::loopBleRuntime(now);
#endif
  studio::devices().loop();
  studio::scenes().loop(now);
  pollDebugSerial();
  pollDebugDeviceCommand();
  portal::loop();
  firmware_update::service().setRuntimeIdle(
      !portal::active() && !studio::scenes().busy() &&
          studio::devices().activeCount() == 0 &&
          !wifi_configuration::service().active(),
      ui::showingHome());
  firmware_update::service().loop();
  wifi_configuration::service().loop();
  const studio::LinkState link = currentLink();
  if (link != lastLink) {
    lastLink = link;
    logRuntimeStats("link_change");
  }
  if (now - lastStatsMs >= 30000) {
    lastStatsMs = now;
    logRuntimeStats("periodic");
  }
  haptic_feedback::loop(now);
  pollButton();
  ui::tick();

  lv_timer_handler();
  delay(5);
}
