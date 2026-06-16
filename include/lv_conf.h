#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_DEPTH 16
// The GC9A01 panel is driven by pushing the LVGL buffer as swap565_t
// (DISPLAY_FLUSH_SWAP565=1 in main.cpp), which DMAs the bytes straight to the
// panel in its big-endian wire order. That requires LVGL to store pixels
// byte-swapped, i.e. LV_COLOR_16_SWAP=1. Pairing swap565 with SWAP=0 swaps
// every pixel's bytes and corrupts the palette (and AA edges).
#ifndef LV_COLOR_16_SWAP
#define LV_COLOR_16_SWAP 1
#endif
#define LV_COLOR_MIX_ROUND_OFS 128

#define LV_MEM_CUSTOM 0
#define LV_MEM_SIZE (32U * 1024U)

#define LV_TICK_CUSTOM 0
#define LV_DPI_DEF 130

#define LV_USE_LOG 0
#define LV_USE_ASSERT_NULL 1
#define LV_USE_ASSERT_MALLOC 1
#define LV_USE_ASSERT_STYLE 0
#define LV_USE_ASSERT_MEM_INTEGRITY 0
#define LV_USE_ASSERT_OBJ 0

#define LV_USE_PERF_MONITOR 0
#define LV_USE_MEM_MONITOR 0

// UI font family is chosen at compile time. Default is Montserrat (bundled with
// LVGL); define UI_FONT_ROBOTO to use the embedded Roboto faces in src/fonts/.
// Only the three sizes the UI uses are enabled, and the unused family is gated
// off so it costs no flash. See src/fonts/ui_fonts.h.
#ifdef UI_FONT_ROBOTO
#define LV_FONT_MONTSERRAT_14 0
#define LV_FONT_MONTSERRAT_16 0
#define LV_FONT_MONTSERRAT_20 0
#define LV_FONT_ROBOTO_14 1
#define LV_FONT_ROBOTO_16 1
#define LV_FONT_ROBOTO_20 1
#define LV_FONT_CUSTOM_DECLARE \
  LV_FONT_DECLARE(lv_font_roboto_14) \
  LV_FONT_DECLARE(lv_font_roboto_16) \
  LV_FONT_DECLARE(lv_font_roboto_20)
#define LV_FONT_DEFAULT &lv_font_roboto_16
#else
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_ROBOTO_14 0
#define LV_FONT_ROBOTO_16 0
#define LV_FONT_ROBOTO_20 0
#define LV_FONT_DEFAULT &lv_font_montserrat_16
#endif
#define LV_FONT_MONTSERRAT_12 0
#define LV_FONT_MONTSERRAT_18 0
#define LV_FONT_MONTSERRAT_24 0
#define LV_FONT_ANTIALIAS 1

#define LV_USE_ARC 1
#define LV_USE_BAR 1
#define LV_USE_BTN 1
#define LV_USE_BTNMATRIX 1
#define LV_USE_CANVAS 0
#define LV_USE_CHECKBOX 0
#define LV_USE_DROPDOWN 0
#define LV_USE_IMG 0
#define LV_USE_LABEL 1
#define LV_USE_LIST 1
#define LV_USE_LINE 0
#define LV_USE_OBJ 1
#define LV_USE_ROLLER 0
#define LV_USE_SLIDER 1
#define LV_USE_SWITCH 1
#define LV_USE_TABLE 0
#define LV_USE_TEXTAREA 1

#define LV_USE_ANIMATION 1
#define LV_USE_SHADOW 1
#define LV_USE_BLEND_MODES 0

#define LV_THEME_DEFAULT_DARK 1
// Grow zooms buttons (and their text) via transform on press/focus, which
// re-samples anti-aliased glyphs with nearest-neighbor and makes them look
// jagged. Keep text crisp by disabling it.
#define LV_THEME_DEFAULT_GROW 0
#define LV_THEME_DEFAULT_TRANSITION_TIME 120

#define LV_USE_CALENDAR 0
#define LV_USE_CHART 0
#define LV_USE_COLORWHEEL 0
#define LV_USE_ANIMIMG 0
#define LV_USE_IMGBTN 0
#define LV_USE_KEYBOARD 0
#define LV_USE_LED 0
#define LV_USE_MENU 0
#define LV_USE_METER 0
#define LV_USE_MSGBOX 1
#define LV_USE_SPAN 0
#define LV_USE_SPINBOX 1
#define LV_USE_SPINNER 0
#define LV_USE_TABVIEW 0
#define LV_USE_TILEVIEW 0
#define LV_USE_WIN 0

#define LV_USE_FLEX 1
#define LV_USE_GRID 0
#define LV_USE_BMP 0
#define LV_USE_FFMPEG 0
#define LV_USE_FREETYPE 0
#define LV_USE_FS_FATFS 0
#define LV_USE_FS_LITTLEFS 0
#define LV_USE_FS_POSIX 0
#define LV_USE_FS_STDIO 0
#define LV_USE_GIF 0
#define LV_USE_PNG 0
#define LV_USE_QRCODE 0
#define LV_USE_RLOTTIE 0
#define LV_USE_SJPG 0
#define LV_USE_TINY_TTF 0
#define LV_USE_FRAGMENT 0
#define LV_USE_GRIDNAV 0
#define LV_USE_IME_PINYIN 0
#define LV_USE_IMGFONT 0
#define LV_USE_MONKEY 0
#define LV_USE_MSG 0
#define LV_USE_SNAPSHOT 0

#define LV_USE_GPU_ARM2D 0
#define LV_USE_GPU_STM32_DMA2D 0
#define LV_USE_GPU_SWM341_DMA2D 0
#define LV_USE_GPU_NXP_PXP 0
#define LV_USE_GPU_NXP_VG_LITE 0
#define LV_USE_GPU_SDL 0

#endif
