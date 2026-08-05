#pragma once

#include <lvgl.h>

namespace studio_ui {

class BlePairingScreen {
 public:
  void create(lv_event_cb_t onBack, lv_event_cb_t onRetry);
  void destroy();
  lv_obj_t* screen() const { return screen_; }
  void setTitle(const char* title);
  void setStatus(const char* status, const char* detail, bool busy,
                 bool retryVisible, const char* retryLabel = "Retry");

 private:
  lv_obj_t* screen_ = nullptr;
  lv_obj_t* title_ = nullptr;
  lv_obj_t* spinner_ = nullptr;
  lv_obj_t* mark_ = nullptr;
  lv_obj_t* status_ = nullptr;
  lv_obj_t* detail_ = nullptr;
  lv_obj_t* retry_ = nullptr;
  lv_obj_t* retryLabel_ = nullptr;
};

}  // namespace studio_ui
