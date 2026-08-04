#include <lvgl.h>

#include <cstdio>
#include <cstdlib>
#include <sys/stat.h>

#include "Arduino.h"
#include "core/device_manager.h"
#include "core/scene_service.h"
#include "devices/canon_ble/ui.h"
#include "devices/canon_trigger/ui.h"
#include "devices/shark_nano_ii/ui.h"
#include "devices/tascam_x8/ui.h"
#include "scene_ui.h"
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

void printLvglMemory(const char* stage) {
  lv_mem_monitor_t monitor {};
  lv_mem_monitor(&monitor);
  std::printf(
      "LVGL memory %s: %u bytes free, %u%% used, %u bytes peak used, frag %u%%\n",
      stage, static_cast<unsigned>(monitor.free_size),
      static_cast<unsigned>(monitor.used_pct),
      static_cast<unsigned>(monitor.max_used),
      static_cast<unsigned>(monitor.frag_pct));
}

}  // namespace

int main() {
  std::setbuf(stdout, nullptr);
  ensureDir("sim/screenshots");
  setupDisplay();

  studio::devices().begin();
  studio::scenes().begin();
  if (studio::devices().count() > 0) {
    const studio::DeviceRecord* record = studio::devices().at(0);
    if (record != nullptr) {
      studio::devices().rename(record->instanceId, "Slider A");
    }
  }
  studio::InstanceId canonId = studio::kInvalidInstanceId;
  studio::devices().add(studio::DriverId::CanonBle, "EOS R6 Mark III",
                        canonId);
  studio::InstanceId canonId2 = studio::kInvalidInstanceId;
  studio::devices().add(studio::DriverId::CanonBle, "Camera B", canonId2);
  studio::InstanceId canonTriggerId = studio::kInvalidInstanceId;
  studio::devices().add(studio::DriverId::CanonTrigger, "EOS R6 Trigger",
                        canonTriggerId);
  studio::InstanceId tascamId = studio::kInvalidInstanceId;
  studio::devices().add(studio::DriverId::TascamX8, "Recorder A", tascamId);
  if (canonId == studio::kInvalidInstanceId ||
      canonId2 == studio::kInvalidInstanceId ||
      canonTriggerId == studio::kInvalidInstanceId ||
      tascamId == studio::kInvalidInstanceId) {
    std::fprintf(stderr, "Failed to seed the maximum device configuration\n");
    return 1;
  }

  ui::init();
  printLvglMemory("after max-device init");

  ui::showHome();
  if (!capture("01_home")) {
    return 1;
  }

  ui::showDevices();
  if (!capture("02_devices")) {
    return 1;
  }

  if (studio::devices().remove(canonTriggerId) != studio::RegistryStatus::Ok) {
    std::fprintf(stderr, "Failed to prepare add-device picker screenshot\n");
    return 1;
  }
  ui::simShowAddDevice();
  if (!capture("03_add_device")) {
    return 1;
  }
  ui::showDevices();
  canonTriggerId = studio::kInvalidInstanceId;
  studio::devices().add(studio::DriverId::CanonTrigger, "EOS R6 Trigger",
                        canonTriggerId);
  if (canonTriggerId == studio::kInvalidInstanceId) {
    std::fprintf(stderr, "Failed to restore maximum device configuration\n");
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

  ui::handleShortPress();
  pump(250);
  if (!capture("07_shark_run")) {
    return 1;
  }

  ui::handleShortPress();
  pump(20);
  if (studio::simSharkState().runStateCode != shark::kRunStandby) {
    std::fprintf(stderr, "Hardware trigger did not put Shark in standby\n");
    return 1;
  }
  ui::handleShortPress();
  pump(20);
  if (studio::simSharkState().runStateCode != shark::kRunStart) {
    std::fprintf(stderr, "Hardware trigger did not start Shark\n");
    return 1;
  }
  ui::handleShortPress();
  pump(20);
  if (studio::simSharkState().runStateCode != shark::kRunStop) {
    std::fprintf(stderr, "Hardware trigger did not stop Shark\n");
    return 1;
  }
  shark_ui::simShowKeypoints();
  pump(250);
  shark_ui::simShowKeypointSettings(2);
  if (!capture("08_shark_key_settings")) {
    return 1;
  }

  shark_ui::simShowPositionChoice(1);
  if (!capture("09_shark_position_choice")) {
    return 1;
  }

  shark_ui::simShowManualPositioning();
  if (!capture("10_shark_position_manual")) {
    return 1;
  }

  shark_ui::simShowPositionChoice(1);
  shark_ui::simShowJoystickPositioning();
  if (!capture("11_shark_position_joystick")) {
    return 1;
  }

  if (canonId == studio::kInvalidInstanceId) {
    std::fprintf(stderr, "No Canon instance for screenshots\n");
    return 1;
  }
  shark_ui::hide();
  canon_ble_ui::show(canonId);
  studio::simSetCanonConnectedState(false);
  pump(250);
  if (!capture("12_canon_ready")) {
    return 1;
  }

  ui::handleShortPress();
  pump(20);
  if (studio::simCanonState().recording !=
          canon_ble::CanonBleState::Recording::Recording ||
      !canon_ble_ui::active()) {
    std::fprintf(stderr, "Hardware trigger did not start Canon recording\n");
    return 1;
  }
  if (!capture("13_canon_recording")) {
    return 1;
  }

  ui::handleShortPress();
  pump(20);
  if (studio::simCanonState().recording !=
      canon_ble::CanonBleState::Recording::Stopped) {
    std::fprintf(stderr, "Hardware trigger did not stop Canon recording\n");
    return 1;
  }
  studio::simSetCanonConnectedState(false, false);
  pump(250);
  if (!capture("14_canon_unknown")) {
    return 1;
  }

  studio::DeviceCommand powerOff;
  powerOff.instanceId = canonId;
  powerOff.type = studio::CommandType::CameraPowerOff;
  studio::devices().enqueue(powerOff);
  pump(20);
  if (studio::simCanonState().phase !=
      canon_ble::CanonBleState::Phase::PoweredOff) {
    std::fprintf(stderr, "Canon power-off command was not routed\n");
    return 1;
  }
  if (!capture("15_canon_powered_off")) {
    return 1;
  }

  studio::DeviceCommand powerOn;
  powerOn.instanceId = canonId;
  powerOn.type = studio::CommandType::CameraPowerOn;
  studio::devices().enqueue(powerOn);
  pump(20);
  if (studio::simCanonState().phase !=
      canon_ble::CanonBleState::Phase::Ready) {
    std::fprintf(stderr, "Canon power-on command was not routed\n");
    return 1;
  }
  if (!capture("16_canon_powered_on")) {
    return 1;
  }

  canon_ble_ui::hide();
  canon_trigger_ui::show(canonTriggerId);
  studio::simSetCanonTriggerConnectedState();
  pump(250);
  if (!capture("17_canon_trigger_ready")) {
    return 1;
  }
  ui::handleShortPress();
  pump(20);
  if (studio::simCanonTriggerState().triggerCount < 1 ||
      !canon_trigger_ui::active()) {
    std::fprintf(stderr, "Hardware trigger did not fire Canon Trigger\n");
    return 1;
  }
  if (!capture("18_canon_trigger_sent")) {
    return 1;
  }
  canon_trigger_ui::hide();

  tascam_x8_ui::show(tascamId);
  studio::simSetTascamConnectedState(false);
  pump(250);
  if (!capture("19_tascam_ready")) {
    return 1;
  }

  ui::handleShortPress();
  pump(20);
  if (studio::simTascamState().recording !=
          tascam_x8::TascamX8State::Recording::Recording ||
      !tascam_x8_ui::active()) {
    std::fprintf(stderr, "Hardware trigger did not start Tascam recording\n");
    return 1;
  }
  if (!capture("20_tascam_recording")) {
    return 1;
  }

  ui::handleShortPress();
  pump(20);
  if (studio::simTascamState().recording !=
      tascam_x8::TascamX8State::Recording::Stopped) {
    std::fprintf(stderr, "Hardware trigger did not stop Tascam recording\n");
    return 1;
  }
  tascam_x8_ui::hide();

  studio::SceneId sceneId = studio::kInvalidSceneId;
  if (!studio::scenes().seedPressRecord(sceneId)) {
    std::fprintf(stderr, "Failed to seed Press Record sequence\n");
    return 1;
  }
  scene_ui::simShowList();
  pump(200);
  if (!capture("21_scenes_list")) {
    return 1;
  }
  scene_ui::simShowEditStart(sceneId);
  pump(200);
  if (!capture("22_scenes_edit_start")) {
    return 1;
  }
  scene_ui::simShowAddStepCategory(sceneId);
  pump(200);
  if (!capture("22b_scenes_add_category")) {
    return 1;
  }
  scene_ui::simShowAddStepDevice(sceneId, studio::DeviceType::Camera);
  pump(200);
  if (!capture("22c_scenes_add_device")) {
    return 1;
  }
  scene_ui::simShowAddStepAction(sceneId, canonId);
  pump(200);
  if (!capture("22d_scenes_add_action")) {
    return 1;
  }
  scene_ui::simShowEditStop(sceneId);
  pump(200);
  if (!capture("23_scenes_edit_stop")) {
    return 1;
  }
  scene_ui::simShowSettings(sceneId);
  pump(200);
  if (!capture("23b_scenes_settings")) {
    return 1;
  }
  scene_ui::simShowRun(sceneId);
  studio::simSetSequenceConnectedState();
  for (int i = 0; i < 10; ++i) {
    studio::devices().loop();
    studio::scenes().loop(static_cast<uint32_t>(i * 20));
    pump(20);
  }
  if (studio::scenes().progress().phase != studio::ScenePhase::Ready) {
    std::fprintf(stderr, "Sequence did not reach Ready after prepare\n");
    return 1;
  }
  if (!capture("24_scenes_run_ready")) {
    return 1;
  }
  if (studio::scenes().start(sceneId) != studio::SceneRunStatus::Ok) {
    std::fprintf(stderr, "Failed to start Press Record sequence\n");
    return 1;
  }
  for (int i = 0; i < 40; ++i) {
    studio::devices().loop();
    studio::scenes().loop(static_cast<uint32_t>(i * 20));
    pump(20);
  }
  if (!capture("25_scenes_start_progress")) {
    return 1;
  }
  for (int i = 40; i < 80; ++i) {
    studio::devices().loop();
    studio::scenes().loop(static_cast<uint32_t>(i * 20));
    pump(20);
  }
  if (studio::scenes().progress().phase != studio::ScenePhase::IdleArmed) {
    std::fprintf(stderr, "Sequence did not reach armed/recording hold\n");
    return 1;
  }
  if (!capture("26_scenes_armed")) {
    return 1;
  }
  if (studio::scenes().stop() != studio::SceneRunStatus::Ok) {
    std::fprintf(stderr, "Failed to stop Press Record sequence\n");
    return 1;
  }
  studio::simSetSequenceConnectedState();
  for (int i = 80; i < 100; ++i) {
    studio::devices().loop();
    studio::scenes().loop(static_cast<uint32_t>(i * 20));
    pump(20);
  }
  if (!capture("27_scenes_stop_progress")) {
    return 1;
  }
  scene_ui::hide();
  ui::showHome();

  if (studio::devices().remove(canonTriggerId) != studio::RegistryStatus::Ok) {
    std::fprintf(stderr, "Failed to remove a device in simulator regression\n");
    return 1;
  }
  ui::showDevices();
  pump(200);
  printLvglMemory("after remove refresh");

  std::printf("UI simulator captures complete.\n");
  return 0;
}
