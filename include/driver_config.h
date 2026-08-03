#pragma once

// PlatformIO supplies these today. The names intentionally match ESP-IDF
// Kconfig symbols so a future Arduino-as-component build can replace defaults
// without changing driver code.
#ifndef CONFIG_DRIVER_SHARK_NANO_II
#define CONFIG_DRIVER_SHARK_NANO_II 1
#endif

#ifndef CONFIG_MAX_DEVICE_INSTANCES
#define CONFIG_MAX_DEVICE_INSTANCES 8
#endif

#ifndef CONFIG_DEVICE_COMMAND_QUEUE_SIZE
#define CONFIG_DEVICE_COMMAND_QUEUE_SIZE 12
#endif

