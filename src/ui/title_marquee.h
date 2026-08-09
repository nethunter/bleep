#pragma once

#include <lvgl.h>

namespace studio_ui {

// Keep a screen title on one line and reveal overflow with a horizontal
// marquee. Short titles remain centered and stationary.
inline void configureTitleMarquee(lv_obj_t* label, lv_coord_t width,
                                  const lv_font_t* font) {
  lv_obj_set_size(label, width, lv_font_get_line_height(font));
  lv_obj_set_style_text_font(label, font, 0);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(label, LV_LABEL_LONG_SCROLL_CIRCULAR);
}

}  // namespace studio_ui
