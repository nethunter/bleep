#include "core/scene_store.h"

#include <cstring>
#include <memory>
#include <new>

namespace studio {
namespace {

constexpr uint8_t kMagic[] = {'S', 'C', 'N', '1'};
constexpr size_t kLegacyHeaderSize = 12;
constexpr size_t kHeaderSize = 15;
constexpr size_t kEncodedV1StepSize = 1 + 4 + 1 + 4;
constexpr size_t kEncodedStepSize = kEncodedV1StepSize + 4 + 4 + 4;
constexpr size_t kEncodedSceneHeaderSize = 4 + 1 + kDeviceNameCapacity + 1 + 1;
constexpr size_t encodedLegacySceneSize(size_t stepSize) {
  return
    kEncodedSceneHeaderSize +
    CONFIG_MAX_SCENE_STEPS * stepSize * 2;
}
constexpr size_t kEncodedSceneHeaderWithModeSize =
    kEncodedSceneHeaderSize + 1;
constexpr size_t kChecksumSize = 4;

uint32_t checksum(const uint8_t* data, size_t length) {
  uint32_t value = 2166136261u;
  for (size_t i = 0; i < length; ++i) {
    value ^= data[i];
    value *= 16777619u;
  }
  return value;
}

void putU16(uint8_t*& out, uint16_t value) {
  *out++ = static_cast<uint8_t>(value & 0xFF);
  *out++ = static_cast<uint8_t>((value >> 8) & 0xFF);
}

void putU32(uint8_t*& out, uint32_t value) {
  for (int shift = 0; shift < 32; shift += 8) {
    *out++ = static_cast<uint8_t>((value >> shift) & 0xFF);
  }
}

uint16_t getU16(const uint8_t*& in) {
  const uint16_t value =
      static_cast<uint16_t>(in[0]) | (static_cast<uint16_t>(in[1]) << 8);
  in += 2;
  return value;
}

uint32_t getU32(const uint8_t*& in) {
  uint32_t value = 0;
  for (int shift = 0; shift < 32; shift += 8) {
    value |= static_cast<uint32_t>(*in++) << shift;
  }
  return value;
}

template <size_t N>
void decodeText(char (&destination)[N], const uint8_t*& in) {
  std::memcpy(destination, in, N);
  destination[N - 1] = '\0';
  in += N;
}

void encodeStep(uint8_t*& out, const SceneStep& step) {
  *out++ = static_cast<uint8_t>(step.type);
  putU32(out, step.targetId);
  *out++ = static_cast<uint8_t>(step.command);
  putU32(out, step.waitMs);
  putU32(out, static_cast<uint32_t>(step.value0));
  putU32(out, static_cast<uint32_t>(step.value1));
  putU32(out, static_cast<uint32_t>(step.value2));
}

SceneStep decodeStep(const uint8_t*& in, bool hasValues) {
  SceneStep step;
  step.type = static_cast<SceneStepType>(*in++);
  step.targetId = getU32(in);
  step.command = static_cast<CommandType>(*in++);
  step.waitMs = getU32(in);
  if (hasValues) {
    step.value0 = static_cast<int32_t>(getU32(in));
    step.value1 = static_cast<int32_t>(getU32(in));
    step.value2 = static_cast<int32_t>(getU32(in));
  }
  return step;
}

}  // namespace

ConfigLoadStatus SceneStore::load(SceneRegistry& registry, bool* migrated) {
  if (migrated != nullptr) *migrated = false;
  std::unique_ptr<uint8_t[]> blob(new (std::nothrow) uint8_t[kMaxBlobSize]);
  if (!blob) return ConfigLoadStatus::Corrupt;
  const size_t length = backend_.read(blob.get(), kMaxBlobSize);
  if (length == 0) {
    return ConfigLoadStatus::Missing;
  }
  if (length < kLegacyHeaderSize + kChecksumSize ||
      std::memcmp(blob.get(), kMagic, sizeof(kMagic)) != 0) {
    return ConfigLoadStatus::Corrupt;
  }

  const uint8_t* cursor = blob.get() + sizeof(kMagic);
  const uint16_t version = getU16(cursor);
  const bool legacy = version == 1 || version == 2;
  const bool hasStopMode = version >= 4;
  if (!legacy && version != 3 && version != kSchemaVersion) {
    return ConfigLoadStatus::Corrupt;
  }
  if (!legacy && length < kHeaderSize + kChecksumSize) {
    return ConfigLoadStatus::Corrupt;
  }
  const bool initialized = *cursor++ != 0;
  const uint32_t count = legacy ? *cursor++ : getU32(cursor);
  const SceneId nextSceneId = getU32(cursor);
  if (legacy) {
    const size_t stepSize = version == 1 ? kEncodedV1StepSize : kEncodedStepSize;
    const size_t expectedLength = kLegacyHeaderSize + static_cast<size_t>(count) *
        encodedLegacySceneSize(stepSize) + kChecksumSize;
    if (length != expectedLength) return ConfigLoadStatus::Corrupt;
  } else if (length < kHeaderSize + kChecksumSize ||
             count > (length - kHeaderSize - kChecksumSize) /
                         (hasStopMode ? kEncodedSceneHeaderWithModeSize
                                      : kEncodedSceneHeaderSize)) {
    return ConfigLoadStatus::Corrupt;
  }

  const uint8_t* storedChecksumCursor = blob.get() + length - kChecksumSize;
  const uint8_t* checksumCursor = storedChecksumCursor;
  const uint32_t storedChecksum = getU32(checksumCursor);
  if (storedChecksum != checksum(blob.get(), length - kChecksumSize)) {
    return ConfigLoadStatus::Corrupt;
  }

  std::unique_ptr<SceneRecord[]> records(
      count == 0 ? nullptr : new (std::nothrow) SceneRecord[count]());
  if (count > 0 && !records) return ConfigLoadStatus::Corrupt;
  for (size_t i = 0; i < count; ++i) {
    SceneRecord& record = records[i];
    if (static_cast<size_t>(storedChecksumCursor - cursor) <
        (hasStopMode ? kEncodedSceneHeaderWithModeSize
                     : kEncodedSceneHeaderSize)) {
      return ConfigLoadStatus::Corrupt;
    }
    record.sceneId = getU32(cursor);
    record.enabled = (*cursor++ != 0);
    decodeText(record.name, cursor);
    record.startCount = *cursor++;
    record.stopCount = *cursor++;
    if (hasStopMode) {
      const uint8_t mode = *cursor++;
      if (mode > static_cast<uint8_t>(SceneStopMode::Custom)) {
        return ConfigLoadStatus::Corrupt;
      }
      record.stopMode = static_cast<SceneStopMode>(mode);
    } else {
      record.stopMode = SceneStopMode::Generated;
    }
    if (record.startCount > CONFIG_MAX_SCENE_STEPS ||
        record.stopCount > CONFIG_MAX_SCENE_STEPS) {
      return ConfigLoadStatus::Corrupt;
    }
    const uint8_t encodedStartCount = legacy ? CONFIG_MAX_SCENE_STEPS
                                              : record.startCount;
    const uint8_t encodedStopCount = legacy ? CONFIG_MAX_SCENE_STEPS
                                             : record.stopCount;
    const size_t encodedStepSize = version == 1 ? kEncodedV1StepSize
                                                 : kEncodedStepSize;
    const size_t stepsLength =
        static_cast<size_t>(encodedStartCount + encodedStopCount) *
        encodedStepSize;
    if (static_cast<size_t>(storedChecksumCursor - cursor) < stepsLength) {
      return ConfigLoadStatus::Corrupt;
    }
    for (uint8_t s = 0; s < encodedStartCount; ++s) {
      const SceneStep step = decodeStep(cursor, version != 1);
      if (s < record.startCount) record.startSteps[s] = step;
    }
    for (uint8_t s = 0; s < encodedStopCount; ++s) {
      const SceneStep step = decodeStep(cursor, version != 1);
      if (s < record.stopCount) record.stopSteps[s] = step;
    }
    if (!hasStopMode || record.stopMode == SceneStopMode::Generated) {
      generateStopSteps(record);
    }
  }

  if (cursor != storedChecksumCursor) return ConfigLoadStatus::Corrupt;

  if (!registry.restore(records.get(), count, nextSceneId, initialized)) {
    return ConfigLoadStatus::Corrupt;
  }
  if (migrated != nullptr) *migrated = !hasStopMode;
  return ConfigLoadStatus::Loaded;
}

bool SceneStore::save(const SceneRegistry& registry) {
  size_t length = kHeaderSize + kChecksumSize;
  for (size_t i = 0; i < registry.count(); ++i) {
    const SceneRecord* record = registry.at(i);
    if (record == nullptr || record->startCount > CONFIG_MAX_SCENE_STEPS ||
        record->stopCount > CONFIG_MAX_SCENE_STEPS ||
        record->stopMode > SceneStopMode::Custom) return false;
    const size_t recordSize = kEncodedSceneHeaderWithModeSize +
        static_cast<size_t>(record->startCount + record->stopCount) *
            kEncodedStepSize;
    if (length > kMaxBlobSize || recordSize > kMaxBlobSize - length) {
      return false;
    }
    length += recordSize;
  }

  if (registry.count() > UINT32_MAX) return false;
  std::unique_ptr<uint8_t[]> blob(new (std::nothrow) uint8_t[length]());
  if (!blob) return false;
  uint8_t* cursor = blob.get();
  std::memcpy(cursor, kMagic, sizeof(kMagic));
  cursor += sizeof(kMagic);
  putU16(cursor, kSchemaVersion);
  *cursor++ = registry.initialized() ? 1 : 0;
  putU32(cursor, static_cast<uint32_t>(registry.count()));
  putU32(cursor, registry.nextSceneId());

  for (size_t i = 0; i < registry.count(); ++i) {
    const SceneRecord* record = registry.at(i);
    putU32(cursor, record->sceneId);
    *cursor++ = record->enabled ? 1 : 0;
    std::memcpy(cursor, record->name, sizeof(record->name));
    cursor += sizeof(record->name);
    *cursor++ = record->startCount;
    *cursor++ = record->stopCount;
    *cursor++ = static_cast<uint8_t>(record->stopMode);
    for (uint8_t s = 0; s < record->startCount; ++s) {
      encodeStep(cursor, record->startSteps[s]);
    }
    for (uint8_t s = 0; s < record->stopCount; ++s) {
      encodeStep(cursor, record->stopSteps[s]);
    }
  }

  putU32(cursor, checksum(blob.get(), length - kChecksumSize));
  return static_cast<size_t>(cursor - blob.get()) == length &&
         backend_.write(blob.get(), length);
}

}  // namespace studio
