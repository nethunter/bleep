#include "core/scene_runner.h"

#include <cstdio>
#include <cstring>

#if defined(ARDUINO)
#include <Arduino.h>
#endif

#include "core/command_traits.h"
#include "core/ble/ble_timing.h"

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

void logSceneDefinition(const char* event, const SceneRecord& record) {
#if defined(ARDUINO)
#if ARDUINO_USB_CDC_ON_BOOT
  Print& output = Serial0;
#else
  Print& output = Serial;
#endif
  output.printf("scene event=%s scene=%lu name=%s start_count=%u stop_count=%u\n",
                event, static_cast<unsigned long>(record.sceneId), record.name,
                static_cast<unsigned>(record.startCount),
                static_cast<unsigned>(record.stopCount));
  for (uint8_t i = 0; i < record.startCount; ++i) {
    const SceneStep& step = record.startSteps[i];
    output.printf(
        "scene event=start_step scene=%lu step=%u type=%u target=%lu "
        "command=%u wait_ms=%lu value0=%ld value1=%ld value2=%ld\n",
        static_cast<unsigned long>(record.sceneId),
        static_cast<unsigned>(i + 1), static_cast<unsigned>(step.type),
        static_cast<unsigned long>(step.targetId),
        static_cast<unsigned>(step.command),
        static_cast<unsigned long>(step.waitMs), static_cast<long>(step.value0),
        static_cast<long>(step.value1), static_cast<long>(step.value2));
  }
#else
  (void)event;
  (void)record;
#endif
}

void logSceneAction(const char* event, SceneId sceneId, uint8_t stepIndex,
                    const SceneStep* step, int status,
                    const DeviceRuntimeState* runtime = nullptr) {
#if defined(ARDUINO)
#if ARDUINO_USB_CDC_ON_BOOT
  Print& output = Serial0;
#else
  Print& output = Serial;
#endif
  output.printf(
      "scene event=%s scene=%lu step=%u target=%lu command=%u status=%d "
      "link=%u ready=%u pending=%u failed=%u\n",
      event, static_cast<unsigned long>(sceneId),
      static_cast<unsigned>(stepIndex + 1),
      static_cast<unsigned long>(step != nullptr ? step->targetId
                                                 : kInvalidInstanceId),
      static_cast<unsigned>(step != nullptr ? step->command
                                            : CommandType::Refresh),
      status,
      static_cast<unsigned>(runtime != nullptr ? runtime->link
                                               : LinkState::Disconnected),
      runtime != nullptr && runtime->protocolReady ? 1u : 0u,
      runtime != nullptr && runtime->commandPending ? 1u : 0u,
      runtime != nullptr && runtime->commandFailed ? 1u : 0u);
#else
  (void)event;
  (void)sceneId;
  (void)stepIndex;
  (void)step;
  (void)status;
  (void)runtime;
#endif
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
      const InstanceProfile profile = devices_.profile(device->instanceId);
      if (profile.type == DeviceType::Unknown) {
        return SceneValidationStatus::MissingTarget;
      }
      const uint32_t required = requiredCapabilities(step.command);
      if (required == 0 || (profile.capabilities & required) != required) {
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
        if (targets.count >= CONFIG_MAX_ACTIVE_INSTANCES) {
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
      if (out.count >= CONFIG_MAX_ACTIVE_INSTANCES) {
        return false;
      }
      out.ids[out.count++] = step.targetId;
    }
  }
  return true;
}

bool SceneRunner::containsTarget(const TargetSet& targets,
                                 InstanceId instanceId) const {
  for (uint8_t i = 0; i < targets.count; ++i) {
    if (targets.ids[i] == instanceId) return true;
  }
  return false;
}

