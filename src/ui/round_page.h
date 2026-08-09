#pragma once

#include <lvgl.h>

#include <cstdint>

namespace studio_ui {

constexpr lv_coord_t kRoundHeaderControlX = 40;
constexpr lv_coord_t kRoundHeaderControlY = 24;
constexpr lv_coord_t kRoundHeaderTitleY = 28;
constexpr lv_coord_t kRoundPageContentY = 58;
constexpr lv_coord_t kRoundHeaderTitleWidth = 92;

struct RoundPageHeaderOptions {
  const char* title = "";
  const char* backSymbol = LV_SYMBOL_LEFT;
  lv_event_cb_t onBack = nullptr;
  const char* alternateBackSymbol = nullptr;
  lv_event_cb_t onAlternateBack = nullptr;
  const char* actionSymbol = nullptr;
  lv_event_cb_t onAction = nullptr;
  const lv_font_t* titleFont = nullptr;
  lv_coord_t titleWidth = kRoundHeaderTitleWidth;
  uint32_t panelColor = 0x12161D;
  uint32_t textColor = 0xF3F4F6;
};

struct RoundPageHeader {
  lv_obj_t* title = nullptr;
  lv_obj_t* back = nullptr;
  lv_obj_t* alternateBack = nullptr;
  lv_obj_t* action = nullptr;
};

lv_obj_t* createRoundPageHeaderButton(lv_obj_t* parent, const char* symbol,
                                      lv_event_cb_t callback,
                                      uint32_t panelColor = 0x12161D,
                                      uint32_t textColor = 0xF3F4F6,
                                      bool right = false);

RoundPageHeader createRoundPageHeader(
    lv_obj_t* parent, const RoundPageHeaderOptions& options);

void setRoundPageTitle(lv_obj_t* title, const char* text);

}  // namespace studio_ui
