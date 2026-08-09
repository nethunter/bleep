#pragma once

#include <cstdint>
#include <lvgl.h>

// Shared recording-control shell for Canon (Smart) and Tascam. Device adapters
// own protocol state and map it into View; this module owns LVGL objects only.
// At most one adapter owns the shell at a time.
namespace recorder_shell {

enum class Owner : uint8_t {
  None, CanonBle, TascamX8, GoPro, PhoneCamera, Insta360, DjiOsmo
};

struct Options {
  bool enablePower = false;
  bool enableUnknownControls = false;
};

struct View {
  const char* title = "";
  const char* status = "";
  const char* detail = "";
  const char* actionLabel = "WAITING";
  uint32_t actionColor = 0xE53935;
  bool actionEnabled = false;
  bool showUnknownControls = false;
  bool powerEnabled = false;
};

struct Callbacks {
  void (*onBack)() = nullptr;
  void (*onAction)() = nullptr;
  void (*onUnknownStart)() = nullptr;
  void (*onUnknownStop)() = nullptr;
  void (*onPower)() = nullptr;
};

// Creates or replaces the shell for `owner`. The previous shell must not be the
// active LVGL screen; call ui::parkForScreenRebuild() first if needed.
void acquire(Owner owner, const Options& options, const Callbacks& callbacks);
// Destroys the shell only when `owner` currently holds it and it is inactive.
void release(Owner owner);
// Destroys any idle shell regardless of owner.
void destroyIdle();
bool ownedBy(Owner owner);
lv_obj_t* screen();
void apply(const View& view);

}  // namespace recorder_shell
