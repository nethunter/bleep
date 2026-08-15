#include "ui/rename_prompt.h"

#include <cstdint>
#include <cstring>
#include <lvgl.h>

#include "core/device_types.h"
#include "fonts/ui_fonts.h"

namespace studio_ui::rename_prompt {
namespace {

constexpr uint32_t kBg = 0x05070A;
constexpr uint32_t kPanel = 0x12161D;
constexpr uint32_t kAccent = 0x35C7F2;
constexpr uint32_t kText = 0xF3F4F6;
constexpr uint32_t kMuted = 0x8A94A6;
constexpr uint8_t kPageCount = 4;

lv_obj_t* overlay = nullptr;
lv_obj_t* textArea = nullptr;
lv_obj_t* keypad = nullptr;
lv_obj_t* pageLabel = nullptr;
lv_obj_t* caseLabel = nullptr;
uint8_t page = 0;
bool upperCase = true;
Done doneCallback = nullptr;
Cancel cancelCallback = nullptr;

const char* upper0[] = {"A", "B", "C", "\n", "D", "E", "F", "\n",
                        "G", "H", "I", ""};
const char* upper1[] = {"J", "K", "L", "\n", "M", "N", "O", "\n",
                        "P", "Q", "R", ""};
const char* upper2[] = {"S", "T", "U", "\n", "V", "W", "X", "\n",
                        "Y", "Z", "", ""};
const char* lower0[] = {"a", "b", "c", "\n", "d", "e", "f", "\n",
                        "g", "h", "i", ""};
const char* lower1[] = {"j", "k", "l", "\n", "m", "n", "o", "\n",
                        "p", "q", "r", ""};
const char* lower2[] = {"s", "t", "u", "\n", "v", "w", "x", "\n",
                        "y", "z", "", ""};
const char* symbols[] = {"0", "1", "2", "3", "\n", "4", "5", "6",
                         "7", "\n", "8", "9", "-", "_", ""};

lv_obj_t* button(lv_obj_t* parent, const char* label, lv_event_cb_t callback,
                 uint32_t color = kPanel) {
  lv_obj_t* object = lv_btn_create(parent);
  lv_obj_set_style_bg_color(object, lv_color_hex(color), 0);
  lv_obj_set_style_radius(object, 8, 0);
  lv_obj_set_style_shadow_width(object, 0, 0);
  lv_obj_t* child = lv_label_create(object);
  lv_label_set_text(child, label);
  lv_obj_set_style_text_font(child, UI_FONT_14, 0);
  lv_obj_set_style_text_color(child, lv_color_hex(kText), 0);
  lv_obj_center(child);
  lv_obj_add_event_cb(object, callback, LV_EVENT_CLICKED, nullptr);
  return object;
}

const char** map() {
  if (page == 0) return upperCase ? upper0 : lower0;
  if (page == 1) return upperCase ? upper1 : lower1;
  if (page == 2) return upperCase ? upper2 : lower2;
  return symbols;
}

void refresh() {
  static const char* names[] = {"A-I", "J-R", "S-Z", "0-9"};
  lv_btnmatrix_set_map(keypad, map());
  lv_label_set_text(pageLabel, names[page]);
  lv_label_set_text(caseLabel, upperCase ? "Aa" : "aA");
}

void step(int delta) {
  int next = static_cast<int>(page) + delta;
  if (next < 0) next += kPageCount;
  if (next >= kPageCount) next -= kPageCount;
  page = static_cast<uint8_t>(next);
  refresh();
}

void onPrevious(lv_event_t*) { step(-1); }
void onNext(lv_event_t*) { step(1); }

void onGesture(lv_event_t*) {
  lv_indev_t* input = lv_indev_get_act();
  if (input == nullptr) {
    return;
  }
  const lv_dir_t direction = lv_indev_get_gesture_dir(input);
  if (direction == LV_DIR_LEFT) {
    step(1);
  } else if (direction == LV_DIR_RIGHT) {
    step(-1);
  }
}

void onCharacter(lv_event_t* event) {
  lv_obj_t* object = lv_event_get_target(event);
  const uint16_t selected = lv_btnmatrix_get_selected_btn(object);
  if (selected == LV_BTNMATRIX_BTN_NONE) {
    return;
  }
  const char* value = lv_btnmatrix_get_btn_text(object, selected);
  if (value != nullptr && value[0] != '\0') {
    lv_textarea_add_text(textArea, value);
  }
}

void onBackspace(lv_event_t*) { lv_textarea_del_char(textArea); }
void onSpace(lv_event_t*) { lv_textarea_add_char(textArea, ' '); }

void onCase(lv_event_t*) {
  upperCase = !upperCase;
  refresh();
}

void onSave(lv_event_t*) {
  char name[studio::kDeviceNameCapacity] = "";
  if (textArea != nullptr) {
    std::strncpy(name, lv_textarea_get_text(textArea), sizeof(name) - 1);
  }
  const Done callback = doneCallback;
  close();
  if (callback != nullptr) {
    callback(name);
  }
}

void onCancel(lv_event_t*) {
  const Cancel callback = cancelCallback;
  close();
  if (callback != nullptr) {
    callback();
  }
}

void build() {
  overlay = lv_obj_create(lv_layer_top());
  lv_obj_set_size(overlay, 236, 236);
  lv_obj_center(overlay);
  lv_obj_set_style_radius(overlay, 118, 0);
  lv_obj_set_style_bg_color(overlay, lv_color_hex(kBg), 0);
  lv_obj_set_style_border_color(overlay, lv_color_hex(kAccent), 0);
  lv_obj_set_style_pad_all(overlay, 0, 0);
  lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* cancel = button(overlay, LV_SYMBOL_CLOSE, onCancel);
  lv_obj_set_size(cancel, 30, 30);
  lv_obj_align(cancel, LV_ALIGN_TOP_LEFT, 34, 24);
  textArea = lv_textarea_create(overlay);
  lv_obj_set_size(textArea, 100, 30);
  lv_obj_align(textArea, LV_ALIGN_TOP_MID, 0, 18);
  lv_textarea_set_max_length(textArea, studio::kDeviceNameCapacity - 1);
  lv_textarea_set_one_line(textArea, true);
  lv_textarea_set_cursor_click_pos(textArea, true);
  lv_obj_t* save = button(overlay, LV_SYMBOL_OK, onSave, kAccent);
  lv_obj_set_size(save, 30, 30);
  lv_obj_align(save, LV_ALIGN_TOP_RIGHT, -34, 24);
  lv_obj_t* previous = button(overlay, LV_SYMBOL_LEFT, onPrevious);
  lv_obj_set_size(previous, 32, 28);
  lv_obj_align(previous, LV_ALIGN_TOP_MID, -55, 53);
  pageLabel = lv_label_create(overlay);
  lv_obj_set_style_text_font(pageLabel, UI_FONT_14, 0);
  lv_obj_set_style_text_color(pageLabel, lv_color_hex(kMuted), 0);
  lv_obj_align(pageLabel, LV_ALIGN_TOP_MID, 0, 59);
  lv_obj_t* next = button(overlay, LV_SYMBOL_RIGHT, onNext);
  lv_obj_set_size(next, 32, 28);
  lv_obj_align(next, LV_ALIGN_TOP_MID, 55, 53);
  keypad = lv_btnmatrix_create(overlay);
  lv_obj_set_size(keypad, 150, 84);
  lv_obj_align(keypad, LV_ALIGN_TOP_MID, 0, 83);
  lv_obj_set_style_bg_color(keypad, lv_color_hex(kBg), 0);
  lv_obj_set_style_border_width(keypad, 0, 0);
  lv_obj_set_style_pad_all(keypad, 2, 0);
  lv_obj_set_style_pad_gap(keypad, 3, 0);
  lv_obj_set_style_bg_color(keypad, lv_color_hex(kPanel), LV_PART_ITEMS);
  lv_obj_set_style_radius(keypad, 7, LV_PART_ITEMS);
  lv_obj_set_style_text_font(keypad, UI_FONT_16, LV_PART_ITEMS);
  lv_obj_add_event_cb(keypad, onCharacter, LV_EVENT_VALUE_CHANGED, nullptr);
  lv_obj_add_event_cb(keypad, onGesture, LV_EVENT_GESTURE, nullptr);
  lv_obj_t* backspace = button(overlay, LV_SYMBOL_BACKSPACE, onBackspace);
  lv_obj_set_size(backspace, 40, 26);
  lv_obj_align(backspace, LV_ALIGN_TOP_MID, -51, 172);
  lv_obj_t* space = button(overlay, "Space", onSpace);
  lv_obj_set_size(space, 56, 26);
  lv_obj_align(space, LV_ALIGN_TOP_MID, 0, 172);
  lv_obj_t* letterCase = button(overlay, "Aa", onCase);
  lv_obj_set_size(letterCase, 40, 26);
  lv_obj_align(letterCase, LV_ALIGN_TOP_MID, 51, 172);
  caseLabel = lv_obj_get_child(letterCase, 0);
}

}  // namespace

void show(const char* initial, Done done, Cancel cancel) {
  if (overlay == nullptr) build();
  doneCallback = done;
  cancelCallback = cancel;
  lv_textarea_set_text(textArea, initial != nullptr ? initial : "");
  lv_textarea_set_cursor_pos(textArea, LV_TEXTAREA_CURSOR_LAST);
  page = 0;
  upperCase = true;
  refresh();
}

void close() {
  if (overlay != nullptr) {
    lv_obj_del(overlay);
  }
  overlay = textArea = keypad = pageLabel = caseLabel = nullptr;
  doneCallback = nullptr;
  cancelCallback = nullptr;
}

void cancel() { onCancel(nullptr); }

bool active() { return overlay != nullptr; }

#ifdef UI_SIMULATOR
void simSetText(const char* name) {
  if (textArea != nullptr) {
    lv_textarea_set_text(textArea, name != nullptr ? name : "");
  }
}
void simSave() { onSave(nullptr); }
#endif

}  // namespace studio_ui::rename_prompt
