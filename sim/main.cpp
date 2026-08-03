#include <lvgl.h>

#include <cstdio>
#include <cstdlib>
#include <sys/stat.h>

#include "Arduino.h"
#include "core/device_manager.h"
#include "devices/shark_nano_ii/ui.h"
#include "sim_runtime.h"
#include "ui.h"

namespace {

constexpr int kWidth = 240;
constexpr int kHeight = 240;

lv_color_t gFb[kWidth * kHeight];
lv_disp_draw_buf_t gDrawBuf;
lv_disp_drv_t gDispDrv;

void flushCb(lv_disp_drv_t* disp, const lv_area_t*, lv_color_t*) {
  lv_disp_flush_ready(disp);
}

void ensureDir(const char* path) {
  struct stat st {};
  if (stat(path, &st) != 0) {
    mkdir(path, 0755);
  }
}

void rgb565ToRgb(uint16_t pixel, uint8_t& r, uint8_t& g, uint8_t& b) {
  r = static_cast<uint8_t>(((pixel >> 11) & 0x1F) * 255 / 31);
  g = static_cast<uint8_t>(((pixel >> 5) & 0x3F) * 255 / 63);
  b = static_cast<uint8_t>((pixel & 0x1F) * 255 / 31);
}

bool writePpm(const char* path) {
  FILE* file = std::fopen(path, "wb");
  if (file == nullptr) {
    std::perror(path);
    return false;
  }
  std::fprintf(file, "P6\n%d %d\n255\n", kWidth, kHeight);
  for (int i = 0; i < kWidth * kHeight; ++i) {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    rgb565ToRgb(gFb[i].full, r, g, b);
    std::fputc(r, file);
    std::fputc(g, file);
    std::fputc(b, file);
  }
  std::fclose(file);
  return true;
}

bool convertToRoundPng(const char* ppmPath, const char* pngPath) {
  char command[512];
  std::snprintf(
      command, sizeof(command),
      "magick '%s' \\( +clone -fill black -colorize 100 -fill white "
      "-draw 'circle 120,120 120,0' \\) -alpha off -compose CopyOpacity -composite '%s'",
      ppmPath, pngPath);
  const int status = std::system(command);
  if (status != 0) {
    // Fall back to a square PNG if ImageMagick mask composition fails.
    std::snprintf(command, sizeof(command), "magick '%s' '%s'", ppmPath, pngPath);
    return std::system(command) == 0;
  }
  return true;
}

void pump(uint32_t ms) {
  simAdvanceMillis(ms);
  lv_tick_inc(ms);
  ui::tick();
  shark_ui::tick();
  studio::devices().loop();
  lv_timer_handler();
}

bool capture(const char* stem) {
  pump(200);
  pump(200);

  char ppmPath[256];
  char pngPath[256];
  std::snprintf(ppmPath, sizeof(ppmPath), "sim/screenshots/%s.ppm", stem);
  std::snprintf(pngPath, sizeof(pngPath), "sim/screenshots/%s.png", stem);
  if (!writePpm(ppmPath)) {
    return false;
  }
  if (!convertToRoundPng(ppmPath, pngPath)) {
    std::fprintf(stderr, "Failed to convert %s\n", ppmPath);
    return false;
  }
  std::printf("wrote %s\n", pngPath);
  return true;
}

void setupDisplay() {
  lv_init();
  lv_disp_draw_buf_init(&gDrawBuf, gFb, nullptr, kWidth * kHeight);
  lv_disp_drv_init(&gDispDrv);
  gDispDrv.hor_res = kWidth;
  gDispDrv.ver_res = kHeight;
  gDispDrv.flush_cb = flushCb;
  gDispDrv.draw_buf = &gDrawBuf;
  gDispDrv.full_refresh = 1;
  lv_disp_drv_register(&gDispDrv);
}

}  // namespace

int main() {
  ensureDir("sim/screenshots");
  setupDisplay();

  studio::devices().begin();
  if (studio::devices().count() > 0) {
    const studio::DeviceRecord* record = studio::devices().at(0);
    if (record != nullptr) {
      studio::devices().rename(record->instanceId, "Slider A");
    }
  }

  ui::init();

  ui::showHome();
  if (!capture("01_home")) {
    return 1;
  }

  ui::showDevices();
  if (!capture("02_devices")) {
    return 1;
  }

  const studio::InstanceId id =
      studio::devices().count() > 0 ? studio::devices().at(0)->instanceId
                                    : studio::kInvalidInstanceId;
  if (id == studio::kInvalidInstanceId) {
    std::fprintf(stderr, "No seeded Shark instance for screenshots\n");
    return 1;
  }

  ui::simShowManage(id);
  if (!capture("03_manage")) {
    return 1;
  }

  ui::simShowRename(id);
  if (!capture("04_rename")) {
    return 1;
  }

  ui::showDevices();
  studio::simSetScanningState();
  shark_ui::show(id);
  if (!capture("05_shark_connect")) {
    return 1;
  }

  studio::simSetConnectedDemoState();
  pump(200);
  if (!capture("06_shark_keys")) {
    return 1;
  }

  shark_ui::handleShortPress();
  pump(250);
  if (!capture("07_shark_run")) {
    return 1;
  }

  std::printf("UI simulator captures complete.\n");
  return 0;
}
