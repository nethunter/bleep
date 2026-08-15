#include "core/recovery_journal.h"

#include <cstring>

namespace studio {
namespace {

constexpr uint32_t kMagic = 0x524A4E4C;
constexpr uint16_t kVersion = 1;
constexpr size_t kSlotSize = 4096;

struct StoredRecord {
  uint32_t magic;
  uint16_t version;
  uint16_t encodedSize;
  uint32_t generation;
  RecoveryRecord record;
  uint32_t crc;
};

uint32_t checksum(const uint8_t* data, size_t length) {
  uint32_t value = 2166136261u;
  for (size_t i = 0; i < length; ++i) value = (value ^ data[i]) * 16777619u;
  return value;
}

bool valid(const StoredRecord& stored) {
  if (stored.magic != kMagic || stored.version != kVersion ||
      stored.encodedSize != sizeof(StoredRecord) ||
      stored.record.manifestLength > kRecoveryManifestCapacity ||
      stored.record.signatureLength > kRecoverySignatureCapacity ||
      static_cast<uint8_t>(stored.record.operation) >
          static_cast<uint8_t>(RecoveryOperation::ResetComplete)) return false;
  return stored.crc == checksum(reinterpret_cast<const uint8_t*>(&stored),
                                offsetof(StoredRecord, crc));
}

}  // namespace

static_assert(sizeof(StoredRecord) <= kSlotSize, "recovery journal record too large");

bool RecoveryJournal::load(RecoveryRecord& record) {
  StoredRecord slots[2] = {};
  const bool valid0 = backend_.readSlot(0, reinterpret_cast<uint8_t*>(&slots[0]),
                                        sizeof(slots[0])) && valid(slots[0]);
  const bool valid1 = backend_.readSlot(1, reinterpret_cast<uint8_t*>(&slots[1]),
                                        sizeof(slots[1])) && valid(slots[1]);
  if (!valid0 && !valid1) {
    record = {};
    generation_ = 0;
    activeSlot_ = 1;
    return false;
  }
  activeSlot_ = valid1 && (!valid0 || slots[1].generation > slots[0].generation) ? 1 : 0;
  const StoredRecord& selected = slots[activeSlot_];
  generation_ = selected.generation;
  record = selected.record;
  return true;
}

bool RecoveryJournal::save(const RecoveryRecord& record) {
  StoredRecord stored = {};
  stored.magic = kMagic;
  stored.version = kVersion;
  stored.encodedSize = sizeof(StoredRecord);
  stored.generation = generation_ + 1;
  stored.record = record;
  stored.crc = checksum(reinterpret_cast<const uint8_t*>(&stored),
                        offsetof(StoredRecord, crc));
  const uint8_t next = activeSlot_ == 0 ? 1 : 0;
  if (!backend_.writeSlot(next, reinterpret_cast<const uint8_t*>(&stored),
                          sizeof(stored))) return false;
  activeSlot_ = next;
  generation_ = stored.generation;
  return true;
}

bool RecoveryJournal::clear() {
  if (!backend_.erase()) return false;
  generation_ = 0;
  activeSlot_ = 1;
  return true;
}

}  // namespace studio