bool SceneRunner::activateTargets(const TargetSet& targets) {
  homeAssistantDeferred_ = false;
  homeAssistantPreparation_ = false;
  bool hasPhysicalTarget = false;
  bool hasHomeAssistantTarget = false;
  for (uint8_t i = 0; i < targets.count; ++i) {
    const DeviceRecord* record = devices_.find(targets.ids[i]);
    if (record != nullptr && record->driverId == DriverId::HomeAssistant) {
      hasHomeAssistantTarget = true;
    } else {
      hasPhysicalTarget = true;
    }
  }
  homeAssistantPreparation_ = hasHomeAssistantTarget && !hasPhysicalTarget;

  InstanceId acquiredIds[CONFIG_MAX_ACTIVE_INSTANCES] = {};
  uint8_t acquiredCount = 0;
  // BLE setup needs both a large contiguous initialization allocation and
  // enough headroom for blocking GATT discovery. Bring physical transports to
  // protocol readiness before HA starts Wi-Fi, regardless of authored order.
  for (uint8_t pass = 0; pass < 2; ++pass) {
    const bool homeAssistantPass = pass == 1;
    if (homeAssistantPass && hasPhysicalTarget && hasHomeAssistantTarget) {
      homeAssistantDeferred_ = true;
      break;
    }
    for (uint8_t i = 0; i < targets.count; ++i) {
      const DeviceRecord* record = devices_.find(targets.ids[i]);
      const bool homeAssistant =
          record != nullptr && record->driverId == DriverId::HomeAssistant;
      if (homeAssistant != homeAssistantPass) {
        continue;
      }
      const bool acquired =
          devices_.acquire(targets.ids[i], ConnectionOwner::Sequence);
      const SceneStep diagnosticStep =
          makeActionStep(targets.ids[i], CommandType::Connect);
      const DeviceRuntimeState runtime = devices_.runtimeState(targets.ids[i]);
      logSceneAction("acquire_target", progress_.sceneId, i, &diagnosticStep,
                     acquired ? 0 : 1, &runtime);
      if (!acquired) {
        for (uint8_t acquired = 0; acquired < acquiredCount; ++acquired) {
          devices_.release(acquiredIds[acquired], ConnectionOwner::Sequence);
        }
        return false;
      }
      acquiredIds[acquiredCount++] = targets.ids[i];
    }
  }
  return true;
}

bool SceneRunner::activateDeferredHomeAssistantTargets() {
  InstanceId acquiredIds[CONFIG_MAX_ACTIVE_INSTANCES] = {};
  uint8_t acquiredCount = 0;
  for (uint8_t i = 0; i < targets_.count; ++i) {
    const DeviceRecord* record = devices_.find(targets_.ids[i]);
    if (record == nullptr || record->driverId != DriverId::HomeAssistant) {
      continue;
    }
    if (!devices_.acquire(targets_.ids[i], ConnectionOwner::Sequence)) {
      for (uint8_t acquired = 0; acquired < acquiredCount; ++acquired) {
        devices_.release(acquiredIds[acquired], ConnectionOwner::Sequence);
      }
      return false;
    }
    acquiredIds[acquiredCount++] = targets_.ids[i];
  }
  homeAssistantPreparation_ = true;
  return true;
}

void SceneRunner::releaseTargets(const TargetSet& targets) {
  for (uint8_t i = 0; i < targets.count; ++i) {
    devices_.release(targets.ids[i], ConnectionOwner::Sequence);
  }
}

void SceneRunner::releaseTargetsExcept(const TargetSet& current,
                                       const TargetSet& next) {
  for (uint8_t i = 0; i < current.count; ++i) {
    if (!containsTarget(next, current.ids[i])) {
      devices_.release(current.ids[i], ConnectionOwner::Sequence);
    }
  }
}

bool SceneRunner::ownsTargets(const TargetSet& targets) const {
  if (targets.count == 0) {
    return false;
  }
  for (uint8_t i = 0; i < targets.count; ++i) {
    if (!devices_.ownedBy(targets.ids[i], ConnectionOwner::Sequence)) {
      return false;
    }
  }
  return true;
}

