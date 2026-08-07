#include "devices/zhiyun_x100/state.h"

#include "devices/zhiyun_x100/protocol.h"

namespace zhiyun_x100 {

bool validCctCommand(int kelvin, int brightness, int tintPermille) {
  return kelvin >= kMinKelvin && kelvin <= kMaxKelvin && brightness >= 0 &&
         brightness <= 100 && tintPermille == 0;
}

}  // namespace zhiyun_x100
