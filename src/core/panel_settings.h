#pragma once

#include <cstdint>

#include "core/config_store.h"

namespace studio {

struct PanelSettings {
  bool hapticEnabled = true;
};

class PanelSettingsStore {
 public:
  explicit PanelSettingsStore(IConfigBackend& backend) : backend_(backend) {}

  ConfigLoadStatus load(PanelSettings& settings);
  bool save(const PanelSettings& settings);

 private:
  IConfigBackend& backend_;
};

class PanelSettingsService {
 public:
  explicit PanelSettingsService(IConfigBackend& backend) : store_(backend) {}

  bool begin();
  const PanelSettings& get() const { return settings_; }
  bool setHapticEnabled(bool enabled);

 private:
  PanelSettingsStore store_;
  PanelSettings settings_;
  bool begun_ = false;
};

PanelSettingsService& panelSettings();

}  // namespace studio
