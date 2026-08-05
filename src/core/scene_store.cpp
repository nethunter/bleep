#include "core/scene_store.h"

#include <cstring>

namespace studio {
namespace {

constexpr uint8_t kMagic[] = {'S', 'C', 'N', '1'};
constexpr size_t kHeaderSize = 12;
constexpr size_t kEncodedV1StepSize = 1 + 4 + 1 + 4;
constexpr size_t kEncodedStepSize = kEncodedV1StepSize + 4 + 4 + 4;
constexpr size_t encodedSceneSize(size_t stepSize) {
  return
    4 + 1 + kDeviceNameCapacity + 1 + 1 +
    CONFIG_MAX_SCENE_STEPS * stepSize * 2;
}
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

ConfigLoadStatus SceneStore::load(SceneRegistry& registry) {
  uint8_t blob[kMaxBlobSize];
  const size_t length = backend_.read(blob, sizeof(blob));
  if (length == 0) {
    return ConfigLoadStatus::Missing;
  }
  if (length < kHeaderSize + kChecksumSize ||
      std::memcmp(blob, kMagic, sizeof(kMagic)) != 0) {
    return ConfigLoadStatus::Corrupt;
  }

  const uint8_t* cursor = blob + sizeof(kMagic);
  const uint16_t version = getU16(cursor);
  const bool initialized = *cursor++ != 0;
  const uint8_t count = *cursor++;
  const SceneId nextSceneId = getU32(cursor);
  const bool v1 = version == 1;
  const size_t stepSize = v1 ? kEncodedV1StepSize : kEncodedStepSize;
  const size_t expectedLength = kHeaderSize + static_cast<size_t>(count) *
      encodedSceneSize(stepSize) + kChecksumSize;
  if ((!v1 && version != kSchemaVersion) || count > CONFIG_MAX_SCENES ||
      length != expectedLength) {
    return ConfigLoadStatus::Corrupt;
  }

  const uint8_t* storedChecksumCursor = blob + length - kChecksumSize;
  const uint32_t storedChecksum = getU32(storedChecksumCursor);
  if (storedChecksum != checksum(blob, length - kChecksumSize)) {
    return ConfigLoadStatus::Corrupt;
  }

  SceneRecord records[CONFIG_MAX_SCENES] = {};
  for (size_t i = 0; i < count; ++i) {
    SceneRecord& record = records[i];
    record.sceneId = getU32(cursor);
    record.enabled = (*cursor++ != 0);
    decodeText(record.name, cursor);
    record.startCount = *cursor++;
    record.stopCount = *cursor++;
    if (record.startCount > CONFIG_MAX_SCENE_STEPS ||
        record.stopCount > CONFIG_MAX_SCENE_STEPS) {
      return ConfigLoadStatus::Corrupt;
    }
    for (uint8_t s = 0; s < CONFIG_MAX_SCENE_STEPS; ++s) {
      record.startSteps[s] = decodeStep(cursor, !v1);
    }
    for (uint8_t s = 0; s < CONFIG_MAX_SCENE_STEPS; ++s) {
      record.stopSteps[s] = decodeStep(cursor, !v1);
    }
  }

  if (!registry.restore(records, count, nextSceneId, initialized)) {
    return ConfigLoadStatus::Corrupt;
  }
  return ConfigLoadStatus::Loaded;
}

bool SceneStore::save(const SceneRegistry& registry) {
  const size_t length =
      kHeaderSize + registry.count() * encodedSceneSize(kEncodedStepSize) + kChecksumSize;
  if (length > kMaxBlobSize || registry.count() > 255) {
    return false;
  }

  uint8_t blob[kMaxBlobSize] = {};
  uint8_t* cursor = blob;
  std::memcpy(cursor, kMagic, sizeof(kMagic));
  cursor += sizeof(kMagic);
  putU16(cursor, kSchemaVersion);
  *cursor++ = registry.initialized() ? 1 : 0;
  *cursor++ = static_cast<uint8_t>(registry.count());
  putU32(cursor, registry.nextSceneId());

  for (size_t i = 0; i < registry.count(); ++i) {
    const SceneRecord* record = registry.at(i);
    putU32(cursor, record->sceneId);
    *cursor++ = record->enabled ? 1 : 0;
    std::memcpy(cursor, record->name, sizeof(record->name));
    cursor += sizeof(record->name);
    *cursor++ = record->startCount;
    *cursor++ = record->stopCount;
    for (uint8_t s = 0; s < CONFIG_MAX_SCENE_STEPS; ++s) {
      encodeStep(cursor, record->startSteps[s]);
    }
    for (uint8_t s = 0; s < CONFIG_MAX_SCENE_STEPS; ++s) {
      encodeStep(cursor, record->stopSteps[s]);
    }
  }

  putU32(cursor, checksum(blob, length - kChecksumSize));
  return static_cast<size_t>(cursor - blob) == length &&
         backend_.write(blob, length);
}

}  // namespace studio
