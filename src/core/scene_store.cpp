#include "core/scene_store.h"

#include <cstring>
#include <memory>
#include <new>

#include "core/blob_codec.h"

namespace studio {
namespace {

constexpr uint8_t kMagic[] = {'S', 'C', 'N', '1'};
constexpr size_t kHeaderSize = 15;
constexpr size_t kEncodedStepSize = 1 + 4 + 1 + 4 + 4 + 4 + 4;
constexpr size_t kEncodedSceneHeaderSize =
    4 + 1 + kDeviceNameCapacity + 1 + 1 + 1;
constexpr size_t kChecksumSize = 4;

void encodeStep(BlobWriter& writer, const SceneStep& step) {
  writer.u8(static_cast<uint8_t>(step.type));
  writer.u32(step.targetId);
  writer.u8(static_cast<uint8_t>(step.command));
  writer.u32(step.waitMs);
  writer.u32(static_cast<uint32_t>(step.value0));
  writer.u32(static_cast<uint32_t>(step.value1));
  writer.u32(static_cast<uint32_t>(step.value2));
}

bool decodeStep(BlobReader& reader, SceneStep& step) {
  uint8_t type = 0;
  uint8_t command = 0;
  uint32_t value0 = 0;
  uint32_t value1 = 0;
  uint32_t value2 = 0;
  if (!reader.u8(type) || !reader.u32(step.targetId) ||
      !reader.u8(command) || !reader.u32(step.waitMs) ||
      !reader.u32(value0) || !reader.u32(value1) || !reader.u32(value2)) {
    return false;
  }
  step.type = static_cast<SceneStepType>(type);
  step.command = static_cast<CommandType>(command);
  step.value0 = static_cast<int32_t>(value0);
  step.value1 = static_cast<int32_t>(value1);
  step.value2 = static_cast<int32_t>(value2);
  return true;
}

}  // namespace

ConfigLoadStatus SceneStore::load(SceneRegistry& registry) {
  std::unique_ptr<uint8_t[]> blob(new (std::nothrow) uint8_t[kMaxBlobSize]);
  if (!blob) return ConfigLoadStatus::Corrupt;
  const size_t length = backend_.read(blob.get(), kMaxBlobSize);
  if (length == 0) return ConfigLoadStatus::Missing;
  if (length < kHeaderSize + kChecksumSize ||
      std::memcmp(blob.get(), kMagic, sizeof(kMagic)) != 0) {
    return ConfigLoadStatus::Corrupt;
  }

  BlobReader reader(blob.get() + sizeof(kMagic), length - sizeof(kMagic));
  uint16_t version = 0;
  uint8_t initialized = 0;
  uint32_t count = 0;
  SceneId nextSceneId = 0;
  if (!reader.u16(version) || !reader.u8(initialized) ||
      !reader.u32(count) || !reader.u32(nextSceneId) ||
      version != kSchemaVersion || initialized > 1 ||
      count > (length - kHeaderSize - kChecksumSize) /
                  kEncodedSceneHeaderSize) {
    return ConfigLoadStatus::Corrupt;
  }

  BlobReader checksumReader(blob.get() + length - kChecksumSize, kChecksumSize);
  uint32_t storedChecksum = 0;
  if (!checksumReader.u32(storedChecksum) ||
      storedChecksum != fnv1a(blob.get(), length - kChecksumSize)) {
    return ConfigLoadStatus::Corrupt;
  }

  std::unique_ptr<SceneRecord[]> records(
      count == 0 ? nullptr : new (std::nothrow) SceneRecord[count]());
  if (count > 0 && !records) return ConfigLoadStatus::Corrupt;
  for (size_t i = 0; i < count; ++i) {
    SceneRecord& record = records[i];
    uint8_t enabled = 0;
    uint8_t mode = 0;
    if (reader.remaining() < kEncodedSceneHeaderSize + kChecksumSize ||
        !reader.u32(record.sceneId) || !reader.u8(enabled) ||
        !reader.text(record.name) || !reader.u8(record.startCount) ||
        !reader.u8(record.stopCount) || !reader.u8(mode) || enabled > 1 ||
        mode > static_cast<uint8_t>(SceneStopMode::Custom) ||
        record.startCount > CONFIG_MAX_SCENE_STEPS ||
        record.stopCount > CONFIG_MAX_SCENE_STEPS) {
      return ConfigLoadStatus::Corrupt;
    }
    record.enabled = enabled != 0;
    record.stopMode = static_cast<SceneStopMode>(mode);

    const size_t stepsLength =
        static_cast<size_t>(record.startCount + record.stopCount) *
        kEncodedStepSize;
    if (reader.remaining() < stepsLength + kChecksumSize) {
      return ConfigLoadStatus::Corrupt;
    }
    for (uint8_t step = 0; step < record.startCount; ++step) {
      if (!decodeStep(reader, record.startSteps[step])) {
        return ConfigLoadStatus::Corrupt;
      }
    }
    for (uint8_t step = 0; step < record.stopCount; ++step) {
      if (!decodeStep(reader, record.stopSteps[step])) {
        return ConfigLoadStatus::Corrupt;
      }
    }
  }

  if (reader.position() != length - sizeof(kMagic) - kChecksumSize ||
      !registry.restore(records.get(), count, nextSceneId, initialized != 0)) {
    return ConfigLoadStatus::Corrupt;
  }
  return ConfigLoadStatus::Loaded;
}

bool SceneStore::save(const SceneRegistry& registry) {
  size_t length = kHeaderSize + kChecksumSize;
  for (size_t i = 0; i < registry.count(); ++i) {
    const SceneRecord* record = registry.at(i);
    if (record == nullptr || record->startCount > CONFIG_MAX_SCENE_STEPS ||
        record->stopCount > CONFIG_MAX_SCENE_STEPS ||
        record->stopMode > SceneStopMode::Custom) {
      return false;
    }
    const size_t recordSize = kEncodedSceneHeaderSize +
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
  BlobWriter writer(blob.get(), length);
  writer.bytes(kMagic, sizeof(kMagic));
  writer.u16(kSchemaVersion);
  writer.u8(registry.initialized() ? 1 : 0);
  writer.u32(static_cast<uint32_t>(registry.count()));
  writer.u32(registry.nextSceneId());

  for (size_t i = 0; i < registry.count(); ++i) {
    const SceneRecord* record = registry.at(i);
    writer.u32(record->sceneId);
    writer.u8(record->enabled ? 1 : 0);
    writer.bytes(record->name, sizeof(record->name));
    writer.u8(record->startCount);
    writer.u8(record->stopCount);
    writer.u8(static_cast<uint8_t>(record->stopMode));
    for (uint8_t step = 0; step < record->startCount; ++step) {
      encodeStep(writer, record->startSteps[step]);
    }
    for (uint8_t step = 0; step < record->stopCount; ++step) {
      encodeStep(writer, record->stopSteps[step]);
    }
  }

  writer.u32(fnv1a(blob.get(), length - kChecksumSize));
  return writer.valid() && writer.size() == length &&
         backend_.write(blob.get(), length);
}

}  // namespace studio
