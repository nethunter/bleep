#include "core/ble/fake_ble_backend.h"

namespace studio::ble {

namespace {

size_t slotIndex(LinkHandle link) {
  return link == kInvalidLinkHandle ? CONFIG_MAX_ACTIVE_LINKS : link - 1;
}

}  // namespace

bool FakeBleBackend::begin() {
  ++beginCalls_;
  initialized_ = beginResult_;
  return beginResult_;
}

void FakeBleBackend::shutdown() {
  ++shutdownCalls_;
  initialized_ = false;
  scanning_ = false;
}

bool FakeBleBackend::startScan() {
  ++scanStarts_;
  scanning_ = initialized_;
  return scanning_;
}

void FakeBleBackend::stopScan() {
  ++scanStops_;
  scanning_ = false;
}

bool FakeBleBackend::createLink(LinkHandle link, uint16_t connectTimeoutMs) {
  const size_t index = slotIndex(link);
  if (!initialized_ || index >= CONFIG_MAX_ACTIVE_LINKS ||
      slots_[index].created) {
    return false;
  }
  slots_[index] = {};
  slots_[index].created = true;
  slots_[index].timeoutMs = connectTimeoutMs;
  if (++generations_[index] == 0) {
    ++generations_[index];
  }
  return true;
}

void FakeBleBackend::destroyLink(LinkHandle link) {
  const size_t index = slotIndex(link);
  if (index < CONFIG_MAX_ACTIVE_LINKS) {
    slots_[index] = {};
  }
}

bool FakeBleBackend::connect(LinkHandle link, const Address& address,
                             SecurityPolicy security) {
  const size_t index = slotIndex(link);
  if (index >= CONFIG_MAX_ACTIVE_LINKS || !slots_[index].created) {
    return false;
  }
  ++slots_[index].connectCalls;
  slots_[index].lastAddress = address;
  slots_[index].security = security;
  return connectResult_;
}

void FakeBleBackend::disconnect(LinkHandle link) {
  const size_t index = slotIndex(link);
  if (index < CONFIG_MAX_ACTIVE_LINKS && slots_[index].created) {
    ++slots_[index].disconnectCalls;
  }
}

bool FakeBleBackend::secure(LinkHandle link, SecurityPolicy security) {
  const size_t index = slotIndex(link);
  if (index >= CONFIG_MAX_ACTIVE_LINKS || !slots_[index].created) {
    return false;
  }
  ++slots_[index].secureCalls;
  slots_[index].security = security;
  return secureResult_;
}

bool FakeBleBackend::updateConnectionParameters(
    LinkHandle link, const ConnectionParameters& parameters) {
  const size_t index = slotIndex(link);
  if (index >= CONFIG_MAX_ACTIVE_LINKS || !slots_[index].created) {
    return false;
  }
  ++slots_[index].parameterUpdateCalls;
  slots_[index].lastParameters = parameters;
  return parameterUpdateResult_;
}

bool FakeBleBackend::deleteBond(const Address&) {
  ++bondDeleteCalls_;
  return initialized_;
}

void* FakeBleBackend::nativeClient(LinkHandle link) {
  const size_t index = slotIndex(link);
  return index < CONFIG_MAX_ACTIVE_LINKS && slots_[index].created
             ? &slots_[index]
             : nullptr;
}

bool FakeBleBackend::popEvent(Event& event) {
  while (eventCount_ > 0) {
    const QueuedEvent queued = events_[eventRead_];
    eventRead_ = (eventRead_ + 1) % CONFIG_BLE_EVENT_QUEUE_SIZE;
    --eventCount_;
    const size_t index = slotIndex(queued.event.link);
    if (queued.generation != 0 &&
        (index >= CONFIG_MAX_ACTIVE_LINKS || !slots_[index].created ||
         generations_[index] != queued.generation)) {
      continue;
    }
    event = queued.event;
    return true;
  }
  return false;
}

bool FakeBleBackend::emit(const Event& event) {
  if (eventCount_ >= CONFIG_BLE_EVENT_QUEUE_SIZE) {
    ++droppedEvents_;
    return false;
  }
  QueuedEvent queued;
  queued.event = event;
  const size_t index = slotIndex(event.link);
  if (index < CONFIG_MAX_ACTIVE_LINKS && slots_[index].created) {
    queued.generation = generations_[index];
  }
  events_[eventWrite_] = queued;
  eventWrite_ = (eventWrite_ + 1) % CONFIG_BLE_EVENT_QUEUE_SIZE;
  ++eventCount_;
  return true;
}

bool FakeBleBackend::emitAdvertisement(
    const Advertisement& advertisement) {
  Event event;
  event.type = EventType::Advertisement;
  event.advertisement = advertisement;
  return emit(event);
}

uint32_t FakeBleBackend::connectCalls(LinkHandle link) const {
  const size_t index = slotIndex(link);
  return index < CONFIG_MAX_ACTIVE_LINKS ? slots_[index].connectCalls : 0;
}

uint32_t FakeBleBackend::disconnectCalls(LinkHandle link) const {
  const size_t index = slotIndex(link);
  return index < CONFIG_MAX_ACTIVE_LINKS ? slots_[index].disconnectCalls : 0;
}

uint32_t FakeBleBackend::secureCalls(LinkHandle link) const {
  const size_t index = slotIndex(link);
  return index < CONFIG_MAX_ACTIVE_LINKS ? slots_[index].secureCalls : 0;
}

uint32_t FakeBleBackend::parameterUpdateCalls(LinkHandle link) const {
  const size_t index = slotIndex(link);
  return index < CONFIG_MAX_ACTIVE_LINKS
             ? slots_[index].parameterUpdateCalls
             : 0;
}

const ConnectionParameters& FakeBleBackend::lastParameters(
    LinkHandle link) const {
  static const ConnectionParameters empty;
  const size_t index = slotIndex(link);
  return index < CONFIG_MAX_ACTIVE_LINKS ? slots_[index].lastParameters
                                         : empty;
}

const Address& FakeBleBackend::lastConnectAddress(LinkHandle link) const {
  static const Address empty;
  const size_t index = slotIndex(link);
  return index < CONFIG_MAX_ACTIVE_LINKS ? slots_[index].lastAddress : empty;
}

}  // namespace studio::ble
