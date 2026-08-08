#include "core/panel_settings.h"

#include "core/preferences_store.h"

namespace studio {

PanelSettingsService& panelSettings() {
  static PreferencesPanelSettingsBackend backend;
  static PanelSettingsService service(backend);
  return service;
}

}  // namespace studio
