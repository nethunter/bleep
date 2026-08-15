#include "core/partition_recovery_backend.h"

#include <esp_partition.h>

namespace studio {
namespace {

constexpr size_t kSlotStride = 0x1000;
constexpr char kPartitionLabel[] = "rec_state";

const esp_partition_t* partition() {
  return esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                  static_cast<esp_partition_subtype_t>(0x40),
                                  kPartitionLabel);
}

}  // namespace

bool PartitionRecoveryJournalBackend::readSlot(uint8_t slot, uint8_t* destination,
                                                size_t capacity) {
  const esp_partition_t* target = partition();
  return target != nullptr && slot < 2 && capacity <= kSlotStride &&
      esp_partition_read(target, slot * kSlotStride, destination, capacity) == ESP_OK;
}

bool PartitionRecoveryJournalBackend::writeSlot(uint8_t slot, const uint8_t* source,
                                                 size_t length) {
  const esp_partition_t* target = partition();
  if (target == nullptr || slot >= 2 || length > kSlotStride) return false;
  const size_t offset = slot * kSlotStride;
  return esp_partition_erase_range(target, offset, kSlotStride) == ESP_OK &&
      esp_partition_write(target, offset, source, length) == ESP_OK;
}

bool PartitionRecoveryJournalBackend::erase() {
  const esp_partition_t* target = partition();
  return target != nullptr &&
      esp_partition_erase_range(target, 0, target->size) == ESP_OK;
}

}  // namespace studio
