#include "matter_fabric_table.h"

#include <string.h>

namespace xiao_nrf54l15 {

// ─── Fabric Management ──────────────────────────────────────────

uint8_t MatterFabricTable::addFabric(const uint8_t fabricId[8],
                                      const uint8_t nodeId[8],
                                      const uint8_t rootPublicKey[65],
                                      const char* label,
                                      bool isLocalFabric) {
  if (fabricCount_ >= kMaxFabrics || fabricId == nullptr || nodeId == nullptr) {
    return kMaxFabrics;  // Invalid index
  }

  // Find first empty slot
  uint8_t idx = 0;
  for (; idx < kMaxFabrics; idx++) {
    if (!entries_[idx].valid) break;
  }
  if (idx >= kMaxFabrics) return kMaxFabrics;

  FabricEntry& entry = entries_[idx];
  memset(&entry, 0, sizeof(entry));
  memcpy(entry.fabricId, fabricId, 8);
  memcpy(entry.nodeId, nodeId, 8);
  if (rootPublicKey != nullptr) {
    memcpy(entry.rootPublicKey, rootPublicKey, 65);
  }
  if (label != nullptr) {
    strncpy(entry.label, label, kFabricLabelSize - 1);
    entry.label[kFabricLabelSize - 1] = '\0';
  }
  entry.isLocalFabric = isLocalFabric;
  entry.valid = true;
  fabricCount_++;
  return idx;
}

bool MatterFabricTable::removeFabric(uint8_t fabricIndex) {
  if (fabricIndex >= kMaxFabrics || !entries_[fabricIndex].valid) {
    return false;
  }

  memset(&entries_[fabricIndex], 0, sizeof(entries_[fabricIndex]));
  fabricCount_--;
  return true;
}

int8_t MatterFabricTable::findFabric(const uint8_t fabricId[8]) const {
  if (fabricId == nullptr) return -1;
  for (uint8_t i = 0; i < kMaxFabrics; i++) {
    if (entries_[i].valid && memcmp(entries_[i].fabricId, fabricId, 8) == 0) {
      return static_cast<int8_t>(i);
    }
  }
  return -1;
}

int8_t MatterFabricTable::findFabricByNode(const uint8_t nodeId[8]) const {
  if (nodeId == nullptr) return -1;
  for (uint8_t i = 0; i < kMaxFabrics; i++) {
    if (entries_[i].valid && memcmp(entries_[i].nodeId, nodeId, 8) == 0) {
      return static_cast<int8_t>(i);
    }
  }
  return -1;
}

bool MatterFabricTable::getFabric(uint8_t fabricIndex,
                                   FabricEntry* outEntry) const {
  if (outEntry == nullptr || fabricIndex >= kMaxFabrics ||
      !entries_[fabricIndex].valid) {
    return false;
  }
  *outEntry = entries_[fabricIndex];
  return true;
}

uint8_t MatterFabricTable::nextFabricIndex() const {
  for (uint8_t i = 0; i < kMaxFabrics; i++) {
    if (!entries_[i].valid) return i;
  }
  return kMaxFabrics;
}

void MatterFabricTable::clear() {
  memset(entries_, 0, sizeof(entries_));
  fabricCount_ = 0U;
}

const uint8_t* MatterFabricTable::findFabricIdForNode(
    const uint8_t nodeId[8]) const {
  if (nodeId == nullptr) return nullptr;
  for (uint8_t i = 0; i < kMaxFabrics; i++) {
    if (entries_[i].valid && memcmp(entries_[i].nodeId, nodeId, 8) == 0) {
      return entries_[i].fabricId;
    }
  }
  return nullptr;
}

uint8_t MatterFabricTable::addTestFabric(uint8_t fabricIndex,
                                          const char* label) {
  if (fabricIndex >= kMaxFabrics) return kMaxFabrics;
  if (entries_[fabricIndex].valid) return kMaxFabrics;

  uint8_t fabricId[8] = {0};
  uint8_t nodeId[8] = {0};
  fabricId[7] = static_cast<uint8_t>(fabricIndex + 1);  // 0x01, 0x02, etc.
  nodeId[7] = static_cast<uint8_t>(0x10 + fabricIndex);  // 0x10, 0x11, etc.

  return addFabric(fabricId, nodeId, nullptr, label, true);
}

}  // namespace xiao_nrf54l15