#include "devices/insta360/ui.h"
#include <Arduino.h>
#include "core/device_manager.h"
#include "devices/insta360/state.h"
#include "ui/recorder_shell.h"
#include "../../ui.h"
namespace insta360_ui { namespace {
constexpr auto kOwner=recorder_shell::Owner::Insta360;studio::InstanceId id=studio::kInvalidInstanceId;bool visible=false;uint32_t refreshed=0;
void action(){if(studio::devices().pendingAddCommitFailed(id)){studio::devices().retryPendingAdd(id);return;}auto r=studio::devices().runtimeState(id);if(r.link!=studio::LinkState::Connected||r.commandPending)return;studio::DeviceCommand c;c.instanceId=id;c.type=studio::CommandType::RecordTrigger;studio::devices().enqueue(c);}
void back(){hide();ui::showDeviceParent();}
void shell(){if(recorder_shell::ownedBy(kOwner))return;if(recorder_shell::screen()&&lv_scr_act()==recorder_shell::screen())ui::parkForScreenRebuild();recorder_shell::destroyIdle();recorder_shell::Options o;recorder_shell::Callbacks c;c.onBack=back;c.onAction=action;recorder_shell::acquire(kOwner,o,c);}
void refresh(){if(!recorder_shell::ownedBy(kOwner))return;recorder_shell::View v;const auto*rec=studio::devices().find(id);v.title=rec?rec->displayName:"Insta360";auto r=studio::devices().runtimeState(id);const auto*s=static_cast<const insta360::State*>(studio::devices().specializedState(id));if(studio::devices().pendingAddCommitFailed(id)){v.status="COULDN'T SAVE";v.detail="RETRY TO ADD DEVICE";v.actionLabel="RETRY";v.actionEnabled=true;v.actionColor=0x2E7D5B;}else if(r.link!=studio::LinkState::Connected||!s){v.status="PAIRING...";v.detail="CONNECT GPS REMOTE";v.actionLabel="WAITING";}else if(s->lastTriggerFailed){v.status="SEND FAILED";v.detail="CAMERA STATE UNKNOWN";v.actionLabel="SHUTTER";v.actionEnabled=true;v.actionColor=0x2E7D5B;}else{v.status="READY";v.detail=s->goUltraExperimental?"GO ULTRA EXPERIMENTAL":"GPS REMOTE CONNECTED";v.actionLabel=s->triggerPending?"WAIT":"SHUTTER";v.actionEnabled=!s->triggerPending;v.actionColor=0x2E7D5B;}recorder_shell::apply(v);}
}
void show(studio::InstanceId value){shell();id=value;visible=studio::devices().acquire(id,studio::ConnectionOwner::Foreground);if(!visible){id=studio::kInvalidInstanceId;return;}refreshed=0;refresh();lv_scr_load(recorder_shell::screen());ui::releaseInactiveScreens();}
void hide(){if(visible)studio::devices().release(id,studio::ConnectionOwner::Foreground);visible=false;id=studio::kInvalidInstanceId;}
void release(){if(!visible)recorder_shell::release(kOwner);}bool active(){return visible;}void tick(){uint32_t now=millis();if(now-refreshed>=200){refreshed=now;refresh();}}void handleShortPress(){action();}void handleLongPress(){back();}
}
