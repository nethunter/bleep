#include <lvgl.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>

#include "Arduino.h"
#include "core/device_manager.h"
#include "core/factory_reset.h"
#include "core/scene_service.h"
#include "devices/canon_ble/ui.h"
#include "devices/canon_trigger/ui.h"
#include "devices/shark_nano_ii/ui.h"
#include "devices/tascam_x8/ui.h"
#include "devices/home_assistant/ui.h"
#include "devices/home_assistant/client.h"
#include "devices/amaran_light/ui.h"
#include "devices/amaran_light/runtime.h"
#include "devices/zhiyun_x100/ui.h"
#include "haptic_feedback.h"
#include "portal_service.h"
#include "scene_ui.h"
#include "sim_runtime.h"
#include "ui.h"
#include "ui/picker_shell.h"

namespace {

constexpr int kWidth = 240;
constexpr int kHeight = 240;

lv_color_t gFb[kWidth * kHeight];
lv_disp_draw_buf_t gDrawBuf;
lv_disp_drv_t gDispDrv;
bool gHapticStates[8] = {};
uint8_t gHapticStateCount = 0;

void hapticOutput(bool enabled) {
  if (gHapticStateCount < sizeof(gHapticStates) / sizeof(gHapticStates[0])) {
    gHapticStates[gHapticStateCount++] = enabled;
  }
}

bool expectHapticStates(const bool* expected, uint8_t count) {
  if (gHapticStateCount != count) {
    return false;
  }
  for (uint8_t i = 0; i < count; ++i) {
    if (gHapticStates[i] != expected[i]) {
      return false;
    }
  }
  return true;
}

void advanceHaptic(uint32_t ms) {
  simAdvanceMillis(ms);
  haptic_feedback::loop(millis());
}

bool verifyHapticPatterns() {
  haptic_feedback::begin(hapticOutput);
  gHapticStateCount = 0;
  haptic_feedback::request(haptic_feedback::Pattern::Press);
  advanceHaptic(19);
  advanceHaptic(1);
  const bool pressExpected[] = {true, false};
  if (!expectHapticStates(pressExpected, 2)) return false;

  haptic_feedback::begin(hapticOutput);
  gHapticStateCount = 0;
  haptic_feedback::request(haptic_feedback::Pattern::Connected);
  advanceHaptic(12);
  advanceHaptic(24);
  advanceHaptic(12);
  const bool connectedExpected[] = {true, false, true, false};
  if (!expectHapticStates(connectedExpected, 4)) return false;

  haptic_feedback::begin(hapticOutput);
  gHapticStateCount = 0;
  haptic_feedback::request(haptic_feedback::Pattern::Press);
  haptic_feedback::request(haptic_feedback::Pattern::Back);
  advanceHaptic(15);
  advanceHaptic(35);
  advanceHaptic(30);
  const bool backExpected[] = {true, false, true, false};
  if (!expectHapticStates(backExpected, 4)) return false;

  haptic_feedback::begin(hapticOutput);
  gHapticStateCount = 0;
  haptic_feedback::request(haptic_feedback::Pattern::Error);
  haptic_feedback::request(haptic_feedback::Pattern::Press);
  haptic_feedback::request(haptic_feedback::Pattern::Connected);
  advanceHaptic(60);
  advanceHaptic(45);
  advanceHaptic(60);
  advanceHaptic(12);
  advanceHaptic(24);
  advanceHaptic(12);
  const bool errorExpected[] = {true, false, true, false,
                                true, false, true, false};
  return expectHapticStates(errorExpected, 8);
}

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
  haptic_feedback::loop(millis());
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
  if (!verifyHapticPatterns()) {
    std::fprintf(stderr, "Haptic pattern regression failed\n");
    return 1;
  }
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
  ui::init();
  ui::showDevices();
  if (!capture("00_devices_unpaged")) {
    return 1;
  }
  studio::InstanceId canonId = studio::kInvalidInstanceId;
  studio::devices().add(studio::DriverId::CanonBle, "EOS R6 Mark III",
                        canonId);
  studio::InstanceId canonId2 = studio::kInvalidInstanceId;
  studio::devices().add(studio::DriverId::CanonBle, "Camera B", canonId2);
  studio::InstanceId canonId3 = studio::kInvalidInstanceId;
  studio::devices().add(studio::DriverId::CanonBle, "Camera C", canonId3);
  studio::InstanceId canonTriggerId = studio::kInvalidInstanceId;
  studio::devices().add(studio::DriverId::CanonTrigger, "EOS R6 Trigger",
                        canonTriggerId);
  studio::InstanceId canonTriggerId2 = studio::kInvalidInstanceId;
  studio::InstanceId canonTriggerId3 = studio::kInvalidInstanceId;
  studio::devices().add(studio::DriverId::CanonTrigger, "Trigger B",
                        canonTriggerId2);
  studio::devices().add(studio::DriverId::CanonTrigger, "Trigger C",
                        canonTriggerId3);
  studio::InstanceId tascamId = studio::kInvalidInstanceId;
  studio::devices().add(studio::DriverId::TascamX8, "Recorder A", tascamId);
  studio::InstanceId haLight = studio::kInvalidInstanceId;
  studio::devices().addHomeAssistantEntity(studio::HomeAssistantDomain::Light,
                                           "light.key_light", "Key Light",
                                           haLight);
  studio::InstanceId haInputBoolean = studio::kInvalidInstanceId;
  studio::devices().addHomeAssistantEntity(
      studio::HomeAssistantDomain::InputBoolean,
      "input_boolean.live", "Studio Live", haInputBoolean);
  studio::InstanceId haButton = studio::kInvalidInstanceId;
  studio::devices().addHomeAssistantEntity(studio::HomeAssistantDomain::Button,
                                           "button.slate", "Slate",
                                           haButton);
  studio::InstanceId haScene = studio::kInvalidInstanceId;
  studio::devices().addHomeAssistantEntity(studio::HomeAssistantDomain::Scene,
                                           "scene.studio_ready", "Studio Ready",
                                           haScene);
  studio::InstanceId pano60Id = studio::kInvalidInstanceId;
  studio::InstanceId pano120Id = studio::kInvalidInstanceId;
  studio::InstanceId ace25Id = studio::kInvalidInstanceId;
  studio::InstanceId zhiyunId = studio::kInvalidInstanceId;
  studio::devices().add(studio::DriverId::AmaranLight, "Amaran Key", pano60Id);
  studio::devices().add(studio::DriverId::AmaranLight, "Amaran Fill", pano120Id);
  studio::devices().add(studio::DriverId::AmaranLight, "Amaran Rim", ace25Id);
  studio::InstanceId amaranFourthId = studio::kInvalidInstanceId;
  studio::devices().add(studio::DriverId::AmaranLight, "Amaran Background",
                        amaranFourthId);
  studio::devices().add(studio::DriverId::ZhiyunLight, "MOLUS X100", zhiyunId);
  studio::InstanceId zhiyunId2 = studio::kInvalidInstanceId;
  studio::InstanceId zhiyunId3 = studio::kInvalidInstanceId;
  studio::InstanceId zhiyunId4 = studio::kInvalidInstanceId;
  studio::devices().add(studio::DriverId::ZhiyunLight, "MOLUS X60RGB",
                        zhiyunId2);
  studio::devices().add(studio::DriverId::ZhiyunLight, "Zhiyun Fill",
                        zhiyunId3);
  studio::devices().add(studio::DriverId::ZhiyunLight, "Zhiyun Rim",
                        zhiyunId4);
  if (canonId == studio::kInvalidInstanceId ||
      canonId2 == studio::kInvalidInstanceId ||
      canonId3 == studio::kInvalidInstanceId ||
      canonTriggerId == studio::kInvalidInstanceId ||
      canonTriggerId2 == studio::kInvalidInstanceId ||
      canonTriggerId3 == studio::kInvalidInstanceId ||
      tascamId == studio::kInvalidInstanceId ||
      haLight == studio::kInvalidInstanceId ||
      haInputBoolean == studio::kInvalidInstanceId ||
      haButton == studio::kInvalidInstanceId ||
      haScene == studio::kInvalidInstanceId ||
      pano60Id == studio::kInvalidInstanceId ||
      pano120Id == studio::kInvalidInstanceId ||
      ace25Id == studio::kInvalidInstanceId ||
      amaranFourthId == studio::kInvalidInstanceId ||
      zhiyunId == studio::kInvalidInstanceId ||
      zhiyunId2 == studio::kInvalidInstanceId ||
      zhiyunId3 == studio::kInvalidInstanceId ||
      zhiyunId4 == studio::kInvalidInstanceId) {
    std::fprintf(stderr,
                 "Failed to seed the maximum compiled-driver configuration\n");
    return 1;
  }

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
  ui::showDevices();
  if (!ui::simAddDeviceAtListEnd()) {
    std::fprintf(stderr, "Add device is not the final device-list row\n");
    return 1;
  }
  ui::simShowAddDevice();
  if (!capture("03_add_device")) {
    return 1;
  }
  const size_t countBeforeOnboarding = studio::devices().count();
  picker_shell::simChooseDriver(studio::DriverId::CanonTrigger);
  const studio::InstanceId canceledAdd = studio::devices().pendingAdd();
  if (canceledAdd == studio::kInvalidInstanceId ||
      studio::devices().count() != countBeforeOnboarding ||
      !studio::devices().ownedBy(canceledAdd,
                                 studio::ConnectionOwner::Foreground)) {
    std::fprintf(stderr, "Add device did not open provisional pairing\n");
    return 1;
  }
  if (!capture("03a_add_device_pairing")) {
    return 1;
  }
  ui::handleLongPress();
  if (studio::devices().pendingAdd() != studio::kInvalidInstanceId ||
      studio::devices().count() != countBeforeOnboarding) {
    std::fprintf(stderr, "Back did not discard provisional device\n");
    return 1;
  }

