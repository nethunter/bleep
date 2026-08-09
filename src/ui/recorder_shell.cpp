#include "ui/recorder_shell.h"

#include "fonts/ui_fonts.h"
#include "haptic_feedback.h"
#include "ui/round_page.h"

namespace recorder_shell {

namespace {

constexpr uint32_t kBg = 0x05070A;
constexpr uint32_t kPanel = 0x171B22;
constexpr uint32_t kAccent = 0xE53935;
constexpr uint32_t kReady = 0x2E7D5B;
constexpr uint32_t kText = 0xF3F4F6;
constexpr uint32_t kMuted = 0x8A94A6;

Owner currentOwner = Owner::None;
lv_obj_t* root = nullptr;
lv_obj_t* titleLabel = nullptr;
lv_obj_t* statusLabel = nullptr;
lv_obj_t* detailLabel = nullptr;
lv_obj_t* powerButton = nullptr;
lv_obj_t* actionRing = nullptr;
lv_obj_t* actionButton = nullptr;
lv_obj_t* actionLabel = nullptr;
lv_obj_t* unknownControls = nullptr;
lv_obj_t* unknownStart = nullptr;
lv_obj_t* unknownStop = nullptr;
bool powerFeature = false;
bool unknownFeature = false;

Callbacks callbacks {};

void showUnknownControls(bool show) {
  if (!unknownFeature || unknownControls == nullptr || actionRing == nullptr) {
    return;
  }
  if (show) {
    lv_obj_add_flag(actionRing, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(unknownControls, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_clear_flag(actionRing, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(unknownControls, LV_OBJ_FLAG_HIDDEN);
  }
}

void setAction(const char* label, uint32_t color, bool enabled) {
  showUnknownControls(false);
  lv_label_set_text(actionLabel, label);
  lv_obj_set_style_border_color(actionRing, lv_color_hex(color), 0);
  lv_obj_set_style_bg_color(actionButton,
                            lv_color_hex(enabled ? color : kPanel), 0);
  if (enabled) {
    lv_obj_clear_state(actionButton, LV_STATE_DISABLED);
  } else {
    lv_obj_add_state(actionButton, LV_STATE_DISABLED);
  }
}

void setPowerEnabled(bool enabled) {
  if (!powerFeature || powerButton == nullptr) {
    return;
  }
  if (enabled) {
    lv_obj_clear_state(powerButton, LV_STATE_DISABLED);
  } else {
    lv_obj_add_state(powerButton, LV_STATE_DISABLED);
  }
}

void onBack(lv_event_t*) {
  haptic_feedback::request(haptic_feedback::Pattern::Back);
  if (callbacks.onBack != nullptr) {
    callbacks.onBack();
  }
}

void onAction(lv_event_t*) {
  if (callbacks.onAction != nullptr) {
    callbacks.onAction();
  }
}

void onUnknownStart(lv_event_t*) {
  if (callbacks.onUnknownStart != nullptr) {
    callbacks.onUnknownStart();
  }
}

void onUnknownStop(lv_event_t*) {
  if (callbacks.onUnknownStop != nullptr) {
    callbacks.onUnknownStop();
  }
}

void onPower(lv_event_t*) {
  if (callbacks.onPower != nullptr) {
    callbacks.onPower();
  }
}

lv_obj_t* createUnknownButton(lv_obj_t* parent, const char* label, uint32_t color,
                              lv_event_cb_t callback) {
  lv_obj_t* button = lv_btn_create(parent);
  lv_obj_set_size(button, 78, 62);
  lv_obj_set_style_radius(button, 24, 0);
  lv_obj_set_style_bg_color(button, lv_color_hex(color), 0);
  lv_obj_set_style_shadow_width(button, 0, 0);
  lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* text = lv_label_create(button);
  lv_label_set_text(text, label);
  lv_obj_set_style_text_font(text, UI_FONT_16, 0);
  lv_obj_set_style_text_color(text, lv_color_hex(kText), 0);
  lv_obj_center(text);
  return button;
}

void clearPointers() {
  currentOwner = Owner::None;
  root = nullptr;
  titleLabel = nullptr;
  statusLabel = nullptr;
  detailLabel = nullptr;
  powerButton = nullptr;
  actionRing = nullptr;
  actionButton = nullptr;
  actionLabel = nullptr;
  unknownControls = nullptr;
  unknownStart = nullptr;
  unknownStop = nullptr;
  powerFeature = false;
  unknownFeature = false;
  callbacks = {};
}

bool destroyIfIdle() {
  if (root == nullptr) {
    return true;
  }
  if (lv_scr_act() == root) {
    return false;
  }
  lv_obj_del(root);
  clearPointers();
  return true;
}

void build(const Options& options, const Callbacks& nextCallbacks) {
  callbacks = nextCallbacks;
  powerFeature = options.enablePower;
  unknownFeature = options.enableUnknownControls;

  root = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(root, lv_color_hex(kBg), 0);
  lv_obj_set_style_text_color(root, lv_color_hex(kText), 0);
  lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

  studio_ui::RoundPageHeaderOptions headerOptions;
  headerOptions.onBack = onBack;
  headerOptions.panelColor = kPanel;
  headerOptions.textColor = kText;
  if (powerFeature) {
    headerOptions.actionSymbol = LV_SYMBOL_POWER;
    headerOptions.onAction = onPower;
  }
  const studio_ui::RoundPageHeader header =
      studio_ui::createRoundPageHeader(root, headerOptions);
  titleLabel = header.title;
  powerButton = header.action;

  statusLabel = lv_label_create(root);
  lv_obj_set_style_text_font(statusLabel, UI_FONT_16, 0);
  lv_obj_set_style_text_color(statusLabel, lv_color_hex(kText), 0);
  lv_obj_align(statusLabel, LV_ALIGN_TOP_MID, 0, 62);

  detailLabel = lv_label_create(root);
  lv_obj_set_width(detailLabel, powerFeature ? 190 : 184);
  lv_obj_set_style_text_align(detailLabel, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(detailLabel, UI_FONT_14, 0);
  lv_obj_set_style_text_color(detailLabel, lv_color_hex(kMuted), 0);
  lv_obj_align(detailLabel, LV_ALIGN_TOP_MID, 0, 78);

  actionRing = lv_obj_create(root);
  lv_obj_set_size(actionRing, 136, 136);
  lv_obj_align(actionRing, LV_ALIGN_BOTTOM_MID, 0, -4);
  lv_obj_set_style_radius(actionRing, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(actionRing, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(actionRing, 3, 0);
  lv_obj_set_style_pad_all(actionRing, 8, 0);
  lv_obj_clear_flag(actionRing, LV_OBJ_FLAG_SCROLLABLE);

  actionButton = lv_btn_create(actionRing);
  lv_obj_set_size(actionButton, 118, 118);
  lv_obj_center(actionButton);
  lv_obj_set_style_radius(actionButton, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_shadow_width(actionButton, 0, 0);
  lv_obj_add_event_cb(actionButton, onAction, LV_EVENT_CLICKED, nullptr);
  actionLabel = lv_label_create(actionButton);
  lv_obj_set_style_text_align(actionLabel, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(actionLabel, UI_FONT_16, 0);
  lv_obj_set_style_text_color(actionLabel, lv_color_hex(kText), 0);
  lv_obj_center(actionLabel);

  if (unknownFeature) {
    unknownControls = lv_obj_create(root);
    lv_obj_set_size(unknownControls, 180, 82);
    lv_obj_align(unknownControls, LV_ALIGN_BOTTOM_MID, 0, -34);
    lv_obj_set_style_bg_opa(unknownControls, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(unknownControls, 0, 0);
    lv_obj_set_style_pad_all(unknownControls, 0, 0);
    lv_obj_clear_flag(unknownControls, LV_OBJ_FLAG_SCROLLABLE);
    unknownStart =
        createUnknownButton(unknownControls, "START", kReady, onUnknownStart);
    lv_obj_align(unknownStart, LV_ALIGN_LEFT_MID, 4, 0);
    unknownStop =
        createUnknownButton(unknownControls, "STOP", kAccent, onUnknownStop);
    lv_obj_align(unknownStop, LV_ALIGN_RIGHT_MID, -4, 0);
    lv_obj_add_flag(unknownControls, LV_OBJ_FLAG_HIDDEN);
  }
}

}  // namespace

void acquire(Owner owner, const Options& options, const Callbacks& nextCallbacks) {
  if (ownedBy(owner)) {
    callbacks = nextCallbacks;
    return;
  }
  if (!destroyIfIdle()) {
    return;
  }
  build(options, nextCallbacks);
  currentOwner = owner;
}

void release(Owner owner) {
  if (!ownedBy(owner)) {
    return;
  }
  destroyIfIdle();
}

void destroyIdle() { destroyIfIdle(); }

bool ownedBy(Owner owner) {
  return root != nullptr && currentOwner == owner;
}

lv_obj_t* screen() { return root; }

void apply(const View& view) {
  if (root == nullptr) {
    return;
  }
  lv_label_set_text(titleLabel, view.title != nullptr ? view.title : "");
  lv_label_set_text(statusLabel, view.status != nullptr ? view.status : "");
  lv_label_set_text(detailLabel, view.detail != nullptr ? view.detail : "");
  setPowerEnabled(view.powerEnabled);
  if (view.showUnknownControls && unknownFeature) {
    showUnknownControls(true);
    return;
  }
  setAction(view.actionLabel != nullptr ? view.actionLabel : "WAITING",
            view.actionColor, view.actionEnabled);
}

}  // namespace recorder_shell
