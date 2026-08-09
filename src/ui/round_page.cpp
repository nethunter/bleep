#include "ui/round_page.h"

#include "fonts/ui_fonts.h"
#include "ui/title_marquee.h"

namespace studio_ui {
lv_obj_t* createRoundPageHeaderButton(lv_obj_t* parent, const char* symbol,
                                      lv_event_cb_t callback,
                                      uint32_t panelColor, uint32_t textColor,
                                      bool right) {
  lv_obj_t* button = lv_btn_create(parent);
  lv_obj_set_size(button, 34, 30);
  lv_obj_set_style_bg_color(button, lv_color_hex(panelColor), 0);
  if (callback != nullptr) {
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, nullptr);
  }
  lv_obj_t* label = lv_label_create(button);
  lv_label_set_text(label, symbol);
  lv_obj_set_style_text_font(label, UI_FONT_16, 0);
  lv_obj_set_style_text_color(label, lv_color_hex(textColor), 0);
  lv_obj_center(label);
  lv_obj_align(button, right ? LV_ALIGN_TOP_RIGHT : LV_ALIGN_TOP_LEFT,
               right ? -kRoundHeaderControlX : kRoundHeaderControlX,
               kRoundHeaderControlY);
  return button;
}

RoundPageHeader createRoundPageHeader(
    lv_obj_t* parent, const RoundPageHeaderOptions& options) {
  RoundPageHeader header;
  if (options.onBack != nullptr) {
    header.back = createRoundPageHeaderButton(
        parent, options.backSymbol, options.onBack, options.panelColor,
        options.textColor);
  }
  if (options.alternateBackSymbol != nullptr) {
    header.alternateBack = createRoundPageHeaderButton(
        parent, options.alternateBackSymbol, options.onAlternateBack,
        options.panelColor, options.textColor);
  }
  if (options.actionSymbol != nullptr) {
    header.action = createRoundPageHeaderButton(
        parent, options.actionSymbol, options.onAction, options.panelColor,
        options.textColor, true);
  }

  header.title = lv_label_create(parent);
  configureTitleMarquee(header.title, kRoundHeaderTitleWidth,
                         options.titleFont != nullptr ? options.titleFont
                                                      : UI_FONT_16);
  lv_label_set_text(header.title,
                    options.title != nullptr ? options.title : "");
  lv_obj_align(header.title, LV_ALIGN_TOP_MID, 0, kRoundHeaderTitleY);
  return header;
}

lv_obj_t* createRoundPageMenuBody(lv_obj_t* parent, lv_coord_t rowGap) {
  lv_obj_t* body = lv_obj_create(parent);
  lv_obj_set_size(body, kRoundMenuWidth, kRoundMenuHeight);
  lv_obj_align(body, LV_ALIGN_TOP_MID, 0, kRoundPageContentY);
  lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(body, 0, 0);
  lv_obj_set_style_pad_all(body, 0, 0);
  lv_obj_set_style_pad_row(body, rowGap, 0);
  lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_scrollbar_mode(body, LV_SCROLLBAR_MODE_OFF);
  lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLLABLE);
  return body;
}

}  // namespace studio_ui
