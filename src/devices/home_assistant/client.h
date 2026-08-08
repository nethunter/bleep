#pragma once

#include <cstddef>
#include <cstdint>

#include "core/device_types.h"
#include "core/home_assistant_config.h"

namespace home_assistant {

struct EntityState {
  bool present = false;
  bool available = false;
  bool stateKnown = false;
  bool on = false;
  bool commandPending = false;
  bool commandAttempted = false;
  studio::CommandStatus lastCommand = studio::CommandStatus::Unavailable;
  char stateText[20] = "unknown";
};

class HomeAssistantClient {
 public:
  static constexpr size_t kMaxEntities = 4;

  ~HomeAssistantClient();

  bool activate(const studio::DeviceRecord& record);
  void deactivate(studio::InstanceId instanceId);
  void loop();
  studio::CommandStatus dispatch(const studio::DeviceCommand& command);
  studio::DeviceRuntimeState runtimeState(studio::InstanceId instanceId) const;
  const EntityState* entityState(studio::InstanceId instanceId) const;
  void enqueueFrame(const uint8_t* payload, size_t length);
  void markWebsocketDisconnected();

 private:
  struct Session {
    studio::InstanceId instanceId = studio::kInvalidInstanceId;
    studio::HomeAssistantDomain domain = studio::HomeAssistantDomain::None;
    char entityId[studio::kHomeAssistantEntityIdCapacity] = "";
    EntityState state;
    uint32_t pendingMessageId = 0;
    bool expectedOn = false;
    uint32_t pendingSince = 0;
    bool refreshNeeded = false;
    uint32_t refreshAfterMs = 0;
    uint8_t refreshFailures = 0;
  };

  Session* sessionFor(studio::InstanceId instanceId);
  const Session* sessionFor(studio::InstanceId instanceId) const;
  bool connectRuntime();
  void disconnectRuntime();
  void processMessage(const char* payload, size_t length);
  void rebuildSubscription();
  void refreshInitialStates();
  void refreshSession(Session& session);
  void applyState(Session& session, const char* state);
  void setFailure(studio::CommandStatus status);

  Session sessions_[kMaxEntities] = {};
  size_t sessionCount_ = 0;
  bool wifiStarted_ = false;
  bool websocketStarted_ = false;
  bool authenticated_ = false;
  bool subscribed_ = false;
  bool subscriptionDirty_ = false;
  uint32_t subscriptionId_ = 0;
  uint32_t nextMessageId_ = 10;
  uint32_t connectionStarted_ = 0;
  uint32_t retryAt_ = 0;
  uint8_t failures_ = 0;
  studio::HomeAssistantConfig config_;
  // HA auth/results fit comfortably; oversized state events intentionally
  // fall back to the bounded per-entity REST reconciliation path.
  static constexpr size_t kFrameCapacity = 2048;
  char frames_[2][kFrameCapacity + 1] = {};
  size_t frameLengths_[2] = {};
  uint8_t frameHead_ = 0;
  uint8_t frameTail_ = 0;
  bool frameFault_ = false;
  bool websocketDisconnected_ = false;
};

}  // namespace home_assistant
