#pragma once

#include <stdint.h>

#include "zigbee_persistence.h"

namespace xiao_nrf54l15 {

// Persistent-state flag bits that identify the corresponding counter field as
// an exclusive, crash-safe high-water mark rather than a next-counter value.
constexpr uint8_t kZigbeePersistentFlagNwkCounterHighWater = 0x10U;
constexpr uint8_t kZigbeePersistentFlagApsCounterHighWater = 0x20U;

enum class ZigbeeOutgoingCounterDomain : uint8_t {
  kNwk = 0U,
  kApsTrustCenter = 1U,
};

// Allocates outgoing security frame counters from ranges whose exclusive end
// was durably committed before any member of the range can be returned. A
// reboot resumes at the persisted exclusive end, deliberately abandoning any
// unused counters from the previous boot.
class ZigbeeOutgoingFrameCounterAllocator {
 public:
  static constexpr uint32_t kReservationBlockSize = 64U;

  ZigbeeOutgoingFrameCounterAllocator();

  // Restores domains whose fields carry high-water semantics. A legacy field
  // without the corresponding flag remains disabled and therefore fails
  // closed; its old next-counter value cannot prove that no later value was
  // transmitted before the last reset.
  void restore(const ZigbeePersistentState& state);
  void disable(ZigbeeOutgoingCounterDomain domain);

  // Starts a fresh counter domain after installing a genuinely new key. The
  // first allocate() still commits its high-water mark before returning 1.
  void resetForNewKey(ZigbeeOutgoingCounterDomain domain,
                      uint32_t firstCounter = 1U);

  // The store must already contain a valid application snapshot. On range
  // exhaustion allocate() reloads that snapshot, advances only the selected
  // high-water mark, commits it through the crash-safe journal, and returns a
  // counter only after save() has verified the commit.
  bool allocate(ZigbeeOutgoingCounterDomain domain,
                ZigbeePersistentStateStore* store, uint32_t* outCounter);

  // Call immediately before every ordinary application-state save so a UI,
  // binding, or reporting update cannot overwrite a previously reserved end.
  void stampPersistentState(ZigbeePersistentState* state) const;

  bool enabled(ZigbeeOutgoingCounterDomain domain) const;
  uint32_t nextCounter(ZigbeeOutgoingCounterDomain domain) const;
  uint32_t highWater(ZigbeeOutgoingCounterDomain domain) const;

 private:
  struct DomainState {
    uint32_t next = 1U;
    uint32_t highWater = 1U;
    bool enabled = false;
  };

  DomainState* domainState(ZigbeeOutgoingCounterDomain domain);
  const DomainState* domainState(ZigbeeOutgoingCounterDomain domain) const;
  static uint8_t persistentFlag(ZigbeeOutgoingCounterDomain domain);
  static uint32_t persistentHighWater(
      const ZigbeePersistentState& state,
      ZigbeeOutgoingCounterDomain domain);
  static void setPersistentHighWater(ZigbeePersistentState* state,
                                     ZigbeeOutgoingCounterDomain domain,
                                     uint32_t highWater);

  DomainState nwk_{};
  DomainState apsTrustCenter_{};
  bool allocating_ = false;
};

}  // namespace xiao_nrf54l15
