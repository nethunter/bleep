#include "core/scene_service.h"

#include <cstring>

namespace studio {

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
  const SceneRegistryStatus status = registry_.add(name, outId);
  if (status == SceneRegistryStatus::Ok && !save()) {
    registry_.remove(outId);
    outId = kInvalidSceneId;
    return SceneRegistryStatus::Invalid;
  }
  return status;
}

SceneRegistryStatus SceneService::remove(SceneId sceneId) {
  if (runner_.busy() && runner_.progress().sceneId == sceneId) {
    return SceneRegistryStatus::Invalid;
  }
  const SceneRegistryStatus status = registry_.remove(sceneId);
  if (status == SceneRegistryStatus::Ok) {
    save();
  }
  return status;
}

SceneRegistryStatus SceneService::rename(SceneId sceneId, const char* name) {
  const SceneRegistryStatus status = registry_.rename(sceneId, name);
  if (status == SceneRegistryStatus::Ok) {
    save();
  }
  return status;
}

SceneRegistryStatus SceneService::replace(const SceneRecord& record) {
  if (runner_.validate(record) != SceneValidationStatus::Ok) {
    return SceneRegistryStatus::Invalid;
  }
  const SceneRegistryStatus status = registry_.replace(record);
  if (status == SceneRegistryStatus::Ok) {
    save();
    runner_.refreshPrepared(record.sceneId);
  }
  return status;
}

SceneValidationStatus SceneService::validate(const SceneRecord& record) const {
  return runner_.validate(record);
}

SceneValidationStatus SceneService::validate(SceneId sceneId) const {
  return runner_.validate(sceneId);
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
  record->stopCount = 2;
  record->stopSteps[0] = makeActionStep(cameraId, CommandType::RecordStop);
  record->stopSteps[1] = makeActionStep(recorderId, CommandType::RecordStop);
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
