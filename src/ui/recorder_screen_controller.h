#pragma once

#include <cstdint>

#include "core/device_types.h"
#include "ui/recorder_shell.h"

namespace studio_ui {

class RecorderScreenController {
 public:
  using Refresh = void (*)();

  explicit RecorderScreenController(recorder_shell::Owner owner)
      : owner_(owner) {}

  bool show(studio::InstanceId id, const recorder_shell::Options& options,
            const recorder_shell::Callbacks& callbacks, Refresh refresh);
  void hide();
  void release();
  void tick();
  void back();
  bool enqueue(studio::CommandType type) const;

  bool active() const { return visible_; }
  studio::InstanceId instanceId() const { return instanceId_; }

 private:
  recorder_shell::Owner owner_;
  studio::InstanceId instanceId_ = studio::kInvalidInstanceId;
  bool visible_ = false;
  uint32_t lastRefreshMs_ = 0;
  Refresh refresh_ = nullptr;
};

}  // namespace studio_ui
