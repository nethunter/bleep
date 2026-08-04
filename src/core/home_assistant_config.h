#pragma once

#include <cstddef>
#include <cstdint>

#include "core/config_store.h"

namespace studio {

constexpr size_t kWifiSsidCapacity = 33;
constexpr size_t kWifiPasswordCapacity = 65;
constexpr size_t kHomeAssistantUrlCapacity = 96;
constexpr size_t kHomeAssistantTokenCapacity = 256;

struct HomeAssistantConfig {
  bool configured = false;
  char wifiSsid[kWifiSsidCapacity] = "";
  char wifiPassword[kWifiPasswordCapacity] = "";
  char baseUrl[kHomeAssistantUrlCapacity] = "";
  char token[kHomeAssistantTokenCapacity] = "";
};

class HomeAssistantConfigStore {
 public:
  explicit HomeAssistantConfigStore(IConfigBackend& backend) : backend_(backend) {}

  ConfigLoadStatus load(HomeAssistantConfig& config);
  bool save(const HomeAssistantConfig& config);
  bool clear();

 private:
  IConfigBackend& backend_;
};

bool validLocalHomeAssistantUrl(const char* url);

}  // namespace studio
