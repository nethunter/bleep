#include "core/scene_service.h"

#include <cstdio>
#include <cstring>

namespace studio {
namespace {

constexpr char kDuplicateSuffix[] = " (2)";

void makeDuplicateName(const char* source,
                       char (&name)[kDeviceNameCapacity]) {
  constexpr size_t suffixLength = sizeof(kDuplicateSuffix) - 1;
  constexpr size_t maxBaseLength =
      kDeviceNameCapacity - 1 - suffixLength;
  std::snprintf(name, sizeof(name), "%.*s%s",
                static_cast<int>(maxBaseLength), source, kDuplicateSuffix);
}

}  // namespace

SceneService::SceneService(IConfigBackend& backend, DeviceManager& devices)
    : store_(backend), devices_(devices), runner_(devices_, registry_) {}

bool SceneService::begin() {
  const ConfigLoadStatus status = store_.load(registry_);
  if (status == ConfigLoadStatus::Corrupt) {
    registry_.clear(true);
    begun_ = true;
    return false;
  }
  if (status == ConfigLoadStatus::Missing) {
    registry_.clear(true);
    save();
  }
  begun_ = true;
  return true;
}

void SceneService::loop(uint32_t nowMs) {
  if (!begun_) {
    return;
  }
  runner_.tick(nowMs);
}

bool SceneService::save() { return store_.save(registry_); }

SceneRegistryStatus SceneService::add(const char* name, SceneId& outId) {
  SceneRegistry previous = registry_;
  if (!previous.healthy()) return SceneRegistryStatus::Full;
  const SceneRegistryStatus status = registry_.add(name, outId);
  if (status == SceneRegistryStatus::Ok && !save()) {
    registry_.swap(previous);
    outId = kInvalidSceneId;
    return SceneRegistryStatus::Invalid;
  }
  return status;
}

SceneRegistryStatus SceneService::duplicate(SceneId sourceId, SceneId& outId) {
  const SceneRecord* source = registry_.find(sourceId);
  if (source == nullptr) {
    outId = kInvalidSceneId;
    return SceneRegistryStatus::NotFound;
  }
  char name[kDeviceNameCapacity];
  makeDuplicateName(source->name, name);
  return duplicate(sourceId, name, outId);
}

SceneRegistryStatus SceneService::duplicate(SceneId sourceId, const char* name,
                                            SceneId& outId) {
  outId = kInvalidSceneId;
  const SceneRecord* source = registry_.find(sourceId);
  if (source == nullptr) return SceneRegistryStatus::NotFound;
  const SceneRecord sourceCopy = *source;
  SceneRegistry previous = registry_;
  if (!previous.healthy()) return SceneRegistryStatus::Full;
  SceneRegistryStatus status = registry_.add(name, outId);
  if (status != SceneRegistryStatus::Ok) return status;
  SceneRecord copy = sourceCopy;
  copy.sceneId = outId;
  std::strncpy(copy.name, name, sizeof(copy.name) - 1);
  copy.name[sizeof(copy.name) - 1] = '\0';
  if (copy.stopMode == SceneStopMode::Generated) {
    generateStopSteps(copy);
  }
  status = registry_.replace(copy);
  if (status != SceneRegistryStatus::Ok || !save()) {
    registry_.swap(previous);
    outId = kInvalidSceneId;
    return SceneRegistryStatus::Invalid;
  }
  return SceneRegistryStatus::Ok;
}

SceneRegistryStatus SceneService::remove(SceneId sceneId) {
  if (runner_.busy() && runner_.progress().sceneId == sceneId) {
    return SceneRegistryStatus::Invalid;
  }
  SceneRegistry previous = registry_;
  if (!previous.healthy()) return SceneRegistryStatus::Full;
  const SceneRegistryStatus status = registry_.remove(sceneId);
  if (status == SceneRegistryStatus::Ok && !save()) {
    registry_.swap(previous);
    return SceneRegistryStatus::Invalid;
  }
  return status;
}

SceneRegistryStatus SceneService::rename(SceneId sceneId, const char* name) {
  SceneRegistry previous = registry_;
  if (!previous.healthy()) return SceneRegistryStatus::Full;
  const SceneRegistryStatus status = registry_.rename(sceneId, name);
  if (status == SceneRegistryStatus::Ok && !save()) {
    registry_.swap(previous);
    return SceneRegistryStatus::Invalid;
  }
  return status;
}

SceneRegistryStatus SceneService::setEnabled(SceneId sceneId, bool enabled) {
  SceneRegistry previous = registry_;
  if (!previous.healthy()) return SceneRegistryStatus::Full;
  const SceneRegistryStatus status = registry_.setEnabled(sceneId, enabled);
  if (status == SceneRegistryStatus::Ok && !save()) {
    registry_.swap(previous);
    return SceneRegistryStatus::Invalid;
  }
  return status;
}

SceneRegistryStatus SceneService::replace(const SceneRecord& record) {
  SceneRecord normalized = record;
  if (normalized.stopMode == SceneStopMode::Generated) {
    generateStopSteps(normalized);
  }
  if (runner_.validate(normalized) != SceneValidationStatus::Ok) {
    return SceneRegistryStatus::Invalid;
  }
  SceneRegistry previous = registry_;
  if (!previous.healthy()) return SceneRegistryStatus::Full;
  const SceneRegistryStatus status = registry_.replace(normalized);
  if (status == SceneRegistryStatus::Ok && !save()) {
    registry_.swap(previous);
    return SceneRegistryStatus::Invalid;
  }
  if (status == SceneRegistryStatus::Ok) {
    runner_.refreshPrepared(normalized.sceneId);
  }
  return status;
}

SceneRegistryStatus SceneService::customizeStop(SceneId sceneId) {
  const SceneRecord* source = registry_.find(sceneId);
  if (source == nullptr) return SceneRegistryStatus::NotFound;
  SceneRecord updated = *source;
  if (updated.stopMode == SceneStopMode::Custom) return SceneRegistryStatus::Ok;
  generateStopSteps(updated);
  updated.stopMode = SceneStopMode::Custom;
  return replace(updated);
}

SceneRegistryStatus SceneService::useGeneratedStop(SceneId sceneId) {
  const SceneRecord* source = registry_.find(sceneId);
  if (source == nullptr) return SceneRegistryStatus::NotFound;
  SceneRecord updated = *source;
  updated.stopMode = SceneStopMode::Generated;
  generateStopSteps(updated);
  return replace(updated);
}

SceneRegistryStatus SceneService::removeStep(SceneId sceneId, bool startList,
                                             uint8_t index) {
  const SceneRecord* source = registry_.find(sceneId);
  if (source == nullptr) return SceneRegistryStatus::NotFound;
  if (runner_.busy() && runner_.progress().sceneId == sceneId) {
    return SceneRegistryStatus::Invalid;
  }

  SceneRecord updated = *source;
  SceneStep* steps = startList ? updated.startSteps : updated.stopSteps;
  uint8_t& count = startList ? updated.startCount : updated.stopCount;
  if (index >= count) return SceneRegistryStatus::NotFound;
  for (uint8_t i = index + 1; i < count; ++i) {
    steps[i - 1] = steps[i];
  }
  steps[count - 1] = SceneStep{};
  --count;

  if (startList && updated.stopMode == SceneStopMode::Generated) {
    generateStopSteps(updated);
  }

  SceneRegistry previous = registry_;
  if (!previous.healthy()) return SceneRegistryStatus::Full;
  const SceneRegistryStatus status = registry_.replace(updated);
  if (status != SceneRegistryStatus::Ok || !save()) {
    registry_.swap(previous);
    return SceneRegistryStatus::Invalid;
  }
  if (runner_.validate(updated) == SceneValidationStatus::Ok) {
    runner_.refreshPrepared(sceneId);
  } else if (runner_.progress().sceneId == sceneId) {
    // A deletion is also the recovery path for empty or orphaned scenes.
    // Release any formerly prepared targets until the authored steps validate.
    runner_.cancel();
  }
  return SceneRegistryStatus::Ok;
}

SceneValidationStatus SceneService::validate(const SceneRecord& record) const {
  return runner_.validate(record);
}

SceneValidationStatus SceneService::validate(SceneId sceneId) const {
  return runner_.validate(sceneId);
}

bool SceneService::referencesInstance(InstanceId instanceId) const {
  for (size_t i = 0; i < registry_.count(); ++i) {
    const SceneRecord* record = registry_.at(i);
    if (record == nullptr) continue;
    const SceneStep* lists[] = {record->startSteps, record->stopSteps};
    const uint8_t counts[] = {record->startCount, record->stopCount};
    for (size_t list = 0; list < 2; ++list) {
      for (uint8_t step = 0; step < counts[list]; ++step) {
        if (lists[list][step].type == SceneStepType::Action &&
            lists[list][step].targetId == instanceId) {
          return true;
        }
      }
    }
  }
  return false;
}

bool SceneService::seedPressRecord(SceneId& outId) {
  outId = kInvalidSceneId;
  InstanceId cameraId = kInvalidInstanceId;
  InstanceId recorderId = kInvalidInstanceId;
  for (size_t i = 0; i < devices_.count(); ++i) {
    const DeviceRecord* record = devices_.at(i);
    if (record == nullptr || !record->enabled) {
      continue;
    }
    const DriverDescriptor* descriptor = DriverCatalog::find(record->driverId);
    if (descriptor == nullptr) {
      continue;
    }
    const bool canStart =
        (descriptor->capabilities & capabilityBit(Capability::RecordStart)) != 0;
    const bool canStop =
        (descriptor->capabilities & capabilityBit(Capability::RecordStop)) != 0;
    if (!canStart || !canStop) {
      continue;
    }
    if (descriptor->type == DeviceType::Camera && cameraId == kInvalidInstanceId) {
      cameraId = record->instanceId;
    } else if (descriptor->type == DeviceType::Recorder &&
               recorderId == kInvalidInstanceId) {
      recorderId = record->instanceId;
    }
  }
  if (cameraId == kInvalidInstanceId || recorderId == kInvalidInstanceId) {
    return false;
  }

  SceneId sceneId = kInvalidSceneId;
  if (add("Press Record", sceneId) != SceneRegistryStatus::Ok) {
    return false;
  }
  SceneRecord* record = registry_.find(sceneId);
  if (record == nullptr) {
    return false;
  }
  record->startCount = 3;
  record->startSteps[0] = makeActionStep(cameraId, CommandType::RecordStart);
  record->startSteps[1] = makeWaitStep(500);
  record->startSteps[2] = makeActionStep(recorderId, CommandType::RecordStart);
  record->stopMode = SceneStopMode::Generated;
  generateStopSteps(*record);
  if (!save()) {
    registry_.remove(sceneId);
    return false;
  }
  outId = sceneId;
  return true;
}

SceneRunStatus SceneService::prepare(SceneId sceneId) {
  return runner_.prepare(sceneId);
}

SceneRunStatus SceneService::start(SceneId sceneId) { return runner_.start(sceneId); }

SceneRunStatus SceneService::stop() { return runner_.stop(); }

void SceneService::cancel() { runner_.cancel(); }

}  // namespace studio
