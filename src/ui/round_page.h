#pragma once

#include <lvgl.h>

#include <cstdint>

namespace studio_ui {

constexpr lv_coord_t kRoundHeaderControlX = 40;
constexpr lv_coord_t kRoundHeaderControlY = 24;
constexpr lv_coord_t kRoundHeaderTitleY = 28;
constexpr lv_coord_t kRoundPageContentY = 58;
constexpr lv_coord_t kRoundHeaderTitleWidth = 92;
constexpr lv_coord_t kRoundMenuWidth = 168;
constexpr lv_coord_t kRoundMenuHeight = 158;

struct RoundPageHeaderOptions {
  const char* title = "";
  const char* backSymbol = LV_SYMBOL_LEFT;
  lv_event_cb_t onBack = nullptr;
  const char* alternateBackSymbol = nullptr;
  lv_event_cb_t onAlternateBack = nullptr;
  const char* actionSymbol = nullptr;
  lv_event_cb_t onAction = nullptr;
  const lv_font_t* titleFont = nullptr;
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

lv_obj_t* createRoundPageMenuBody(lv_obj_t* parent,
                                  lv_coord_t rowGap = 4);

}  // namespace studio_ui
