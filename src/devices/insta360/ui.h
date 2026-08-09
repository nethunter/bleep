#pragma once
#include "core/device_types.h"
namespace insta360_ui { void show(studio::InstanceId); void hide(); void release(); bool active(); void tick(); void handleShortPress(); void handleLongPress(); }
