#pragma once

#include "core/scene_runner.h"
#include "core/scene_store.h"

namespace studio {

class SceneService {
 public:
  SceneService(IConfigBackend& backend, DeviceManager& devices);

  bool begin();
  void loop(uint32_t nowMs);

  size_t count() const { return registry_.count(); }
  const SceneRecord* at(size_t index) const { return registry_.at(index); }
  const SceneRecord* find(SceneId sceneId) const { return registry_.find(sceneId); }

  SceneRegistryStatus add(const char* name, SceneId& outId);
  SceneRegistryStatus remove(SceneId sceneId);
  SceneRegistryStatus rename(SceneId sceneId, const char* name);
  SceneRegistryStatus replace(const SceneRecord& record);
  SceneValidationStatus validate(const SceneRecord& record) const;
  SceneValidationStatus validate(SceneId sceneId) const;
  bool referencesInstance(InstanceId instanceId) const;

  bool seedPressRecord(SceneId& outId);

  SceneRunStatus prepare(SceneId sceneId);
  SceneRunStatus start(SceneId sceneId);
  SceneRunStatus stop();
  void cancel();

  bool busy() const { return runner_.busy(); }
  bool holdsLinks() const { return runner_.holdsLinks(); }
  const SceneProgress& progress() const { return runner_.progress(); }

 private:
  bool save();

  SceneStore store_;
  DeviceManager& devices_;
  SceneRegistry registry_;
  SceneRunner runner_;
  bool begun_ = false;
};

SceneService& scenes();

}  // namespace studio
