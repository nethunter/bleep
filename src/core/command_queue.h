#pragma once

#include "core/device_types.h"

namespace studio {

template <typename T, size_t Capacity>
class RingQueue {
 public:
  bool push(const T& value) {
    if (count_ == Capacity) {
      return false;
    }
    values_[tail_] = value;
    tail_ = (tail_ + 1) % Capacity;
    ++count_;
    return true;
  }

  bool pop(T& value) {
    if (count_ == 0) {
      return false;
    }
    value = values_[head_];
    head_ = (head_ + 1) % Capacity;
    --count_;
    return true;
  }

  size_t size() const { return count_; }
  bool empty() const { return count_ == 0; }

 private:
  T values_[Capacity] = {};
  size_t head_ = 0;
  size_t tail_ = 0;
  size_t count_ = 0;
};

using DeviceCommandQueue =
    RingQueue<DeviceCommand, CONFIG_DEVICE_COMMAND_QUEUE_SIZE>;
using DeviceResultQueue =
    RingQueue<CommandResult, CONFIG_DEVICE_COMMAND_QUEUE_SIZE>;

}  // namespace studio

