#include "devices/home_assistant/protocol.h"

#include <cstdio>
#include <cstring>

namespace home_assistant {
namespace {

bool begins(const char* value, const char* prefix) {
  return value != nullptr &&
         std::strncmp(value, prefix, std::strlen(prefix)) == 0;
}

char lowerAscii(char value) {
  if (value >= 'A' && value <= 'Z') return value + ('a' - 'A');
  return value;
}

bool containsIgnoringCase(const char* value, const char* query) {
  if (query == nullptr || query[0] == '\0') return true;
  if (value == nullptr) return false;
  for (const char* candidate = value; *candidate != '\0'; ++candidate) {
    const char* left = candidate;
    const char* right = query;
    while (*left != '\0' && *right != '\0' &&
           lowerAscii(*left) == lowerAscii(*right)) {
      ++left;
      ++right;
    }
    if (*right == '\0') return true;
  }
  return false;
}

}  // namespace

studio::HomeAssistantDomain domainFromEntityId(const char* entityId) {
  if (begins(entityId, "light.")) return studio::HomeAssistantDomain::Light;
  if (begins(entityId, "switch.")) return studio::HomeAssistantDomain::Switch;
  if (begins(entityId, "input_boolean.")) {
    return studio::HomeAssistantDomain::InputBoolean;
  }
  if (begins(entityId, "button.")) return studio::HomeAssistantDomain::Button;
  if (begins(entityId, "scene.")) return studio::HomeAssistantDomain::Scene;
  if (begins(entityId, "script.")) return studio::HomeAssistantDomain::Script;
  return studio::HomeAssistantDomain::None;
}

const char* domainName(studio::HomeAssistantDomain domain) {
  switch (domain) {
    case studio::HomeAssistantDomain::Light: return "light";
    case studio::HomeAssistantDomain::Switch: return "switch";
    case studio::HomeAssistantDomain::InputBoolean: return "input_boolean";
    case studio::HomeAssistantDomain::Button: return "button";
    case studio::HomeAssistantDomain::Scene: return "scene";
    case studio::HomeAssistantDomain::Script: return "script";
    case studio::HomeAssistantDomain::None: return "";
  }
  return "";
}

bool supportedEntityId(const char* entityId) {
  const studio::HomeAssistantDomain domain = domainFromEntityId(entityId);
  if (domain == studio::HomeAssistantDomain::None) return false;
  const char* dot = std::strchr(entityId, '.');
  return dot != nullptr && dot[1] != '\0' && std::strchr(dot + 1, '.') == nullptr;
}

bool matchesEntitySearch(const char* entityId, const char* friendlyName,
                         const char* query) {
  return containsIgnoringCase(entityId, query) ||
         containsIgnoringCase(friendlyName, query);
}

const char* serviceFor(studio::HomeAssistantDomain domain,
                       studio::CommandType command) {
  if (command == studio::CommandType::TurnOn &&
      (domain == studio::HomeAssistantDomain::Light ||
       domain == studio::HomeAssistantDomain::Switch ||
       domain == studio::HomeAssistantDomain::InputBoolean)) {
    return "turn_on";
  }
  if (command == studio::CommandType::TurnOff &&
      (domain == studio::HomeAssistantDomain::Light ||
       domain == studio::HomeAssistantDomain::Switch ||
       domain == studio::HomeAssistantDomain::InputBoolean)) {
    return "turn_off";
  }
  if (command == studio::CommandType::Press &&
      domain == studio::HomeAssistantDomain::Button) {
    return "press";
  }
  if (command == studio::CommandType::Activate &&
      (domain == studio::HomeAssistantDomain::Scene ||
       domain == studio::HomeAssistantDomain::Script)) {
    return "turn_on";
  }
  return nullptr;
}

bool commandSupported(studio::HomeAssistantDomain domain,
                      studio::CommandType command) {
  return serviceFor(domain, command) != nullptr;
}

bool buildAuth(char* output, size_t capacity, const char* token) {
  if (output == nullptr || capacity == 0 || token == nullptr || token[0] == '\0' ||
      std::strchr(token, '"') != nullptr || std::strchr(token, '\\') != nullptr) {
    return false;
  }
  const int written = std::snprintf(
      output, capacity, "{\"type\":\"auth\",\"access_token\":\"%s\"}", token);
  return written > 0 && static_cast<size_t>(written) < capacity;
}

bool buildServiceCall(char* output, size_t capacity, uint32_t id,
                      studio::HomeAssistantDomain domain,
                      studio::CommandType command, const char* entityId) {
  const char* service = serviceFor(domain, command);
  const char* domainText = domainName(domain);
  if (output == nullptr || capacity == 0 || service == nullptr ||
      !supportedEntityId(entityId)) {
    return false;
  }
  const int written = std::snprintf(
      output, capacity,
      "{\"id\":%lu,\"type\":\"call_service\",\"domain\":\"%s\","
      "\"service\":\"%s\",\"target\":{\"entity_id\":\"%s\"}}",
      static_cast<unsigned long>(id), domainText, service, entityId);
  return written > 0 && static_cast<size_t>(written) < capacity;
}

}  // namespace home_assistant
