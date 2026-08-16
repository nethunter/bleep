#pragma once

#ifndef BLEEP_FIRMWARE_VERSION
#define BLEEP_FIRMWARE_VERSION "0.3.0"
#endif

#ifndef BLEEP_GIT_COMMIT
#define BLEEP_GIT_COMMIT "unknown"
#endif

#ifndef BLEEP_GIT_DATE
#define BLEEP_GIT_DATE "unknown"
#endif

#ifndef BLEEP_RELEASE_SEQUENCE
#define BLEEP_RELEASE_SEQUENCE 0ULL
#endif

#ifndef BLEEP_RELEASE_CHANNEL
#define BLEEP_RELEASE_CHANNEL "local"
#endif

#ifndef DISPLAY_VARIANT_NAME
#define DISPLAY_VARIANT_NAME "unknown"
#endif

namespace build_info {

constexpr const char* kFirmwareVersion = BLEEP_FIRMWARE_VERSION;
constexpr const char* kGitCommit = BLEEP_GIT_COMMIT;
constexpr const char* kGitDate = BLEEP_GIT_DATE;
constexpr unsigned long long kReleaseSequence = BLEEP_RELEASE_SEQUENCE;
constexpr const char* kReleaseChannel = BLEEP_RELEASE_CHANNEL;
constexpr const char* kHardware = DISPLAY_VARIANT_NAME;

}  // namespace build_info
