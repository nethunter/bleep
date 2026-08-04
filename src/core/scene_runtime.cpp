#include "core/scene_service.h"

#include "core/preferences_store.h"

namespace studio {

SceneService& scenes() {
  static PreferencesScenesBackend backend;
  static SceneService service(backend, devices());
  return service;
}

}  // namespace studio