bool SceneRunner::allTargetsConnected(const TargetSet& targets,
                                      InstanceId& waiting) const {
  waiting = kInvalidInstanceId;
  for (uint8_t i = 0; i < targets.count; ++i) {
    const DeviceRuntimeState runtime = devices_.runtimeState(targets.ids[i]);
    if (runtime.link != LinkState::Connected || !runtime.protocolReady) {
      waiting = targets.ids[i];
      return false;
    }
  }
  return true;
}

bool SceneRunner::allPhysicalTargetsConnected(const TargetSet& targets,
                                              InstanceId& waiting) const {
  waiting = kInvalidInstanceId;
  for (uint8_t i = 0; i < targets.count; ++i) {
    const DeviceRecord* record = devices_.find(targets.ids[i]);
    if (record != nullptr && record->driverId == DriverId::HomeAssistant) {
      continue;
    }
    const DeviceRuntimeState runtime = devices_.runtimeState(targets.ids[i]);
    if (runtime.link != LinkState::Connected || !runtime.protocolReady) {
      waiting = targets.ids[i];
      return false;
    }
  }
  return true;
}

void SceneRunner::setDetail(const char* text) { copyDetail(progress_.detail, text); }

void SceneRunner::fail(SceneRunStatus status, const char* detail) {
  const SceneStep* step = currentStep();
  const DeviceRuntimeState runtime =
      step != nullptr ? devices_.runtimeState(step->targetId)
                      : DeviceRuntimeState{};
  logSceneAction("failed", progress_.sceneId, progress_.stepIndex, step,
                 static_cast<int>(status), &runtime);
  progress_.lastStatus = status;
  progress_.phase = ScenePhase::Failed;
  progress_.stepResult = SceneStepResult::Failed;
  setDetail(detail);
  direction_ = Direction::None;
  waitingForResult_ = false;
  pendingRequestId_ = 0;
  // Sequence ownership stays in place so Stop can still run after a partial
  // Start failure.
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
}

bool SceneRunner::busy() const {
  return progress_.phase == ScenePhase::Connecting ||
         progress_.phase == ScenePhase::RunningStart ||
         progress_.phase == ScenePhase::RunningStop;
}

bool SceneRunner::holdsLinks() const {
  return ownsTargets(targets_) || progress_.phase == ScenePhase::Ready ||
         progress_.phase == ScenePhase::IdleArmed ||
         progress_.phase == ScenePhase::Connecting ||
         progress_.phase == ScenePhase::RunningStart ||
         progress_.phase == ScenePhase::RunningStop ||
         (progress_.phase == ScenePhase::Failed && targets_.count > 0);
}

