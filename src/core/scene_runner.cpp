#include "core/scene_runner.h"

#include <cstdio>
#include <cstring>

#include "core/command_traits.h"

namespace studio {
namespace {

void copyDetail(char (&destination)[48], const char* text) {
  if (text == nullptr) {
    destination[0] = '\0';
    return;
  }
  std::strncpy(destination, text, sizeof(destination) - 1);
  destination[sizeof(destination) - 1] = '\0';
}

}  // namespace

SceneRunner::SceneRunner(DeviceManager& devices, SceneRegistry& registry)
    : devices_(devices), registry_(registry) {}

SceneValidationStatus SceneRunner::validate(const SceneRecord& record) const {
  if (record.name[0] == '\0') {
    return SceneValidationStatus::InvalidName;
  }
  if (record.startCount == 0 && record.stopCount == 0) {
    return SceneValidationStatus::Empty;
  }
  if (record.startCount > CONFIG_MAX_SCENE_STEPS ||
      record.stopCount > CONFIG_MAX_SCENE_STEPS) {
    return SceneValidationStatus::Full;
  }

  TargetSet targets;
  const SceneStep* lists[] = {record.startSteps, record.stopSteps};
  const uint8_t counts[] = {record.startCount, record.stopCount};
  for (size_t list = 0; list < 2; ++list) {
    for (uint8_t i = 0; i < counts[list]; ++i) {
      const SceneStep& step = lists[list][i];
      if (step.type == SceneStepType::Wait) {
        if (step.waitMs < CONFIG_SCENE_MIN_WAIT_MS ||
            step.waitMs > CONFIG_SCENE_MAX_WAIT_MS) {
          return SceneValidationStatus::WaitOutOfRange;
        }
        continue;
      }
      if (step.type != SceneStepType::Action) {
        return SceneValidationStatus::InvalidStep;
      }
      if (!commandAllowedInScene(step.command)) {
        return SceneValidationStatus::UnsupportedCommand;
      }
      const DeviceRecord* device = devices_.find(step.targetId);
      if (device == nullptr) {
        return SceneValidationStatus::MissingTarget;
      }
      if (!device->enabled) {
        return SceneValidationStatus::DisabledTarget;
      }
      const DriverDescriptor* descriptor = DriverCatalog::find(device->driverId);
      if (descriptor == nullptr) {
        return SceneValidationStatus::MissingTarget;
      }
      const Capability required = requiredCapability(step.command);
      if ((descriptor->capabilities & capabilityBit(required)) == 0) {
        return SceneValidationStatus::MissingCapability;
      }
      bool known = false;
      for (uint8_t t = 0; t < targets.count; ++t) {
        if (targets.ids[t] == step.targetId) {
          known = true;
          break;
        }
      }
      if (!known) {
        if (targets.count >= CONFIG_MAX_ACTIVE_LINKS) {
          return SceneValidationStatus::TooManyTargets;
        }
        targets.ids[targets.count++] = step.targetId;
      }
    }
  }
  return SceneValidationStatus::Ok;
}

SceneValidationStatus SceneRunner::validate(SceneId sceneId) const {
  const SceneRecord* record = registry_.find(sceneId);
  if (record == nullptr) {
    return SceneValidationStatus::MissingTarget;
  }
  return validate(*record);
}

bool SceneRunner::collectTargets(const SceneRecord& record, TargetSet& out) const {
  out = TargetSet{};
  const SceneStep* lists[] = {record.startSteps, record.stopSteps};
  const uint8_t counts[] = {record.startCount, record.stopCount};
  for (size_t list = 0; list < 2; ++list) {
    for (uint8_t i = 0; i < counts[list]; ++i) {
      const SceneStep& step = lists[list][i];
      if (step.type != SceneStepType::Action) {
        continue;
      }
      bool known = false;
      for (uint8_t t = 0; t < out.count; ++t) {
        if (out.ids[t] == step.targetId) {
          known = true;
          break;
        }
      }
      if (known) {
        continue;
      }
      if (out.count >= CONFIG_MAX_ACTIVE_LINKS) {
        return false;
      }
      out.ids[out.count++] = step.targetId;
    }
  }
  return true;
}

bool SceneRunner::activateTargets(const TargetSet& targets) {
  devices_.deactivateAll();
  for (uint8_t i = 0; i < targets.count; ++i) {
    if (!devices_.activateHeld(targets.ids[i])) {
      devices_.deactivateAll();
      return false;
    }
  }
  devices_.setLinksHeld(true);
  return true;
}

bool SceneRunner::allTargetsConnected(const TargetSet& targets,
                                      InstanceId& waiting) const {
  waiting = kInvalidInstanceId;
  for (uint8_t i = 0; i < targets.count; ++i) {
    const DeviceRuntimeState runtime = devices_.runtimeState(targets.ids[i]);
    if (runtime.link != LinkState::Connected) {
      waiting = targets.ids[i];
      return false;
    }
  }
  return true;
}

void SceneRunner::setDetail(const char* text) { copyDetail(progress_.detail, text); }

void SceneRunner::fail(SceneRunStatus status, const char* detail) {
  progress_.lastStatus = status;
  progress_.phase = ScenePhase::Failed;
  progress_.stepResult = SceneStepResult::Failed;
  setDetail(detail);
  direction_ = Direction::None;
  waitingForResult_ = false;
  pendingRequestId_ = 0;
  // Keep links held so Stop can still run after a partial Start failure.
  if (!devices_.linksHeld()) {
    devices_.setLinksHeld(targets_.count > 0);
  }
}

void SceneRunner::finishStop(SceneRunStatus status) {
  progress_.lastStatus = status;
  progress_.phase =
      status == SceneRunStatus::Ok ? ScenePhase::Completed : ScenePhase::Failed;
  progress_.stepResult = status == SceneRunStatus::Ok ? SceneStepResult::Succeeded
                                                      : SceneStepResult::Failed;
  setDetail(status == SceneRunStatus::Ok ? "Stopped" : "Stop failed");
  direction_ = Direction::None;
  waitingForResult_ = false;
  pendingRequestId_ = 0;
  devices_.deactivateAll();
  targets_ = TargetSet{};
}

bool SceneRunner::busy() const {
  return progress_.phase == ScenePhase::Connecting ||
         progress_.phase == ScenePhase::RunningStart ||
         progress_.phase == ScenePhase::RunningStop;
}

bool SceneRunner::holdsLinks() const {
  return devices_.linksHeld() || progress_.phase == ScenePhase::IdleArmed ||
         progress_.phase == ScenePhase::Connecting ||
         progress_.phase == ScenePhase::RunningStart ||
         progress_.phase == ScenePhase::RunningStop ||
         (progress_.phase == ScenePhase::Failed && targets_.count > 0);
}

SceneRunStatus SceneRunner::start(SceneId sceneId) {
  if (busy()) {
    return SceneRunStatus::Busy;
  }
  const SceneRecord* record = registry_.find(sceneId);
  if (record == nullptr) {
    return SceneRunStatus::InvalidScene;
  }
  if (!record->enabled) {
    return SceneRunStatus::Disabled;
  }
  if (validate(*record) != SceneValidationStatus::Ok) {
    return SceneRunStatus::ValidationFailed;
  }
  if (record->startCount == 0) {
    return SceneRunStatus::ValidationFailed;
  }
  if (!collectTargets(*record, targets_)) {
    return SceneRunStatus::ValidationFailed;
  }

  activeScene_ = *record;
  progress_ = SceneProgress{};
  progress_.sceneId = sceneId;
  progress_.phase = ScenePhase::Connecting;
  progress_.runningStart = true;
  progress_.stepCount = activeScene_.startCount;
  progress_.stepIndex = 0;
  progress_.stepResult = SceneStepResult::Running;
  setDetail("Connecting");
  direction_ = Direction::Start;
  phaseStartedMs_ = 0;
  waitUntilMs_ = 0;
  waitingForResult_ = false;
  pendingRequestId_ = 0;

  if (!activateTargets(targets_)) {
    fail(SceneRunStatus::ActionFailed, "Activate failed");
    return SceneRunStatus::ActionFailed;
  }
  return SceneRunStatus::Ok;
}

SceneRunStatus SceneRunner::stop() {
  if (progress_.phase == ScenePhase::RunningStop) {
    return SceneRunStatus::Busy;
  }
  if (progress_.sceneId == kInvalidSceneId) {
    return SceneRunStatus::InvalidScene;
  }
  if (progress_.phase != ScenePhase::IdleArmed &&
      progress_.phase != ScenePhase::Failed &&
      progress_.phase != ScenePhase::RunningStart &&
      progress_.phase != ScenePhase::Connecting) {
    return SceneRunStatus::InvalidScene;
  }
  if (activeScene_.stopCount == 0) {
    finishStop(SceneRunStatus::Ok);
    return SceneRunStatus::Ok;
  }

  // Abort an in-flight Start action and move to Stop while holding links.
  waitingForResult_ = false;
  pendingRequestId_ = 0;
  direction_ = Direction::Stop;
  progress_.phase = ScenePhase::RunningStop;
  progress_.runningStart = false;
  progress_.stepIndex = 0;
  progress_.stepCount = activeScene_.stopCount;
  progress_.stepResult = SceneStepResult::Running;
  progress_.lastStatus = SceneRunStatus::Ok;
  setDetail("Stopping");
  phaseStartedMs_ = 0;
  waitUntilMs_ = 0;

  if (!devices_.linksHeld() || devices_.activeCount() == 0) {
    if (!activateTargets(targets_)) {
      fail(SceneRunStatus::ActionFailed, "Reconnect failed");
      return SceneRunStatus::ActionFailed;
    }
    progress_.phase = ScenePhase::Connecting;
    setDetail("Reconnecting");
  } else {
    beginStep(0);
  }
  return SceneRunStatus::Ok;
}

void SceneRunner::cancel() {
  progress_.lastStatus = SceneRunStatus::Canceled;
  progress_.phase = ScenePhase::Idle;
  progress_.stepResult = SceneStepResult::Skipped;
  setDetail("Canceled");
  direction_ = Direction::None;
  waitingForResult_ = false;
  pendingRequestId_ = 0;
  devices_.deactivateAll();
  targets_ = TargetSet{};
  activeScene_ = SceneRecord{};
  progress_.sceneId = kInvalidSceneId;
}

const SceneStep* SceneRunner::currentStep() const {
  if (direction_ == Direction::Start) {
    return progress_.stepIndex < activeScene_.startCount
               ? &activeScene_.startSteps[progress_.stepIndex]
               : nullptr;
  }
  if (direction_ == Direction::Stop) {
    return progress_.stepIndex < activeScene_.stopCount
               ? &activeScene_.stopSteps[progress_.stepIndex]
               : nullptr;
  }
  return nullptr;
}

void SceneRunner::dispatchCurrentAction() {
  const SceneStep* step = currentStep();
  if (step == nullptr || step->type != SceneStepType::Action) {
    return;
  }
  DeviceCommand command;
  command.instanceId = step->targetId;
  command.type = step->command;
  if (!devices_.enqueue(command)) {
    fail(SceneRunStatus::ActionFailed, "Queue full");
    return;
  }
  // requestId assigned in enqueue; recover from next result match by instance.
  waitingForResult_ = true;
  pendingRequestId_ = 0;
  progress_.stepResult = SceneStepResult::Running;
  char detail[48];
  std::snprintf(detail, sizeof(detail), "Step %u",
                static_cast<unsigned>(progress_.stepIndex + 1));
  setDetail(detail);
}

void SceneRunner::beginStep(uint32_t nowMs) {
  const SceneStep* step = currentStep();
  if (step == nullptr) {
    if (direction_ == Direction::Start) {
      progress_.phase = ScenePhase::IdleArmed;
      progress_.stepResult = SceneStepResult::Succeeded;
      progress_.lastStatus = SceneRunStatus::Ok;
      setDetail("Recording");
      direction_ = Direction::None;
      waitingForResult_ = false;
    } else {
      finishStop(SceneRunStatus::Ok);
    }
    return;
  }

  progress_.stepResult = SceneStepResult::Running;
  if (step->type == SceneStepType::Wait) {
    waitUntilMs_ = nowMs + step->waitMs;
    char detail[48];
    std::snprintf(detail, sizeof(detail), "Wait %u ms",
                  static_cast<unsigned>(step->waitMs));
    setDetail(detail);
    return;
  }
  waitUntilMs_ = 0;
  phaseStartedMs_ = nowMs;
  dispatchCurrentAction();
}

void SceneRunner::advanceStep(uint32_t nowMs) {
  progress_.stepResult = SceneStepResult::Succeeded;
  ++progress_.stepIndex;
  waitingForResult_ = false;
  pendingRequestId_ = 0;
  beginStep(nowMs);
}

void SceneRunner::tick(uint32_t nowMs) {
  if (progress_.phase == ScenePhase::Idle ||
      progress_.phase == ScenePhase::Completed) {
    return;
  }

  if (phaseStartedMs_ == 0) {
    phaseStartedMs_ = nowMs;
  }

  if (progress_.phase == ScenePhase::Connecting) {
    InstanceId waiting = kInvalidInstanceId;
    if (allTargetsConnected(targets_, waiting)) {
      progress_.connectTarget = kInvalidInstanceId;
      if (direction_ == Direction::Stop) {
        progress_.phase = ScenePhase::RunningStop;
        setDetail("Stopping");
        beginStep(nowMs);
      } else {
        progress_.phase = ScenePhase::RunningStart;
        setDetail("Starting");
        beginStep(nowMs);
      }
      return;
    }
    progress_.connectTarget = waiting;
    const DeviceRecord* record = devices_.find(waiting);
    char detail[48];
    std::snprintf(detail, sizeof(detail), "Connect %s",
                  record != nullptr ? record->displayName : "?");
    setDetail(detail);
    if (nowMs - phaseStartedMs_ >= CONFIG_SCENE_CONNECT_TIMEOUT_MS) {
      fail(SceneRunStatus::ConnectTimeout, "Connect timeout");
    }
    return;
  }

  if (progress_.phase != ScenePhase::RunningStart &&
      progress_.phase != ScenePhase::RunningStop) {
    return;
  }

  const SceneStep* step = currentStep();
  if (step == nullptr) {
    beginStep(nowMs);
    return;
  }

  if (step->type == SceneStepType::Wait) {
    if (nowMs >= waitUntilMs_) {
      advanceStep(nowMs);
    }
    return;
  }

  if (waitingForResult_) {
    CommandResult result;
    while (devices_.popResult(result)) {
      if (result.instanceId != step->targetId) {
        continue;
      }
      waitingForResult_ = false;
      if (result.status != CommandStatus::Succeeded) {
        fail(SceneRunStatus::ActionFailed, "Action failed");
        return;
      }
      advanceStep(nowMs);
      return;
    }
    if (nowMs - phaseStartedMs_ >= CONFIG_SCENE_ACTION_TIMEOUT_MS) {
      fail(SceneRunStatus::ActionTimeout, "Action timeout");
    }
  }
}

}  // namespace studio
