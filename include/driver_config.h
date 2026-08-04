#pragma once

// PlatformIO supplies these today. The names intentionally match ESP-IDF
// Kconfig symbols so a future Arduino-as-component build can replace defaults
// without changing driver code.
#ifndef CONFIG_DRIVER_SHARK_NANO_II
#define CONFIG_DRIVER_SHARK_NANO_II 1
#endif

#ifndef CONFIG_DRIVER_CANON_BLE
#define CONFIG_DRIVER_CANON_BLE 0
#endif

#ifndef CONFIG_DRIVER_CANON_TRIGGER
#define CONFIG_DRIVER_CANON_TRIGGER 0
#endif

#ifndef CONFIG_DRIVER_TASCAM_X8
#define CONFIG_DRIVER_TASCAM_X8 0
#endif

#ifndef CONFIG_MAX_DEVICE_INSTANCES
#define CONFIG_MAX_DEVICE_INSTANCES 8
#endif

#ifndef CONFIG_DEVICE_COMMAND_QUEUE_SIZE
#define CONFIG_DEVICE_COMMAND_QUEUE_SIZE 12
#endif

#ifndef CONFIG_MAX_ACTIVE_LINKS
#define CONFIG_MAX_ACTIVE_LINKS 4
#endif

#ifndef CONFIG_MAX_SCENES
#define CONFIG_MAX_SCENES 4
#endif

#ifndef CONFIG_MAX_SCENE_STEPS
#define CONFIG_MAX_SCENE_STEPS 8
#endif

#ifndef CONFIG_SCENE_MIN_WAIT_MS
#define CONFIG_SCENE_MIN_WAIT_MS 0
#endif

#ifndef CONFIG_SCENE_MAX_WAIT_MS
#define CONFIG_SCENE_MAX_WAIT_MS 60000
#endif

#ifndef CONFIG_SCENE_CONNECT_TIMEOUT_MS
#define CONFIG_SCENE_CONNECT_TIMEOUT_MS 20000
#endif

#ifndef CONFIG_SCENE_ACTION_TIMEOUT_MS
#define CONFIG_SCENE_ACTION_TIMEOUT_MS 5000
#endif

