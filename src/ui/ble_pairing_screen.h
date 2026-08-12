#pragma once

#include <cstddef>
#include <cstdint>

#include <lvgl.h>

#include "core/device_types.h"

namespace studio_ui {

class BlePairingScreen {
 public:
  void create(lv_event_cb_t onBack, lv_event_cb_t onRetry);
  void destroy();
  lv_obj_t* screen() const { return screen_; }
  void setTitle(const char* title);
  void setStatus(const char* status, const char* detail, bool busy,
                 bool retryVisible, const char* retryLabel = "Retry");
  void setCandidates(const studio::OnboardingCandidate* candidates,
                     size_t count, void (*onSelect)(uint32_t));
#ifdef UI_SIMULATOR
  size_t simCandidateRowCount() const;
  lv_obj_t* simCandidateRow(size_t index) const;
  void simScrollCandidates(int16_t delta);
  int32_t simCandidateScrollY() const;
  void simClickCandidate(size_t index);
#endif

 private:
  static void onCandidateClicked(lv_event_t* event);
  lv_obj_t* screen_ = nullptr;
  lv_obj_t* title_ = nullptr;
  lv_obj_t* spinner_ = nullptr;
  lv_obj_t* mark_ = nullptr;
  lv_obj_t* status_ = nullptr;
  lv_obj_t* detail_ = nullptr;
  lv_obj_t* retry_ = nullptr;
  lv_obj_t* retryLabel_ = nullptr;
  lv_obj_t* candidateList_ = nullptr;
  lv_obj_t* candidateRows_[4] = {};
  lv_obj_t* candidateLabels_[4] = {};
  size_t candidateCount_ = 0;
  void (*onSelect_)(uint32_t) = nullptr;
};

}  // namespace studio_ui
