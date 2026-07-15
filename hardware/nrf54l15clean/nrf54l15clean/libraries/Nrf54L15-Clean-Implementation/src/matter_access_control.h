#pragma once

#include <stddef.h>
#include <stdint.h>

#include "matter_secp256r1.h"

namespace xiao_nrf54l15 {

// Matter Access Control List (ACL) Implementation
// Implements authorization for cluster operations on endpoints

constexpr size_t kAclMaxEntries = 8U;
constexpr size_t kAclMaxPrivileges = 4U;

// Matter privilege levels
enum class AclPrivilege : uint8_t {
  kNone = 0U,
  kView = 1U,      // Read access
  kOperate = 2U,   // Read + write (commands)
  kManage = 3U,    // Full access (including ACL modification)
};

// Matter ACL entry
struct AclEntry {
  uint8_t fabricId[8] = {0};           // Fabric this entry applies to
  uint8_t nodeId[8] = {0};             // Node ID; wildcard is explicit
  uint8_t subjectId[8] = {0};          // Subject (accessor) ID
  uint32_t clusterId = 0U;             // Cluster ID (wildcard is explicit)
  uint16_t endpointId = 0U;            // Endpoint ID; wildcard is explicit
  AclPrivilege privilege = AclPrivilege::kNone;
  bool wildcardFabric = false;          // Apply to all fabrics
  bool wildcardNode = false;           // Apply to all nodes in fabric
  bool wildcardSubject = false;        // Apply to all subjects
  bool wildcardCluster = false;        // Apply to all clusters
  bool wildcardEndpoint = false;       // Apply to all endpoints
  bool valid = false;
};

class MatterAccessControl {
 public:
  MatterAccessControl() = default;

  // Add an ACL entry
  bool addEntry(const AclEntry& entry);

  // Remove an ACL entry
  bool removeEntry(uint8_t index);

  // Check if a subject has access to a cluster/endpoint
  bool checkAccess(const uint8_t subjectId[8],
                   const uint8_t fabricId[8],
                   const uint8_t nodeId[8],
                   uint32_t clusterId,
                   uint16_t endpointId,
                   AclPrivilege requiredPrivilege) const;

  // Get number of entries
  uint8_t entryCount() const { return entryCount_; }

  // Get an entry by index
  bool getEntry(uint8_t index, AclEntry* outEntry) const;

  // Clear all entries
  void clear();

  // Add a default entry (all fabrics, all nodes, view access to all clusters)
  bool addDefaultViewEntry();

  // Add an entry for a specific node to operate on all clusters
  bool addNodeOperateEntry(const uint8_t nodeId[8],
                           const uint8_t fabricId[8]);

  // Get privilege name
  static const char* privilegeName(AclPrivilege privilege);

 private:
  AclEntry entries_[kAclMaxEntries];
  uint8_t entryCount_ = 0U;

  bool matchesEntry(const AclEntry& entry,
                    const uint8_t subjectId[8],
                    const uint8_t fabricId[8],
                    const uint8_t nodeId[8],
                    uint32_t clusterId,
                    uint16_t endpointId) const;
};

}  // namespace xiao_nrf54l15