  ui::simShowAddDevice();
  picker_shell::simChooseDriver(studio::DriverId::CanonTrigger);
  canonTriggerId = studio::devices().pendingAdd();
  studio::simSetCanonTriggerConnectedState();
  studio::devices().loop();
  ui::tick();
  if (canonTriggerId == studio::kInvalidInstanceId ||
      studio::devices().pendingAdd() != studio::kInvalidInstanceId ||
      studio::devices().count() != countBeforeOnboarding + 1) {
    std::fprintf(stderr, "Ready pairing did not commit provisional device\n");
    return 1;
  }
  // Reload the now-committed instance so the headless full-refresh framebuffer
  // redraws static chrome as well as the status/button changes.
  ui::showDevices();
  ui::showDevice(canonTriggerId);
  if (!capture("03b_add_device_ready")) {
    return 1;
  }
  ui::showDevices();

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
  ui::showDevice(id);
  if (!capture("05_shark_connect")) {
    return 1;
  }

  studio::simSetConnectedDemoState();
  gHapticStateCount = 0;
  pump(1);
  pump(12);
  pump(24);
  pump(12);
  const bool deviceConnectedExpected[] = {true, false, true, false};
  if (!expectHapticStates(deviceConnectedExpected, 4)) {
    std::fprintf(stderr, "Device Ready did not request Connected haptic\n");
    return 1;
  }
  if (!capture("06_shark_keys")) {
    return 1;
  }

