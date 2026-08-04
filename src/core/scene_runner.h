#pragma once

#include "core/device_manager.h"
#include "core/scene_registry.h"

namespace studio {

class SceneRunner {
 public:
  SceneRunner(DeviceManager& devices, SceneRegistry& registry);

  SceneValidationStatus validate(const SceneRecord& record) const;
  SceneValidationStatus validate(SceneId sceneId) const;

  SceneRunStatus start(SceneId sceneId);
  SceneRunStatus stop();
  void cancel();
  void tick(uint32_t nowMs);

  bool busy() const;
  bool holdsLinks() const;
  const SceneProgress& progress() const { return progress_; }

 private:
  enum class Direction : uint8_t { None, Start, Stop };

  struct TargetSet {
    InstanceId ids[CONFIG_MAX_ACTIVE_LINKS] = {};
    uint8_t count = 0;
  };

  bool collectTargets(const SceneRecord& record, TargetSet& out) const;
  bool activateTargets(const TargetSet& targets);
  bool allTargetsConnected(const TargetSet& targets, InstanceId& waiting) const;
  void setDetail(const char* text);
  void fail(SceneRunStatus status, const char* detail);
  void finishStop(SceneRunStatus status);
  void beginStep(uint32_t nowMs);
  void advanceStep(uint32_t nowMs);
  void dispatchCurrentAction();
  const SceneStep* currentStep() const;

  DeviceManager& devices_;
  SceneRegistry& registry_;
  SceneProgress progress_;
  SceneRecord activeScene_;
  TargetSet targets_;
  Direction direction_ = Direction::None;
  uint32_t phaseStartedMs_ = 0;
  uint32_t waitUntilMs_ = 0;
  uint32_t pendingRequestId_ = 0;
  bool waitingForResult_ = false;
};

}  // namespace studio