SceneRunStatus SceneRunner::prepare(SceneId sceneId) {
  if (busy()) {
    return SceneRunStatus::Busy;
  }
  if (progress_.phase == ScenePhase::Ready && progress_.sceneId == sceneId &&
      ownsTargets(targets_)) {
    return SceneRunStatus::Ok;
  }
  if (progress_.phase == ScenePhase::IdleArmed && progress_.sceneId == sceneId) {
    return SceneRunStatus::Ok;
  }
  const bool switchingScenes =
      holdsLinks() && progress_.sceneId != sceneId;
  const SceneRecord* record = registry_.find(sceneId);
  if (record == nullptr) {
    if (switchingScenes) cancel();
    return SceneRunStatus::InvalidScene;
  }
  if (!record->enabled) {
    if (switchingScenes) cancel();
    return SceneRunStatus::Disabled;
  }
  logSceneDefinition("prepare", *record);
  const SceneValidationStatus validation = validate(*record);
  if (validation != SceneValidationStatus::Ok) {
    logSceneAction("validation_failed", sceneId, 0, nullptr,
                   static_cast<int>(validation));
    if (switchingScenes) cancel();
    progress_ = SceneProgress{};
    progress_.sceneId = sceneId;
    progress_.phase = ScenePhase::Idle;
    progress_.lastStatus = SceneRunStatus::ValidationFailed;
    setDetail(validation == SceneValidationStatus::Empty ? "Add steps"
                                                         : "Invalid sequence");
    activeScene_ = *record;
    targets_ = TargetSet{};
    direction_ = Direction::None;
    return SceneRunStatus::ValidationFailed;
  }
  TargetSet nextTargets;
  if (!collectTargets(*record, nextTargets)) {
    return SceneRunStatus::ValidationFailed;
  }

  if (switchingScenes) {
    // Transfer sequence ownership atomically: shared targets never become
    // idle eviction candidates between scenes. Only old-only resources are
    // released before the new target set is acquired.
    releaseTargetsExcept(targets_, nextTargets);
  }
  targets_ = nextTargets;

  activeScene_ = *record;
  progress_ = SceneProgress{};
  progress_.sceneId = sceneId;
  progress_.phase = ScenePhase::Connecting;
  progress_.stepCount = activeScene_.startCount;
  progress_.stepIndex = 0;
  progress_.stepResult = SceneStepResult::Pending;
  setDetail("Connecting");
  direction_ = Direction::Prepare;
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

SceneRunStatus SceneRunner::refreshPrepared(SceneId sceneId) {
  if (busy()) {
    return SceneRunStatus::Busy;
  }
  const SceneRecord* record = registry_.find(sceneId);
  if (record == nullptr) {
    return SceneRunStatus::InvalidScene;
  }
  if (progress_.sceneId != sceneId || !ownsTargets(targets_)) {
    return SceneRunStatus::Ok;
  }

  TargetSet nextTargets;
  if (validate(*record) != SceneValidationStatus::Ok ||
      !collectTargets(*record, nextTargets)) {
    return SceneRunStatus::ValidationFailed;
  }
  // Drop only sequence ownership, then reacquire through the same physical-
  // before-HA path used by prepare(). Ready physical sessions remain retained,
  // while an HA session can be stopped before a newly added BLE target needs
  // NimBLE's contiguous initialization allocation.
  releaseTargetsExcept(targets_, nextTargets);
  targets_ = nextTargets;
  activeScene_ = *record;
  if (!activateTargets(targets_)) {
    fail(SceneRunStatus::ActionFailed, "Activate failed");
    return SceneRunStatus::ActionFailed;
  }
  progress_.stepCount = activeScene_.startCount;
  progress_.stepIndex = 0;
  progress_.lastStatus = SceneRunStatus::Ok;
  InstanceId waiting = kInvalidInstanceId;
  if (allTargetsConnected(targets_, waiting)) {
    progress_.phase = ScenePhase::Ready;
    progress_.connectTarget = kInvalidInstanceId;
    progress_.stepResult = SceneStepResult::Succeeded;
    setDetail("Ready");
    direction_ = Direction::None;
  } else {
    progress_.phase = ScenePhase::Connecting;
    progress_.connectTarget = waiting;
    progress_.stepResult = SceneStepResult::Pending;
    setDetail("Connecting");
    direction_ = Direction::Prepare;
  }
  phaseStartedMs_ = 0;
  waitUntilMs_ = 0;
  waitingForResult_ = false;
  pendingRequestId_ = 0;
  return SceneRunStatus::Ok;
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
  logSceneDefinition("start", *record);
  const SceneValidationStatus validation = validate(*record);
  if (validation != SceneValidationStatus::Ok) {
    logSceneAction("validation_failed", sceneId, 0, nullptr,
                   static_cast<int>(validation));
    return SceneRunStatus::ValidationFailed;
  }
  if (record->startCount == 0) {
    return SceneRunStatus::ValidationFailed;
  }

  const bool reusablePhase = progress_.phase == ScenePhase::Ready ||
                             progress_.phase == ScenePhase::Completed;
  InstanceId waiting = kInvalidInstanceId;
  const bool alreadyPrepared = reusablePhase &&
                               progress_.sceneId == sceneId &&
                               ownsTargets(targets_) &&
                               allTargetsConnected(targets_, waiting);
  if (!alreadyPrepared) {
    if (!collectTargets(*record, targets_)) {
      return SceneRunStatus::ValidationFailed;
    }
  }

  activeScene_ = *record;
  if (alreadyPrepared) {
    progress_.phase = ScenePhase::RunningStart;
    progress_.runningStart = true;
    progress_.stepCount = activeScene_.startCount;
    progress_.stepIndex = 0;
    progress_.stepResult = SceneStepResult::Running;
    progress_.lastStatus = SceneRunStatus::Ok;
    setDetail("Starting");
    direction_ = Direction::Start;
    phaseStartedMs_ = 0;
    waitUntilMs_ = 0;
    waitingForResult_ = false;
    pendingRequestId_ = 0;
    beginStep(0);
    return SceneRunStatus::Ok;
  }

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
  // Preparing (not yet Ready) must not jump into Stop mid-connect-for-prepare.
  if (progress_.phase == ScenePhase::Connecting &&
      direction_ == Direction::Prepare) {
    return SceneRunStatus::InvalidScene;
  }
  if (activeScene_.stopCount == 0) {
    finishStop(SceneRunStatus::Ok);
    return SceneRunStatus::Ok;
  }

  // Abort an in-flight Start action and move to Stop while holding links. The
  // request/result and driver transaction must be superseded together so a
  // delayed compound stage cannot undo the first generated Stop action.
  if (progress_.phase == ScenePhase::RunningStart) {
    const SceneStep* interrupted = currentStep();
    if (interrupted != nullptr && interrupted->type == SceneStepType::Action &&
        pendingRequestId_ != 0) {
      devices_.cancelCommand(pendingRequestId_, interrupted->targetId);
    }
  }
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

  if (!ownsTargets(targets_)) {
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
  homeAssistantDeferred_ = false;
  releaseTargets(targets_);
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
  command.value0 = step->value0;
  command.value1 = step->value1;
  command.value2 = step->value2;
  const DeviceRuntimeState before = devices_.runtimeState(step->targetId);
  logSceneAction("dispatch", progress_.sceneId, progress_.stepIndex, step, 0,
                 &before);
  if (!devices_.enqueue(command, &pendingRequestId_)) {
    fail(SceneRunStatus::ActionFailed, "Queue full");
    return;
  }
  waitingForResult_ = true;
  waitingForConfirmation_ = false;
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
  waitingForConfirmation_ = false;
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

  if (progress_.phase == ScenePhase::Connecting && homeAssistantDeferred_) {
    InstanceId waiting = kInvalidInstanceId;
    if (!allPhysicalTargetsConnected(targets_, waiting)) {
      if (progress_.connectTarget != waiting) {
        const DeviceRuntimeState runtime = devices_.runtimeState(waiting);
        logSceneAction("waiting_for_target", progress_.sceneId, 0, nullptr,
                       static_cast<int>(waiting), &runtime);
      }
      progress_.connectTarget = waiting;
      const DeviceRecord* record = devices_.find(waiting);
      char detail[48];
      std::snprintf(detail, sizeof(detail), "Connect %s",
                    record != nullptr ? record->displayName : "?");
      setDetail(detail);
      if (nowMs - phaseStartedMs_ >=
          CONFIG_SCENE_PHYSICAL_CONNECT_TIMEOUT_MS) {
        fail(SceneRunStatus::ConnectTimeout, "Connect timeout");
      }
      return;
    }
    // Physical BLE preparation has its own cold-start budget. Once it
    // succeeds, give deferred Wi-Fi/WebSocket setup a fresh timeout instead
    // of charging it for the AK-BT1 or camera wake interval.
    phaseStartedMs_ = nowMs;
    homeAssistantDeferred_ = false;
    if (!activateDeferredHomeAssistantTargets()) {
      releaseTargets(targets_);
      fail(SceneRunStatus::ActionFailed, "Activate failed");
      return;
    }
  }

  if (progress_.phase == ScenePhase::Connecting) {
    InstanceId waiting = kInvalidInstanceId;
    if (allTargetsConnected(targets_, waiting)) {
      progress_.connectTarget = kInvalidInstanceId;
      if (direction_ == Direction::Stop) {
        progress_.phase = ScenePhase::RunningStop;
        setDetail("Stopping");
        beginStep(nowMs);
      } else if (direction_ == Direction::Prepare) {
        progress_.phase = ScenePhase::Ready;
        progress_.stepResult = SceneStepResult::Succeeded;
        progress_.lastStatus = SceneRunStatus::Ok;
        setDetail("Ready");
        studio::ble::logTiming("sequence", studio::ble::kInvalidLinkHandle,
                               "all_targets_ready",
                               nowMs - phaseStartedMs_,
                               nowMs - phaseStartedMs_, "ok");
        direction_ = Direction::None;
        phaseStartedMs_ = nowMs;
      } else {
        progress_.phase = ScenePhase::RunningStart;
        setDetail("Starting");
        beginStep(nowMs);
      }
      return;
    }
    if (progress_.connectTarget != waiting) {
      const DeviceRuntimeState runtime = devices_.runtimeState(waiting);
      logSceneAction("waiting_for_target", progress_.sceneId, 0, nullptr,
                     static_cast<int>(waiting), &runtime);
    }
    progress_.connectTarget = waiting;
    const DeviceRecord* record = devices_.find(waiting);
    char detail[48];
    std::snprintf(detail, sizeof(detail), "Connect %s",
                  record != nullptr ? record->displayName : "?");
    setDetail(detail);
    const uint32_t connectTimeoutMs =
        homeAssistantPreparation_ ? CONFIG_SCENE_CONNECT_TIMEOUT_MS
                                  : CONFIG_SCENE_PHYSICAL_CONNECT_TIMEOUT_MS;
    if (nowMs - phaseStartedMs_ >= connectTimeoutMs) {
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

  if (waitingForConfirmation_) {
    const DeviceRuntimeState runtime = devices_.runtimeState(step->targetId);
    if (!runtime.commandPending) {
      if (runtime.commandFailed) {
        logSceneAction("confirmation_failed", progress_.sceneId,
                       progress_.stepIndex, step, 0, &runtime);
        fail(SceneRunStatus::ActionFailed, "Confirmation failed");
      } else {
        logSceneAction("confirmed", progress_.sceneId, progress_.stepIndex,
                       step, 0, &runtime);
        advanceStep(nowMs);
      }
      return;
    }
    if (nowMs - phaseStartedMs_ >= CONFIG_SCENE_ACTION_TIMEOUT_MS) {
      fail(SceneRunStatus::ActionTimeout, "Confirmation timeout");
    }
    return;
  }

  if (waitingForResult_) {
    CommandResult result;
    if (devices_.takeResult(pendingRequestId_, result)) {
      waitingForResult_ = false;
      const DeviceRuntimeState runtime = devices_.runtimeState(step->targetId);
      logSceneAction("dispatch_result", progress_.sceneId,
                     progress_.stepIndex, step,
                     static_cast<int>(result.status), &runtime);
      if (result.status != CommandStatus::Succeeded) {
        fail(SceneRunStatus::ActionFailed, "Action failed");
        return;
      }
      if (runtime.commandPending) {
        waitingForConfirmation_ = true;
      } else {
        advanceStep(nowMs);
      }
      return;
    }
    if (nowMs - phaseStartedMs_ >= CONFIG_SCENE_ACTION_TIMEOUT_MS) {
      fail(SceneRunStatus::ActionTimeout, "Action timeout");
    }
  }
}

}  // namespace studio