  ui::showDevices();
  pump(1);
  gHapticStateCount = 0;
  ui::showDevice(id);
  pump(1);
  pump(12);
  pump(24);
  pump(12);
  if (!expectHapticStates(deviceConnectedExpected, 4)) {
    std::fprintf(stderr,
                 "Opening retained Ready device did not request Connected haptic\n");
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

  canon_ble_ui::hide();
  canon_ble_ui::show(canonId);
  pump(20);
  if (studio::simCanonState().phase !=
      canon_ble::CanonBleState::Phase::Ready) {
    std::fprintf(stderr, "Reopening powered-off Canon did not wake it\n");
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

  ui::simShowManage(tascamId);
  pump(100);
  if (!capture("20b_tascam_manage_recording")) {
    return 1;
  }
  ui::simRequestManagedDisconnect();
  pump(50);
  if (!capture("20c_tascam_disconnect_confirm")) {
    return 1;
  }

  tascam_x8_ui::hide();
  ui::showHome();
  amaran_light_ui::show(pano60Id);
  pump(300);
  if (!capture("20d_amaran_pairing")) return 1;
  amaran_light::runtime()->simSetPhase(
      pano60Id, amaran_light::AmaranLightState::Phase::Ready);
  pump(300);
  studio::DeviceCommand lightOn;
  lightOn.instanceId = pano60Id;
  lightOn.type = studio::CommandType::TurnOn;
  studio::devices().enqueue(lightOn);
  pump(20);
  amaran_light_ui::simSetCctLook(4300, 120, 72);
  pump(400);
  const amaran_light::AmaranLightState* lightState =
      amaran_light::runtime()->state(pano60Id);
  if (lightState == nullptr || lightState->kelvin != 4300 ||
      lightState->tintPermille != 120 || lightState->cctBrightness != 72) {
    std::fprintf(stderr, "Amaran CCT draft was not applied\n");
    return 1;
  }
  amaran_light_ui::simSetRgbLook(0x3366ff, 38);
  pump(400);
  lightState = amaran_light::runtime()->state(pano60Id);
  if (lightState == nullptr || lightState->rgb == 0xffffff ||
      lightState->rgbBrightness != 38) {
    std::fprintf(stderr, "Amaran RGB draft was not applied\n");
    return 1;
  }
  amaran_light_ui::simShowCct();
  pump(400);
  lightState = amaran_light::runtime()->state(pano60Id);
  if (lightState == nullptr || lightState->kelvin != 4300 ||
      lightState->tintPermille != 120 || lightState->cctBrightness != 72) {
    std::fprintf(stderr, "Amaran CCT look was not recalled\n");
    return 1;
  }
  if (!capture("20e_amaran_cct_optimistic")) return 1;
  amaran_light_ui::simShowRgb();
  pump(400);
  lightState = amaran_light::runtime()->state(pano60Id);
  if (lightState == nullptr || lightState->rgb == 0xffffff ||
      lightState->rgbBrightness != 38 ||
      lightState->mode != amaran_light::AmaranLightState::Mode::Rgb) {
    std::fprintf(stderr, "Amaran RGB look was not recalled\n");
    return 1;
  }
  if (!capture("20f_amaran_rgb")) return 1;
  amaran_light_ui::hide();

  zhiyun_x100_ui::show(zhiyunId);
  pump(300);
  if (!capture("20g_zhiyun_x100_confirmed")) {
    return 1;
  }
  studio::simZhiyunState().model = zhiyun_x100::MolusModel::X60Rgb;
  studio::simZhiyunState().mode = zhiyun_x100::X100State::Mode::Rgb;
  studio::simZhiyunState().rgb = 0x0066ff;
  studio::simZhiyunState().saturation = 100;
  studio::simZhiyunState().brightness = 42.0f;
  studio::devices().rename(zhiyunId, "MOLUS X60RGB");
  zhiyun_x100_ui::simShowRgb();
  pump(300);
  if (!capture("20h_zhiyun_x60rgb_confirmed")) {
    return 1;
  }
  zhiyun_x100_ui::hide();
  ui::showHome();
  tascam_x8_ui::show(tascamId);
  pump(100);
  if (studio::simTascamState().recording !=
      tascam_x8::TascamX8State::Recording::Recording) {
    std::fprintf(stderr, "Retained Tascam session was not reused\n");
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
  const studio::SceneRecord* seededScene = studio::scenes().find(sceneId);
  if (seededScene == nullptr) {
    std::fprintf(stderr, "Seeded sequence is missing\n");
    return 1;
  }
  const studio::SceneRecord originalScene = *seededScene;
  studio::SceneRecord haVisualScene = originalScene;
  haVisualScene.startSteps[haVisualScene.startCount++] =
      studio::makeActionStep(haInputBoolean, studio::CommandType::TurnOn);
  haVisualScene.stopSteps[haVisualScene.stopCount++] =
      studio::makeActionStep(haInputBoolean, studio::CommandType::TurnOff);
  if (studio::scenes().replace(haVisualScene) !=
      studio::SceneRegistryStatus::Ok) {
    std::fprintf(stderr, "Failed to add HA visual-regression target\n");
    return 1;
  }
  scene_ui::simShowRun(sceneId);
  pump(200);
  if (!capture("20g_sequence_ha_switch_status_ring")) {
    return 1;
  }
  studio::scenes().cancel();
  if (studio::scenes().replace(originalScene) !=
      studio::SceneRegistryStatus::Ok) {
    std::fprintf(stderr, "Failed to restore seeded sequence\n");
    return 1;
  }
  scene_ui::simShowList();
  pump(200);
  if (!capture("21_scenes_list")) {
    return 1;
  }
  scene_ui::simShowEditStart(sceneId);
  pump(200);
  if (!scene_ui::simAddStepAtListEnd()) {
    std::fprintf(stderr, "Add step is not the final step-list row\n");
    return 1;
  }
  if (!capture("22_scenes_edit_start")) {
    return 1;
  }
  scene_ui::simEditStep(sceneId, true, 1);
  pump(200);
  if (!capture("22a_scenes_edit_wait")) {
    return 1;
  }
  picker_shell::simSaveWait(750);
  const studio::SceneRecord* editedScene = studio::scenes().find(sceneId);
  if (editedScene == nullptr || editedScene->startCount != 3 ||
      editedScene->startSteps[1].type != studio::SceneStepType::Wait ||
      editedScene->startSteps[1].waitMs != 750) {
    std::fprintf(stderr, "Sequence wait editor did not replace the step\n");
    return 1;
  }
  scene_ui::simEditStep(sceneId, true, 0);
  if (!picker_shell::handleBack() || picker_shell::active() ||
      !scene_ui::simShowingEdit()) {
    std::fprintf(stderr,
                 "Back from step settings did not return to the step list\n");
    return 1;
  }
  studio::SceneRecord colorEditScene = *editedScene;
  colorEditScene.startSteps[1] = studio::makeActionStep(
      pano60Id, studio::CommandType::SetLightCct, 5600, 50, 0);
  if (studio::scenes().replace(colorEditScene) !=
      studio::SceneRegistryStatus::Ok) {
    std::fprintf(stderr, "Failed to prepare color-step edit regression\n");
    return 1;
  }
  scene_ui::simEditStep(sceneId, true, 1);
  picker_shell::simSaveLightCct(4300, 72, 120);
  const studio::SceneRecord* colorEditedScene = studio::scenes().find(sceneId);
  if (colorEditedScene == nullptr || colorEditedScene->startCount != 3 ||
      colorEditedScene->startSteps[1].command !=
          studio::CommandType::SetLightCct ||
      colorEditedScene->startSteps[1].value0 != 4300 ||
      colorEditedScene->startSteps[1].value1 != 72 ||
      colorEditedScene->startSteps[1].value2 != 120) {
    std::fprintf(stderr, "Sequence color editor did not replace the step\n");
    return 1;
  }
  colorEditScene = *colorEditedScene;
  colorEditScene.startSteps[1] = studio::makeWaitStep(750);
  if (studio::scenes().replace(colorEditScene) !=
      studio::SceneRegistryStatus::Ok) {
    std::fprintf(stderr, "Failed to restore wait after edit regression\n");
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
  scene_ui::simShowAddStepAction(sceneId, pano60Id);
  pump(200);
  if (!capture("22d_scenes_add_action")) {
    return 1;
  }
  picker_shell::simShowLightColor(picker_shell::Mode::SceneStep, pano60Id,
                                  false);
  pump(200);
  if (!capture("22e_scenes_set_color_cct")) {
    return 1;
  }
  picker_shell::simShowLightColor(picker_shell::Mode::SceneStep, pano60Id,
                                  true);
  pump(200);
  if (!capture("22f_scenes_set_color_rgb")) {
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
  // Exercise the initially-disconnected preparation and timeout path even
  // though earlier device screenshots now leave protocol-ready links retained.
  studio::devices().deactivateAll();
  scene_ui::simShowRun(sceneId);
  pump(200);
  if (!capture("24a_scenes_run_connecting")) {
    return 1;
  }
  studio::scenes().loop(1);
  studio::scenes().loop(CONFIG_SCENE_CONNECT_TIMEOUT_MS + 2);
  pump(200);
  if (studio::scenes().progress().phase != studio::ScenePhase::Failed) {
    std::fprintf(stderr, "Sequence did not reach terminal connection failure\n");
    return 1;
  }
  if (!capture("24aa_scenes_connect_failed")) {
    return 1;
  }
  if (studio::scenes().prepare(sceneId) != studio::SceneRunStatus::Ok) {
    std::fprintf(stderr, "Sequence did not restart preparation after failure\n");
    return 1;
  }
  gHapticStateCount = 0;
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
  pump(60);
  const bool sequenceConnectedExpected[] = {true, false, true, false};
  if (!expectHapticStates(sequenceConnectedExpected, 4)) {
    std::fprintf(stderr, "Sequence Ready did not request Connected haptic\n");
    return 1;
  }
  if (!capture("24_scenes_run_ready")) {
    return 1;
  }
  const size_t heldDeviceCount = studio::devices().activeCount();
  scene_ui::simOpenDeviceControl(canonId);
  pump(200);
  if (!canon_ble_ui::active() || !scene_ui::deviceControlOpen() ||
      studio::devices().activeCount() != heldDeviceCount ||
      !studio::devices().isActive(tascamId)) {
    std::fprintf(stderr,
                 "Sequence device control did not preserve held links\n");
    return 1;
  }
  if (!capture("24b_sequence_camera_control")) {
    return 1;
  }
  ui::handleLongPress();
  pump(200);
  if (canon_ble_ui::active() || scene_ui::deviceControlOpen() ||
      studio::devices().activeCount() != heldDeviceCount ||
      !studio::devices().isActive(canonId) ||
      !studio::devices().isActive(tascamId)) {
    std::fprintf(stderr,
                 "Returning to sequence released a held device link\n");
    return 1;
  }
  studio::simCanonState().link =
      canon_ble::CanonBleState::Link::Disconnected;
  studio::simCanonState().phase =
      canon_ble::CanonBleState::Phase::PoweredOff;
  pump(200);
  if (!capture("24c_scenes_camera_disconnected")) {
    return 1;
  }
  studio::simSetSequenceConnectedState();
  pump(200);
  ui::handleShortPress();
  if (studio::scenes().progress().phase != studio::ScenePhase::RunningStart) {
    std::fprintf(stderr, "Hardware trigger did not start Press Record sequence\n");
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
  ui::handleShortPress();
  if (studio::scenes().progress().phase != studio::ScenePhase::RunningStop) {
    std::fprintf(stderr, "Hardware trigger did not stop Press Record sequence\n");
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
  scene_ui::simShowSettings(sceneId);
  pump(200);
  printLvglMemory("after sequence stop settings");
  if (!capture("27b_scenes_settings_after_stop")) {
    return 1;
  }
  // The first long-press stage performs one Back step. Continuing to hold
  // safely unwinds the remaining navigation path and lands on Home.
  ui::handleLongPress();
  pump(200);
  if (!scene_ui::active() || scene_ui::simShowingList() ||
      !studio::scenes().holdsLinks()) {
    std::fprintf(stderr,
                 "First long-press stage did more than one Back step\n");
    return 1;
  }
  ui::handleLongPressToHome();
  pump(200);
  if (scene_ui::active() || !ui::simShowingHome() ||
      studio::scenes().holdsLinks() ||
      studio::devices().ownedBy(canonId, studio::ConnectionOwner::Sequence) ||
      studio::devices().ownedBy(tascamId,
                                studio::ConnectionOwner::Sequence)) {
    std::fprintf(stderr,
                 "Continued long press did not return Home and unlink\n");
    return 1;
  }
  if (ui::handleLongPressToHome()) {
    std::fprintf(stderr, "Continued long press should stop at Home\n");
    return 1;
  }

  scene_ui::simShowRun(sceneId);
  scene_ui::simShowSettings(sceneId);
  if (studio::scenes().progress().phase != studio::ScenePhase::Connecting) {
    std::fprintf(stderr,
                 "Delete regression did not begin from sequence preparation\n");
    return 1;
  }
  scene_ui::simDeleteCurrentScene();
  if (studio::scenes().find(sceneId) != nullptr ||
      studio::scenes().holdsLinks() ||
      studio::devices().ownedBy(canonId, studio::ConnectionOwner::Sequence) ||
      studio::devices().ownedBy(tascamId, studio::ConnectionOwner::Sequence)) {
    std::fprintf(stderr,
                 "Delete did not cancel preparation and remove sequence\n");
    return 1;
  }
  scene_ui::hide();
  ui::showHome();

  home_assistant_ui::show(haLight);
  if (!capture("28_ha_light")) {
    return 1;
  }
  auto* haState = const_cast<home_assistant::EntityState*>(
      static_cast<const home_assistant::EntityState*>(
          studio::devices().specializedState(haLight)));
  if (haState == nullptr) return 1;
  haState->stateKnown = false;
  if (!capture("28b_ha_unknown")) return 1;
  haState->available = false;
  if (!capture("28c_ha_unavailable")) return 1;
  haState->available = true;
  haState->commandPending = true;
  if (!capture("28d_ha_pending")) return 1;
  haState->commandPending = false;
  haState->commandAttempted = true;
  haState->lastCommand = studio::CommandStatus::QueueFull;
  if (!capture("28e_ha_error")) return 1;
  home_assistant_ui::hide();
  home_assistant_ui::show(haInputBoolean);
  if (!capture("28f_ha_input_boolean")) {
    return 1;
  }
  auto* inputBooleanState = const_cast<home_assistant::EntityState*>(
      static_cast<const home_assistant::EntityState*>(
          studio::devices().specializedState(haInputBoolean)));
  if (inputBooleanState == nullptr) return 1;
  home_assistant_ui::hide();
  ui::showHome();
  inputBooleanState->on = true;
  std::strcpy(inputBooleanState->stateText, "on");
  home_assistant_ui::show(haInputBoolean);
  if (!capture("28g_ha_input_boolean_on")) {
    return 1;
  }
  home_assistant_ui::hide();
  home_assistant_ui::show(haButton);
  if (!capture("29_ha_button")) {
    return 1;
  }
  home_assistant_ui::hide();
  ui::showPortal();
  if (std::strcmp(portal::qrPayload(),
                  "WIFI:T:WPA;S:Bleep-Setup-SIM;P:12345678;;") != 0) {
    std::fprintf(stderr, "Portal setup QR does not contain Wi-Fi credentials\n");
    return 1;
  }
  if (!capture("30_portal")) {
    return 1;
  }
  portal::stop();
  ui::showHome();
  ui::showPortal();
  portal::simSetWifiFeedback(portal::Status::Testing, "Joining studio Wi-Fi");
  if (!capture("30a_portal_connecting")) {
    return 1;
  }
  portal::stop();
  ui::showHome();
  ui::showPortal();
  portal::simSetWifiFeedback(portal::Status::Ready, "Wi-Fi failed - retry");
  pump(600);
  lv_obj_invalidate(lv_scr_act());
  pump(20);
  if (!capture("30aa_portal_wifi_failed")) {
    return 1;
  }
  portal::stop();
  ui::showHome();
  ui::showPortal();
  portal::simSetLan(true);
  if (std::strcmp(portal::qrPayload(), "http://192.168.1.84") != 0) {
    std::fprintf(stderr, "LAN Portal QR does not contain its numeric URL\n");
    return 1;
  }
  if (!capture("30b_portal_lan")) {
    return 1;
  }
  ui::showHome();

  ui::showSettings();
  if (!capture("31_settings")) return 1;
  ui::simScrollSettingsToEnd();
  if (!capture("31_settings_scrolled")) return 1;
  ui::simShowWifiSettings();
  if (!capture("31a_settings_wifi")) return 1;
  portal::simSetSavedWifi("");
  ui::showSettings();
  ui::simShowWifiSettings();
  if (!capture("31b_settings_wifi_empty")) return 1;
  portal::simSetSavedWifi("Studio-WiFi");
  ui::simShowAbout();
  if (!capture("31c_settings_about")) return 1;
  ui::simScrollAboutToEnd();
  if (!capture("31c_settings_about_scrolled")) return 1;
  ui::simShowSystemInfo();
  if (!capture("31d_settings_system")) return 1;

  ui::simSetHapticEnabled(false);
  gHapticStateCount = 0;
  haptic_feedback::request(haptic_feedback::Pattern::Press);
  if (gHapticStateCount != 0) {
    std::fprintf(stderr, "Disabled haptics still drove the output\n");
    return 1;
  }
  ui::simSetHapticEnabled(true);

  studio::factory_reset::clearSimulatedResetCount();
  ui::showSettings();
  pump(20);
  ui::simShowFactoryReset();
  if (!capture("31e_settings_factory_reset")) return 1;
  ui::simSetFactoryResetHolding(true);
  pump(2999);
  if (studio::factory_reset::simulatedResetCount() != 0) {
    std::fprintf(stderr, "Factory reset triggered before the hold completed\n");
    return 1;
  }
  ui::simSetFactoryResetHolding(false);
  ui::simSetFactoryResetHolding(true);
  pump(3001);
  if (studio::factory_reset::simulatedResetCount() != 1) {
    std::fprintf(stderr, "Factory reset did not trigger exactly once\n");
    return 1;
  }
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
