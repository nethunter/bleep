#include "devices/aputure_light/ui.h"

#include <Arduino.h>
#include <lvgl.h>

#include <cstdio>
#include <cstring>

#include "core/device_manager.h"
#include "devices/aputure_light/runtime.h"
#include "devices/aputure_light/state.h"
#include "fonts/ui_fonts.h"
#include "haptic_feedback.h"
#include "ui/ble_pairing_screen.h"
#include "ui/light_control.h"
#include "ui/round_page.h"
#include "../../ui.h"

namespace aputure_light_ui {
namespace {
constexpr uint32_t kBg=0x05070a, kPanel=0x12161d, kAccent=0x35c7f2,
                   kText=0xf3f4f6, kMuted=0x8a94a6, kDanger=0xf26d6d;
studio::InstanceId instanceId=studio::kInvalidInstanceId;
lv_obj_t *screen=nullptr,*title=nullptr,*status=nullptr,*cctBody=nullptr,*rgbBody=nullptr,
         *kelvinSlider=nullptr,*tintSlider=nullptr,*cctBrightness=nullptr,
         *wheel=nullptr,*rgbSaturation=nullptr,*rgbBrightness=nullptr,*modeCct=nullptr,*modeRgb=nullptr,*power=nullptr,
         *identifyAce=nullptr,*identifyPano60=nullptr,*identifyPano120=nullptr,
         *identifyMcPro=nullptr;
bool visible=false, rgbMode=false, dirty=false, lookAppliedForView=false;
bool draftInitialized=false, syncingControls=false;
uint16_t draftKelvin=5600;
int16_t draftTint=0;
uint8_t draftCctBrightness=50;
uint32_t draftRgb=0xffffff;
uint8_t draftRgbSaturation=100;
uint8_t draftRgbBrightness=50;
uint32_t applyAt=0,lastRefresh=0;
studio_ui::BlePairingScreen pairingScreen;

lv_obj_t* button(lv_obj_t* parent,const char* text,lv_event_cb_t cb,uint32_t color=kPanel){
  lv_obj_t* b=lv_btn_create(parent); lv_obj_set_style_bg_color(b,lv_color_hex(color),0);
  lv_obj_set_style_radius(b,8,0); lv_obj_set_style_shadow_width(b,0,0);
  lv_obj_t* l=lv_label_create(b); lv_label_set_text(l,text); lv_obj_set_style_text_font(l,UI_FONT_14,0); lv_obj_center(l);
  if(cb) lv_obj_add_event_cb(b,cb,LV_EVENT_CLICKED,nullptr); return b;
}
bool queue(studio::CommandType type,int v0=0,int v1=0,int v2=0){
  studio::DeviceCommand c; c.instanceId=instanceId;c.type=type;c.value0=v0;c.value1=v1;c.value2=v2;return studio::devices().enqueue(c);
}
void captureDraft(){
  if(syncingControls)return;
  if(rgbMode){lv_color_hsv_t hsv=lv_colorwheel_get_hsv(wheel);draftRgbSaturation=lv_slider_get_value(rgbSaturation);draftRgb=lv_color_to32(lv_color_hsv_to_rgb(hsv.h,draftRgbSaturation,100))&0xffffff;draftRgbBrightness=lv_slider_get_value(rgbBrightness);}
  else {draftKelvin=lv_slider_get_value(kelvinSlider);draftTint=lv_slider_get_value(tintSlider);draftCctBrightness=lv_slider_get_value(cctBrightness);}
}
void restoreDraft(){
  syncingControls=true;
  if(rgbMode){lv_color_hsv_t hsv=lv_color_rgb_to_hsv(static_cast<uint8_t>(draftRgb>>16),static_cast<uint8_t>(draftRgb>>8),static_cast<uint8_t>(draftRgb));lv_colorwheel_set_hsv(wheel,lv_color_hsv_t{hsv.h,100,100});lv_slider_set_value(rgbSaturation,draftRgbSaturation,LV_ANIM_OFF);lv_slider_set_value(rgbBrightness,draftRgbBrightness,LV_ANIM_OFF);}
  else {lv_slider_set_value(kelvinSlider,draftKelvin,LV_ANIM_OFF);lv_slider_set_value(tintSlider,draftTint,LV_ANIM_OFF);lv_slider_set_value(cctBrightness,draftCctBrightness,LV_ANIM_OFF);}
  syncingControls=false;
}
void renderMode(){lv_obj_set_style_bg_color(modeCct,lv_color_hex(rgbMode?kPanel:kAccent),0);lv_obj_set_style_bg_color(modeRgb,lv_color_hex(rgbMode?kAccent:kPanel),0);if(rgbMode){lv_obj_add_flag(cctBody,LV_OBJ_FLAG_HIDDEN);lv_obj_clear_flag(rgbBody,LV_OBJ_FLAG_HIDDEN);}else{lv_obj_add_flag(rgbBody,LV_OBJ_FLAG_HIDDEN);lv_obj_clear_flag(cctBody,LV_OBJ_FLAG_HIDDEN);}lv_obj_move_foreground(power);lv_obj_invalidate(screen);}
void markDirty(lv_event_t*){if(syncingControls)return;captureDraft();dirty=true;applyAt=millis()+350;}
void setMode(bool rgb){if(rgb==rgbMode)return;captureDraft();rgbMode=rgb;renderMode();restoreDraft();dirty=true;applyAt=millis()+350;}
void onCct(lv_event_t*){setMode(false);} void onRgb(lv_event_t*){setMode(true);}
void onPower(lv_event_t*){const auto* s=static_cast<const aputure_light::AputureLightState*>(studio::devices().specializedState(instanceId));queue(s&&s->on?studio::CommandType::TurnOff:studio::CommandType::TurnOn);}
bool needsVendorIdentity() {
  const auto* state = static_cast<const aputure_light::AputureLightState*>(
      studio::devices().specializedState(instanceId));
  const aputure_light::AputureLightRuntime* runtime =
      aputure_light::runtimeIfActive();
  return state != nullptr &&
         state->phase == aputure_light::AputureLightState::Phase::Failed &&
         runtime != nullptr && runtime->canIdentifyVendorModel(instanceId);
}
void onRetry(lv_event_t*) {
  if (studio::devices().pendingAddCommitFailed(instanceId))
    studio::devices().retryPendingAdd(instanceId);
  else
    queue(studio::CommandType::Connect);
}
void identify(uint16_t companyId, uint16_t modelId, const char* productName) {
  aputure_light::AputureLightRuntime* runtime =
      aputure_light::runtimeIfActive();
  if (runtime != nullptr)
    runtime->identifyVendorModel(instanceId, companyId, modelId, productName);
}
void onIdentifyAce(lv_event_t*) {
  identify(0x0211, 0x0000, "amaran Ace 25c");
}
void onIdentifyPano60(lv_event_t*) {
  identify(0x0211, 0x0000, "amaran Pano 60c");
}
void onIdentifyPano120(lv_event_t*) {
  identify(0x0211, 0x0000, "amaran Pano 120c");
}
void onIdentifyMcPro(lv_event_t*) {
  identify(0x03f6, 0x1000, "Aputure MC Pro");
}
void showIdentificationButtons(bool show) {
  if (show && identifyAce == nullptr) {
    identifyAce = button(pairingScreen.screen(), "Ace 25c", onIdentifyAce);
    lv_obj_set_size(identifyAce, 82, 30);
    lv_obj_align(identifyAce, LV_ALIGN_BOTTOM_MID, -44, -50);
    identifyPano60 =
        button(pairingScreen.screen(), "Pano 60c", onIdentifyPano60);
    lv_obj_set_size(identifyPano60, 82, 30);
    lv_obj_align(identifyPano60, LV_ALIGN_BOTTOM_MID, 44, -50);
    identifyPano120 =
        button(pairingScreen.screen(), "Pano 120c", onIdentifyPano120);
    lv_obj_set_size(identifyPano120, 82, 30);
    lv_obj_align(identifyPano120, LV_ALIGN_BOTTOM_MID, -44, -14);
    identifyMcPro = button(pairingScreen.screen(), "MC Pro", onIdentifyMcPro);
    lv_obj_set_size(identifyMcPro, 82, 30);
    lv_obj_align(identifyMcPro, LV_ALIGN_BOTTOM_MID, 44, -14);
  }
  if (identifyAce == nullptr || identifyPano60 == nullptr ||
      identifyPano120 == nullptr || identifyMcPro == nullptr) return;
  if (show) {
    lv_obj_clear_flag(identifyAce, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(identifyPano60, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(identifyPano120, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(identifyMcPro, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(identifyAce, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(identifyPano60, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(identifyPano120, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(identifyMcPro, LV_OBJ_FLAG_HIDDEN);
  }
}
void onCandidate(uint32_t token) {
  studio::devices().selectOnboardingCandidate(instanceId, token);
}
void updateCandidates(bool show) {
  studio::OnboardingCandidate candidates[4];
  size_t count = 0;
  if (show) {
    const size_t available = studio::devices().onboardingCandidateCount(instanceId);
    while (count < available && count < 4 &&
           studio::devices().onboardingCandidate(instanceId, count,
                                                 candidates[count])) {
      ++count;
    }
  }
  pairingScreen.setCandidates(candidates, count, onCandidate);
}
void onBack(lv_event_t*){haptic_feedback::request(haptic_feedback::Pattern::Back);if(studio::devices().isPendingAdd(instanceId))studio::devices().cancelPendingAdd(instanceId);hide();ui::showDeviceParent();}
void onSharedBack(){onBack(nullptr);}
lv_obj_t* labeledSlider(lv_obj_t* parent,const char* text,int min,int max,lv_obj_t*& slider){
  lv_obj_t* row=lv_obj_create(parent);lv_obj_set_size(row,166,28);lv_obj_set_style_bg_opa(row,LV_OPA_TRANSP,0);lv_obj_set_style_border_width(row,0,0);lv_obj_set_style_pad_all(row,0,0);
  lv_obj_t* label=lv_label_create(row);lv_label_set_text(label,text);lv_obj_set_style_text_font(label,UI_FONT_14,0);lv_obj_align(label,LV_ALIGN_LEFT_MID,0,0);
  slider=lv_slider_create(row);lv_obj_set_size(slider,104,10);lv_obj_align(slider,LV_ALIGN_RIGHT_MID,0,0);lv_slider_set_range(slider,min,max);lv_obj_add_event_cb(slider,markDirty,LV_EVENT_VALUE_CHANGED,nullptr);return row;
}
void ensure(){if(screen)return;screen=lv_obj_create(nullptr);lv_obj_set_style_bg_color(screen,lv_color_hex(kBg),0);lv_obj_set_style_text_color(screen,lv_color_hex(kText),0);lv_obj_clear_flag(screen,LV_OBJ_FLAG_SCROLLABLE);
  studio_ui::RoundPageHeaderOptions headerOptions;headerOptions.onBack=onBack;headerOptions.panelColor=kPanel;headerOptions.textColor=kText;title=studio_ui::createRoundPageHeader(screen,headerOptions).title;
  status=lv_label_create(screen);lv_obj_set_width(status,170);lv_obj_set_style_text_align(status,LV_TEXT_ALIGN_CENTER,0);lv_obj_set_style_text_font(status,UI_FONT_14,0);lv_obj_set_style_text_color(status,lv_color_hex(kMuted),0);lv_obj_align(status,LV_ALIGN_TOP_MID,0,51);
  modeCct=button(screen,"CCT",onCct,kAccent);lv_obj_set_size(modeCct,58,27);lv_obj_align(modeCct,LV_ALIGN_TOP_MID,-33,72);
  modeRgb=button(screen,"RGB",onRgb);lv_obj_set_size(modeRgb,58,27);lv_obj_align(modeRgb,LV_ALIGN_TOP_MID,33,72);
  cctBody=lv_obj_create(screen);lv_obj_set_size(cctBody,174,88);lv_obj_align(cctBody,LV_ALIGN_TOP_MID,0,103);lv_obj_set_style_bg_opa(cctBody,LV_OPA_TRANSP,0);lv_obj_set_style_border_width(cctBody,0,0);lv_obj_set_style_pad_all(cctBody,2,0);lv_obj_set_style_pad_row(cctBody,0,0);lv_obj_set_flex_flow(cctBody,LV_FLEX_FLOW_COLUMN);
  labeledSlider(cctBody,"K",2300,10000,kelvinSlider);labeledSlider(cctBody,"Tint",-1000,1000,tintSlider);labeledSlider(cctBody,"Bri",0,100,cctBrightness);
  rgbBody=lv_obj_create(screen);lv_obj_set_size(rgbBody,174,88);lv_obj_align(rgbBody,LV_ALIGN_TOP_MID,0,103);lv_obj_set_style_bg_opa(rgbBody,LV_OPA_TRANSP,0);lv_obj_set_style_border_width(rgbBody,0,0);lv_obj_set_style_pad_all(rgbBody,0,0);lv_obj_add_flag(rgbBody,LV_OBJ_FLAG_HIDDEN);
  wheel=lv_colorwheel_create(rgbBody,true);lv_obj_set_size(wheel,72,72);lv_obj_align(wheel,LV_ALIGN_LEFT_MID,5,0);lv_obj_add_event_cb(wheel,markDirty,LV_EVENT_VALUE_CHANGED,nullptr);
  lv_obj_t* satLabel=lv_label_create(rgbBody);lv_label_set_text(satLabel,"Sat");lv_obj_set_style_text_font(satLabel,UI_FONT_14,0);lv_obj_align(satLabel,LV_ALIGN_TOP_RIGHT,-55,6);
  rgbSaturation=lv_slider_create(rgbBody);lv_obj_set_size(rgbSaturation,72,8);lv_obj_align(rgbSaturation,LV_ALIGN_TOP_RIGHT,-3,25);lv_slider_set_range(rgbSaturation,0,100);lv_obj_add_event_cb(rgbSaturation,markDirty,LV_EVENT_VALUE_CHANGED,nullptr);
  lv_obj_t* briLabel=lv_label_create(rgbBody);lv_label_set_text(briLabel,"Bri");lv_obj_set_style_text_font(briLabel,UI_FONT_14,0);lv_obj_align(briLabel,LV_ALIGN_TOP_RIGHT,-55,43);
  rgbBrightness=lv_slider_create(rgbBody);lv_obj_set_size(rgbBrightness,72,8);lv_obj_align(rgbBrightness,LV_ALIGN_TOP_RIGHT,-3,62);lv_slider_set_range(rgbBrightness,0,100);lv_obj_add_event_cb(rgbBrightness,markDirty,LV_EVENT_VALUE_CHANGED,nullptr);
  power=button(screen,"POWER",onPower,kAccent);lv_obj_set_size(power,94,28);lv_obj_align(power,LV_ALIGN_BOTTOM_MID,0,-16);
  lv_obj_move_background(cctBody);lv_obj_move_background(rgbBody);
}
const char* phase(const aputure_light::AputureLightState* s){if(!s)return "Unavailable";switch(s->phase){case aputure_light::AputureLightState::Phase::Unprovisioned:return "Not provisioned";case aputure_light::AputureLightState::Phase::Scanning:return "Scanning for light";case aputure_light::AputureLightState::Phase::Provisioning:return "Provisioning";case aputure_light::AputureLightState::Phase::PendingConfig:return "Configuring mesh";case aputure_light::AputureLightState::Phase::ConnectingProxy:return "Connecting proxy";case aputure_light::AputureLightState::Phase::Ready:return s->powerConfirmed&&s->nodeReachable?"Ready / confirmed":(s->optimistic?"Ready / optimistic":"Ready / state unknown");case aputure_light::AputureLightState::Phase::Failed:return s->error[0]?s->error:"Failed";}return "Unknown";}
void showForState(const aputure_light::AputureLightState* s){
  if(studio::devices().pendingAddCommitFailed(instanceId)){pairingScreen.create(onBack,onRetry);showIdentificationButtons(false);const auto* r=studio::devices().find(instanceId);pairingScreen.setTitle(r?r->displayName:"Aputure Light");pairingScreen.setStatus("Couldn't save","Retry to add this device",false,true,"Retry");if(lv_scr_act()!=pairingScreen.screen())lv_scr_load(pairingScreen.screen());return;}
  if(s&&s->phase==aputure_light::AputureLightState::Phase::Ready){if(!light_control_ui::active())light_control_ui::show(instanceId,onSharedBack,true);return;}
  light_control_ui::hide();
  pairingScreen.create(onBack,onRetry);const auto* r=studio::devices().find(instanceId);pairingScreen.setTitle(r?r->displayName:"Aputure Light");
  const bool failed=s&&s->phase==aputure_light::AputureLightState::Phase::Failed;
  const bool scanning=!s||s->phase==aputure_light::AputureLightState::Phase::Unprovisioned||s->phase==aputure_light::AputureLightState::Phase::Scanning;
  const bool unknownVendor = needsVendorIdentity();
  showIdentificationButtons(unknownVendor);
  const char* detail = scanning ? "Factory-reset light nearby"
                       : unknownVendor ? ""
                       : failed ? "Check light and try again"
                                : "Keep the light powered on";
  pairingScreen.setStatus(unknownVendor ? "Identify fixture" : phase(s),
                          detail, !failed,
                          unknownVendor ? false : (scanning || failed),
                          "Retry");
  updateCandidates(scanning && !unknownVendor);
  if(lv_scr_act()!=pairingScreen.screen())lv_scr_load(pairingScreen.screen());
}
void refresh(){const auto* r=studio::devices().find(instanceId);const auto* s=static_cast<const aputure_light::AputureLightState*>(studio::devices().specializedState(instanceId));lv_label_set_text(title,r?r->displayName:"Aputure Light");lv_label_set_text(status,phase(s));if(!s)return;
  if(!draftInitialized){draftKelvin=s->kelvin;draftTint=s->tintPermille;draftCctBrightness=s->cctBrightness;draftRgb=s->rgb;lv_color_hsv_t hsv=lv_color_rgb_to_hsv(static_cast<uint8_t>(draftRgb>>16),static_cast<uint8_t>(draftRgb>>8),static_cast<uint8_t>(draftRgb));draftRgbSaturation=s->mode==aputure_light::AputureLightState::Mode::Rgb?hsv.s:100;draftRgbBrightness=s->rgbBrightness;rgbMode=s->mode==aputure_light::AputureLightState::Mode::Rgb;draftInitialized=true;renderMode();restoreDraft();}
  if(s->phase==aputure_light::AputureLightState::Phase::Ready&&!lookAppliedForView){lookAppliedForView=true;dirty=true;applyAt=millis()+350;}
  lv_obj_set_style_bg_color(power,lv_color_hex(s->on?kDanger:kAccent),0);
}
void apply(){studio::DeviceRuntimeState rt=studio::devices().runtimeState(instanceId);if(!rt.protocolReady||rt.commandPending)return;captureDraft();const bool queued=rgbMode?queue(studio::CommandType::SetLightRgb,draftRgb,draftRgbBrightness):queue(studio::CommandType::SetLightCct,draftKelvin,draftCctBrightness,draftTint);if(queued)dirty=false;}
}
void show(studio::InstanceId id){ensure();instanceId=id;visible=studio::devices().acquire(id,studio::ConnectionOwner::Foreground);dirty=false;draftInitialized=false;lookAppliedForView=false;refresh();showForState(static_cast<const aputure_light::AputureLightState*>(studio::devices().specializedState(id)));}
void hide(){light_control_ui::hide();if(visible)studio::devices().release(instanceId,studio::ConnectionOwner::Foreground);visible=false;instanceId=studio::kInvalidInstanceId;}
void release(){if(visible)return;light_control_ui::release();if(screen){lv_obj_del(screen);screen=title=status=cctBody=rgbBody=kelvinSlider=tintSlider=cctBrightness=wheel=rgbSaturation=rgbBrightness=modeCct=modeRgb=power=nullptr;}pairingScreen.destroy();identifyAce=identifyPano60=identifyPano120=identifyMcPro=nullptr;}
bool active(){return visible;}
void tick(){if(!visible)return;if(light_control_ui::active()){light_control_ui::tick();return;}uint32_t now=millis();if(dirty&&static_cast<int32_t>(now-applyAt)>=0)apply();if(now-lastRefresh>=250){lastRefresh=now;const auto* s=static_cast<const aputure_light::AputureLightState*>(studio::devices().specializedState(instanceId));refresh();showForState(s);}}
void handleShortPress(){if(light_control_ui::active())light_control_ui::handleShortPress();else onPower(nullptr);}void handleLongPress(){if(light_control_ui::active())light_control_ui::handleLongPress();else onBack(nullptr);}
#ifdef UI_SIMULATOR
void simSetCctLook(int kelvin,int tintPermille,int brightness){light_control_ui::simSetCctLook(kelvin,tintPermille,brightness);}
void simSetRgbLook(uint32_t rgb,int brightness){light_control_ui::simSetRgbLook(rgb,brightness);}
void simShowCct(){light_control_ui::simShowCct();}
void simShowRgb(){light_control_ui::simShowRgb();}
int simRgbSaturation(){return light_control_ui::simRgbSaturation();}
size_t simCandidateRowCount(){return pairingScreen.simCandidateRowCount();}
lv_obj_t* simCandidateRow(size_t index){return pairingScreen.simCandidateRow(index);}
void simScrollCandidates(int16_t delta){pairingScreen.simScrollCandidates(delta);}
int32_t simCandidateScrollY(){return pairingScreen.simCandidateScrollY();}
void simClickCandidate(size_t index){pairingScreen.simClickCandidate(index);}
#endif
}  // namespace aputure_light_ui
