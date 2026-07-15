#if defined(NRF54L15_CLEAN_MATTER_CORE_ENABLE) && NRF54L15_CLEAN_MATTER_CORE_ENABLE
#include "matter_access_control.h"

#include <string.h>

namespace xiao_nrf54l15 {

// ─── Entry Management ────────────────────────────────────────────

bool MatterAccessControl::addEntry(const AclEntry& entry) {
  if (entryCount_ >= kAclMaxEntries) return false;
  entries_[entryCount_++] = entry;
  return true;
}

bool MatterAccessControl::removeEntry(uint8_t index) {
  if (index >= entryCount_) return false;

  // Shift entries down
  for (uint8_t i = index; i < entryCount_ - 1; i++) {
    entries_[i] = entries_[i + 1];
  }
  entryCount_--;
  return true;
}

bool MatterAccessControl::getEntry(uint8_t index, AclEntry* outEntry) const {
  if (outEntry == nullptr || index >= entryCount_) return false;
  *outEntry = entries_[index];
  return true;
}

void MatterAccessControl::clear() {
  for (AclEntry& entry : entries_) {
    entry = AclEntry{};
  }
  entryCount_ = 0U;
}

// ─── Default Entries ─────────────────────────────────────────────

bool MatterAccessControl::addDefaultViewEntry() {
  AclEntry entry = {};
  entry.wildcardFabric = true;
  entry.wildcardNode = true;
  entry.wildcardSubject = true;
  entry.wildcardCluster = true;
  entry.wildcardEndpoint = true;
  entry.privilege = AclPrivilege::kView;
  entry.valid = true;
  return addEntry(entry);
}

bool MatterAccessControl::addNodeOperateEntry(
    const uint8_t nodeId[8],
    const uint8_t fabricId[8]) {
  AclEntry entry = {};

  if (nodeId != nullptr) memcpy(entry.nodeId, nodeId, 8);
  if (fabricId != nullptr) memcpy(entry.fabricId, fabricId, 8);

  entry.wildcardNode = (nodeId == nullptr);
  entry.wildcardFabric = (fabricId == nullptr);
  entry.wildcardSubject = true;
  entry.wildcardCluster = true;
  entry.wildcardEndpoint = true;
  entry.privilege = AclPrivilege::kOperate;
  entry.valid = true;

  return addEntry(entry);
}

// ─── Access Check ────────────────────────────────────────────────

bool MatterAccessControl::matchesEntry(
    const AclEntry& entry,
    const uint8_t subjectId[8],
    const uint8_t fabricId[8],
    const uint8_t nodeId[8],
    uint32_t clusterId,
    uint16_t endpointId) const {

  // Check fabric match
  if (!entry.wildcardFabric) {
    if (memcmp(entry.fabricId, fabricId, 8) != 0) return false;
  }

  // Check node match
  if (!entry.wildcardNode) {
    if (memcmp(entry.nodeId, nodeId, 8) != 0) return false;
  }

  // Subject IDs are opaque 64-bit values; leading zero bytes are significant.
  if (!entry.wildcardSubject) {
    if (memcmp(entry.subjectId, subjectId, 8) != 0) return false;
  }

  // Check cluster match
  if (!entry.wildcardCluster) {
    if (entry.clusterId != clusterId) return false;
  }

  // Check endpoint match
  if (!entry.wildcardEndpoint) {
    if (entry.endpointId != endpointId) return false;
  }

  return true;
}

bool MatterAccessControl::checkAccess(
    const uint8_t subjectId[8],
    const uint8_t fabricId[8],
    const uint8_t nodeId[8],
    uint32_t clusterId,
    uint16_t endpointId,
    AclPrivilege requiredPrivilege) const {

  if (subjectId == nullptr || fabricId == nullptr || nodeId == nullptr) {
    return false;
  }
  const uint8_t required = static_cast<uint8_t>(requiredPrivilege);
  if (required < static_cast<uint8_t>(AclPrivilege::kView) ||
      required > static_cast<uint8_t>(AclPrivilege::kManage)) {
    return false;
  }

  // Any matching entry with sufficient privilege authorizes the operation.
  for (uint8_t i = 0; i < entryCount_; i++) {
    const AclEntry& entry = entries_[i];
    if (!entry.valid) continue;

    if (matchesEntry(entry, subjectId, fabricId, nodeId,
                     clusterId, endpointId)) {
      // Check if entry's privilege meets or exceeds required
      const uint8_t granted = static_cast<uint8_t>(entry.privilege);
      if (granted >= static_cast<uint8_t>(AclPrivilege::kView) &&
          granted <= static_cast<uint8_t>(AclPrivilege::kManage) &&
          granted >= required) {
        return true;
      }
      // Privilege insufficient, continue checking other entries
    }
  }

  // No matching entry — deny access
  return false;
}

// ─── Utilities ───────────────────────────────────────────────────

const char* MatterAccessControl::privilegeName(AclPrivilege privilege) {
  switch (privilege) {
    case AclPrivilege::kNone: return "none";
    case AclPrivilege::kView: return "view";
    case AclPrivilege::kOperate: return "operate";
    case AclPrivilege::kManage: return "manage";
    default: return "unknown";
  }
}

}  // namespace xiao_nrf54l15
#endif // NRF54L15_CLEAN_MATTER_CORE_ENABLE
