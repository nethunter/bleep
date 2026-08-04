#pragma once

#include <cstddef>
#include <cstdint>

#include "core/device_types.h"

namespace home_assistant {

studio::HomeAssistantDomain domainFromEntityId(const char* entityId);
const char* domainName(studio::HomeAssistantDomain domain);
bool supportedEntityId(const char* entityId);
const char* serviceFor(studio::HomeAssistantDomain domain,
                       studio::CommandType command);
bool commandSupported(studio::HomeAssistantDomain domain,
                      studio::CommandType command);

bool buildAuth(char* output, size_t capacity, const char* token);
bool buildServiceCall(char* output, size_t capacity, uint32_t id,
                      studio::HomeAssistantDomain domain,
                      studio::CommandType command, const char* entityId);

}  // namespace home_assistant
