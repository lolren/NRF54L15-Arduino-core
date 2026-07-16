#pragma once

#include <Preferences.h>

#include "zigbee_stack.h"

namespace xiao_nrf54l15 {

struct ZigbeePersistentPeerSecurityState {
  bool used = false;
  uint64_t ieeeAddress = 0U;
  uint16_t shortAddress = 0U;
  uint32_t incomingNwkFrameCounter = 0U;
  uint8_t networkKeySequence = 0U;
};

struct ZigbeePersistentState {
  uint32_t magic = 0U;
  uint16_t version = 0U;
  uint8_t channel = 0U;
  uint8_t logicalType = 0U;
  uint16_t panId = 0U;
  uint16_t nwkAddress = 0U;
  uint16_t parentShort = 0U;
  uint16_t manufacturerCode = 0U;
  uint64_t ieeeAddress = 0U;
  uint64_t extendedPanId = 0U;
  uint8_t networkKey[16] = {0};
  uint32_t nwkFrameCounter = 0U;
  uint32_t apsFrameCounter = 0U;
  uint8_t keySequence = 0U;
  uint8_t alternateNetworkKey[16] = {0};
  uint8_t alternateKeySequence = 0U;
  uint8_t flags = 0U;
  uint8_t preconfiguredKeyMode = 0U;
  bool onOffState = false;
  uint8_t levelState = 0U;
  uint64_t trustCenterIeee = 0U;
  uint8_t reportingCount = 0U;
  ZigbeeReportingConfiguration reporting[8] = {};
  uint8_t bindingCount = 0U;
  ZigbeeBindingEntry bindings[8] = {};
  uint32_t incomingNwkFrameCounter = 0U;
  uint32_t incomingApsFrameCounter = 0U;
  // Coordinator replay state. A Trust Center must not forget a child's last
  // authenticated NWK counter across reset and then accept an old frame.
  ZigbeePersistentPeerSecurityState peerSecurity[8] = {};
};

enum class ZigbeePersistentLoadStatus : uint8_t {
  kLive = 0U,
  kEmpty = 1U,
  kTombstone = 2U,
  kCorrupt = 3U,
  kNamespaceCollision = 4U,
  kNotOpen = 5U,
};

class ZigbeePersistentStateStore {
 public:
  ZigbeePersistentStateStore();

  // The 4 KB journal has two hashed namespace partitions. Each partition owns
  // one A/B pair; save/clear fail rather than evict a colliding namespace.
  bool begin(const char* name = "zigbee");
  void end();
  bool load(ZigbeePersistentState* outState);
  // Strict status-bearing load for security state. Unlike load(), a corrupt
  // direct-journal history is never treated as an empty first boot and never
  // falls back to a legacy record.
  ZigbeePersistentLoadStatus loadWithStatus(
      ZigbeePersistentState* outState);
  // Crash-safe snapshot commit. This API does not itself reserve future NWK
  // or APS counter ranges; the security layer must reserve before first use.
  bool save(const ZigbeePersistentState& state);
  // Atomically advances one exclusive counter high-water mark in the newest
  // live snapshot. The RRAM lock is held across read, compare, commit, and
  // readback so an ordinary save cannot interleave and roll the reservation
  // backward. `persistentFlag` is the domain's high-water semantics bit.
  bool reserveOutgoingFrameCounterRange(uint8_t persistentFlag,
                                        bool apsTrustCenter,
                                        uint32_t expectedHighWater,
                                        uint32_t reservedHighWater);
  bool clear();

  static void initialize(ZigbeePersistentState* state);
  static bool isValid(const ZigbeePersistentState& state);

 private:
  Preferences prefs_;
  bool open_;
  bool legacyPrefsOpen_;
  uint8_t namespaceLength_;
  char namespaceName_[16];
  uint32_t namespaceHash_;
};

}  // namespace xiao_nrf54l15
