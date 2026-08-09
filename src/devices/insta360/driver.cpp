#include "devices/insta360/driver.h"
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <cstring>
#include <new>
#include <vector>
#include "core/ble/ble_runtime.h"
#include "core/ble/peripheral_dispatcher.h"
#include "devices/camera_peripheral/gatt.h"
#include "devices/insta360/protocol.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
namespace studio { namespace { constexpr uint16_t kNoConnection=0xffff; }
class Insta360Driver::Runtime : public ble::BleCentralDelegate, public ble::PeripheralListener {
 public:
  enum class EventType:uint8_t { Connected, Disconnected };
  struct Event { EventType type=EventType::Disconnected; uint16_t handle=kNoConnection; uint8_t addressType=0; char address[kBleAddressCapacity]=""; char name[kBleNameCapacity]=""; };
  bool begin() {
    queue_=xQueueCreate(12,sizeof(Event)); if(!queue_) return false;
    ble::ConnectPolicy policy; policy.diagnosticTag="insta360_remote";
    radio_=ble::bleCentral().acquire(*this,policy); if(radio_==ble::kInvalidLinkHandle) return fail();
    server_=NimBLEDevice::createServer(); if(!server_ || !ble::registerPeripheralListener(server_,this)) return fail();
    camera_peripheral::GattServices services;
    if(!camera_peripheral::ensureGattServices(server_,services)) return fail();
    write_=services.instaWrite; notify_=services.instaNotify;
    initialized_=notify_!=nullptr; return initialized_;
  }
  bool startCameraScan(){return initialized_&&ble::bleCentral().requestScan(radio_,true);}
  bool advertise() {
    if(!initialized_ || shutdown_) return false;
    const ble::PeripheralAdvertisement advertisement{
        "insta360", "Insta360 GPS Remote BEP", insta360::kServiceUuid, 0};
    return ble::requestPeripheralAdvertising(this,advertisement,millis());
  }
  void stopAdvertising(){ble::releasePeripheralAdvertising(this,millis());}
  bool notifyShutter(uint16_t handle){return notify_&&notify_->notify(insta360::kShutterCommand,sizeof(insta360::kShutterCommand),handle);}
  bool pop(Event& e){return queue_&&xQueueReceive(queue_,&e,0)==pdTRUE;}
  void shutdown(){if(shutdown_)return;shutdown_=true;stopAdvertising();if(server_)for(uint16_t h:server_->getPeerDevices())server_->disconnect(h);}
  bool finishShutdown(){if(!shutdown_||(server_&&server_->getConnectedCount()))return false;ble::unregisterPeripheralListener(this);server_=nullptr;write_=nullptr;notify_=nullptr;ble::bleCentral().release(radio_);radio_=ble::kInvalidLinkHandle;if(queue_)vQueueDelete(queue_);queue_=nullptr;initialized_=false;shutdown_=false;return true;}
  void onBleAdvertisement(ble::LinkHandle link,const ble::Advertisement& adv) override {
    if(link!=radio_)return; char name[kBleNameCapacity]=""; if(!ble::advertisementName(adv,name,sizeof(name))||!insta360::matchesCameraName(name))return;
    Candidate* c=nullptr;for(Candidate& x:candidates_)if(std::strcmp(x.address,adv.address.value)==0||x.address[0]=='\0'){c=&x;break;}if(!c)return;
    std::strncpy(c->address,adv.address.value,sizeof(c->address)-1);c->type=adv.address.type;std::strncpy(c->name,name,sizeof(c->name)-1);
  }
  void onBleEvent(ble::LinkHandle,const ble::Event&) override{}
  bool acceptsPeripheralPeer(const char* address) const override {for(const Candidate& c:candidates_)if(c.address[0]&&std::strcmp(c.address,address)==0)return true;return false;}
  void onPeripheralConnected(NimBLEConnInfo& info) override {push(EventType::Connected,info);}
  void onPeripheralDisconnected(NimBLEConnInfo& info) override {push(EventType::Disconnected,info);}
  void onPeripheralAuthentication(NimBLEConnInfo&) override{}
 private:
  struct Candidate{char address[kBleAddressCapacity]="";uint8_t type=0;char name[kBleNameCapacity]="";};
  bool fail(){shutdown_=true;finishShutdown();return false;}
  void push(EventType type,NimBLEConnInfo& info){if(!queue_)return;Event e;e.type=type;e.handle=info.getConnHandle();NimBLEAddress a=type==EventType::Connected?info.getAddress():info.getIdAddress();e.addressType=a.getType();std::strncpy(e.address,a.toString().c_str(),sizeof(e.address)-1);for(const Candidate& c:candidates_)if(std::strcmp(c.address,e.address)==0){std::strncpy(e.name,c.name,sizeof(e.name)-1);break;}xQueueSend(queue_,&e,0);}
  QueueHandle_t queue_=nullptr;NimBLEServer* server_=nullptr;NimBLECharacteristic* write_=nullptr;NimBLECharacteristic* notify_=nullptr;ble::LinkHandle radio_=ble::kInvalidLinkHandle;Candidate candidates_[8]={};bool initialized_=false;bool shutdown_=false;
};
Insta360Driver::Session* Insta360Driver::sessionFor(InstanceId id){for(Session*s:sessions_)if(s&&s->instanceId==id)return s;return nullptr;}
const Insta360Driver::Session* Insta360Driver::sessionFor(InstanceId id)const{for(const Session*s:sessions_)if(s&&s->instanceId==id)return s;return nullptr;}
Insta360Driver::Session* Insta360Driver::sessionForAddress(const char*a){for(Session*s:sessions_)if(s&&s->paired&&std::strcmp(s->address,a)==0)return s;return nullptr;}
Insta360Driver::Session* Insta360Driver::firstAwaiting(){for(Session*s:sessions_)if(s&&!s->paired)return s;return nullptr;}
bool Insta360Driver::activate(const DeviceRecord&r){if(sessionFor(r.instanceId))return true;if(!runtime_){runtime_=new(std::nothrow)Runtime;if(!runtime_||!runtime_->begin()){delete runtime_;runtime_=nullptr;return false;}}for(Session*&s:sessions_){if(s)continue;s=new(std::nothrow)Session;if(!s)return false;s->instanceId=r.instanceId;s->paired=r.paired&&r.bleAddress[0];s->addressType=r.bleAddressType;std::strncpy(s->address,r.bleAddress,sizeof(s->address)-1);std::strncpy(s->state.deviceName,r.bleName[0]?r.bleName:r.displayName,sizeof(s->state.deviceName)-1);s->state.goUltraExperimental=insta360::isGoUltra(s->state.deviceName);s->state.link=insta360::State::Link::Scanning;updateAdvertising();runtime_->startCameraScan();return true;}return false;}
void Insta360Driver::deactivate(InstanceId id){Session*s=sessionFor(id);if(!s)return;for(Session*&x:sessions_)if(x==s){delete x;x=nullptr;break;}bool any=false;for(Session*x:sessions_)any|=x!=nullptr;if(!any&&runtime_)runtime_->shutdown();}
void Insta360Driver::loop(){if(!runtime_)return;Runtime::Event e;while(runtime_->pop(e)){Session*s=sessionForAddress(e.address);if(e.type==Runtime::EventType::Connected){if(!s)s=firstAwaiting();if(!s)continue;s->connHandle=e.handle;s->paired=true;s->pairingChanged=true;s->addressType=e.addressType;std::strncpy(s->address,e.address,sizeof(s->address)-1);if(e.name[0])std::strncpy(s->state.deviceName,e.name,sizeof(s->state.deviceName)-1);s->state.goUltraExperimental=insta360::isGoUltra(s->state.deviceName);s->state.link=insta360::State::Link::Connected;}else{for(Session*x:sessions_)if(x&&x->connHandle==e.handle){x->connHandle=kNoConnection;x->state.link=insta360::State::Link::Scanning;x->state.triggerPending=false;}}}
for(Session*s:sessions_)if(s&&s->triggerRequested){s->triggerRequested=false;bool ok=runtime_->notifyShutter(s->connHandle);s->state.triggerPending=false;s->state.lastTriggerFailed=!ok;if(ok)++s->state.triggerCount;}updateAdvertising();bool any=false;for(Session*s:sessions_)any|=s!=nullptr;if(!any&&runtime_->finishShutdown()){delete runtime_;runtime_=nullptr;}}
void Insta360Driver::updateAdvertising(){if(!runtime_)return;bool needed=false;for(Session*s:sessions_)needed|=s&&s->connHandle==kNoConnection;if(needed)runtime_->advertise();else runtime_->stopAdvertising();}
CommandStatus Insta360Driver::dispatch(const DeviceCommand&c){Session*s=sessionFor(c.instanceId);if(!s)return CommandStatus::Unavailable;if(c.type==CommandType::RecordTrigger){if(s->connHandle==kNoConnection||s->state.triggerPending)return CommandStatus::Unavailable;s->state.triggerPending=true;s->state.lastTriggerFailed=false;s->triggerRequested=true;return CommandStatus::Succeeded;}if(c.type==CommandType::ForgetPairing){s->paired=false;s->pairingChanged=true;s->address[0]='\0';return CommandStatus::Succeeded;}return CommandStatus::Unsupported;}
DeviceRuntimeState Insta360Driver::runtimeState(InstanceId id)const{DeviceRuntimeState r;const Session*s=sessionFor(id);if(!s)return r;r.link=s->state.link==insta360::State::Link::Connected?LinkState::Connected:LinkState::Scanning;r.protocolReady=s->connHandle!=kNoConnection;r.commandPending=s->state.triggerPending;r.commandFailed=s->state.lastTriggerFailed;r.quality=StateQuality::Optimistic;return r;}
const void* Insta360Driver::specializedState(InstanceId id)const{const Session*s=sessionFor(id);return s?&s->state:nullptr;}
void Insta360Driver::forgetPairing(const DeviceRecord&){} void Insta360Driver::cancelOnboarding(const DeviceRecord&){}
bool Insta360Driver::consumePairingUpdate(InstanceId id,DeviceRecord&r){Session*s=sessionFor(id);if(!s||!s->pairingChanged)return false;s->pairingChanged=false;r.paired=s->paired;r.bleAddressType=s->addressType;std::strncpy(r.bleAddress,s->address,sizeof(r.bleAddress)-1);std::strncpy(r.bleName,s->state.deviceName,sizeof(r.bleName)-1);return true;}
}  // namespace studio
