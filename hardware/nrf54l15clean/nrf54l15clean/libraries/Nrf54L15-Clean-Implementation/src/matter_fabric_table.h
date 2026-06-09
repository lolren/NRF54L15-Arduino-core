#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace xiao_nrf54l15 {

// Matter Fabric Table Implementation
// Stores up to kMaxFabrics configured fabrics
// Each fabric has a unique fabric ID, node ID, and root key

constexpr uint8_t kMaxFabrics = 4U;
constexpr uint8_t kFabricLabelSize = 32U;

struct FabricEntry {
  uint8_t fabricId[8] = {0};      // Unique fabric identifier
  uint8_t nodeId[8] = {0};         // Node's ID within this fabric
  uint8_t rootPublicKey[65] = {0}; // Fabric root public key (uncompressed)
  char label[kFabricLabelSize] = {0}; // Human-readable label
  bool valid = false;
  bool isLocalFabric = false;      // True if this is a self-generated fabric
};

class MatterFabricTable {
 public:
  MatterFabricTable() = default;

  // Add a fabric entry
  uint8_t addFabric(const uint8_t fabricId[8],
                    const uint8_t nodeId[8],
                    const uint8_t rootPublicKey[65] = nullptr,
                    const char* label = nullptr,
                    bool isLocalFabric = false);

  // Remove a fabric by index
  bool removeFabric(uint8_t fabricIndex);

  // Find fabric by fabric ID
  int8_t findFabric(const uint8_t fabricId[8]) const;

  // Find fabric by node ID
  int8_t findFabricByNode(const uint8_t nodeId[8]) const;

  // Get fabric entry
  bool getFabric(uint8_t fabricIndex, FabricEntry* outEntry) const;

  // Get number of fabrics
  uint8_t fabricCount() const { return fabricCount_; }

  // Get next available fabric index
  uint8_t nextFabricIndex() const;

  // Check if fabric is full
  bool isFull() const { return fabricCount_ >= kMaxFabrics; }

  // Clear all fabrics
  void clear();

  // Get fabric ID for a node (Matter: node ID determines fabric)
  const uint8_t* findFabricIdForNode(const uint8_t nodeId[8]) const;

  // Add a default test fabric (for testing without CASE)
  uint8_t addTestFabric(uint8_t fabricIndex, const char* label);

  // Check if any fabric is configured
  bool hasFabrics() const { return fabricCount_ > 0U; }

 private:
  FabricEntry entries_[kMaxFabrics];
  uint8_t fabricCount_ = 0U;
};

}  // namespace xiao_nrf54l15