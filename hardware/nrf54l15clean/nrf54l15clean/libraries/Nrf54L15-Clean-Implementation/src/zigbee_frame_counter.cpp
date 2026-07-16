#include "zigbee_feature.h"

#if NRF54L15_CLEAN_ZIGBEE_AVAILABLE
#include "zigbee_frame_counter.h"

#include <limits.h>

namespace xiao_nrf54l15 {

ZigbeeOutgoingFrameCounterAllocator::ZigbeeOutgoingFrameCounterAllocator() =
    default;

ZigbeeOutgoingFrameCounterAllocator::DomainState*
ZigbeeOutgoingFrameCounterAllocator::domainState(
    ZigbeeOutgoingCounterDomain domain) {
  switch (domain) {
    case ZigbeeOutgoingCounterDomain::kNwk:
      return &nwk_;
    case ZigbeeOutgoingCounterDomain::kApsTrustCenter:
      return &apsTrustCenter_;
  }
  return nullptr;
}

const ZigbeeOutgoingFrameCounterAllocator::DomainState*
ZigbeeOutgoingFrameCounterAllocator::domainState(
    ZigbeeOutgoingCounterDomain domain) const {
  switch (domain) {
    case ZigbeeOutgoingCounterDomain::kNwk:
      return &nwk_;
    case ZigbeeOutgoingCounterDomain::kApsTrustCenter:
      return &apsTrustCenter_;
  }
  return nullptr;
}

uint8_t ZigbeeOutgoingFrameCounterAllocator::persistentFlag(
    ZigbeeOutgoingCounterDomain domain) {
  switch (domain) {
    case ZigbeeOutgoingCounterDomain::kNwk:
      return kZigbeePersistentFlagNwkCounterHighWater;
    case ZigbeeOutgoingCounterDomain::kApsTrustCenter:
      return kZigbeePersistentFlagApsCounterHighWater;
  }
  return 0U;
}

uint32_t ZigbeeOutgoingFrameCounterAllocator::persistentHighWater(
    const ZigbeePersistentState& state,
    ZigbeeOutgoingCounterDomain domain) {
  switch (domain) {
    case ZigbeeOutgoingCounterDomain::kNwk:
      return state.nwkFrameCounter;
    case ZigbeeOutgoingCounterDomain::kApsTrustCenter:
      return state.apsFrameCounter;
  }
  return 0U;
}

void ZigbeeOutgoingFrameCounterAllocator::setPersistentHighWater(
    ZigbeePersistentState* state, ZigbeeOutgoingCounterDomain domain,
    uint32_t highWater) {
  if (state == nullptr) {
    return;
  }
  switch (domain) {
    case ZigbeeOutgoingCounterDomain::kNwk:
      state->nwkFrameCounter = highWater;
      return;
    case ZigbeeOutgoingCounterDomain::kApsTrustCenter:
      state->apsFrameCounter = highWater;
      return;
  }
}

void ZigbeeOutgoingFrameCounterAllocator::restore(
    const ZigbeePersistentState& state) {
  static constexpr ZigbeeOutgoingCounterDomain kDomains[] = {
      ZigbeeOutgoingCounterDomain::kNwk,
      ZigbeeOutgoingCounterDomain::kApsTrustCenter};
  for (const ZigbeeOutgoingCounterDomain domain : kDomains) {
    DomainState* const current = domainState(domain);
    const uint8_t flag = persistentFlag(domain);
    const uint32_t highWater = persistentHighWater(state, domain);
    if (current == nullptr || flag == 0U || (state.flags & flag) == 0U ||
        highWater == 0U) {
      disable(domain);
      continue;
    }

    // Everything below the committed exclusive end may have left the radio
    // before reset, so none of it can be returned in this boot.
    current->next = highWater;
    current->highWater = highWater;
    current->enabled = true;
  }
}

void ZigbeeOutgoingFrameCounterAllocator::disable(
    ZigbeeOutgoingCounterDomain domain) {
  DomainState* const current = domainState(domain);
  if (current == nullptr) {
    return;
  }
  *current = DomainState{};
}

void ZigbeeOutgoingFrameCounterAllocator::resetForNewKey(
    ZigbeeOutgoingCounterDomain domain, uint32_t firstCounter) {
  DomainState* const current = domainState(domain);
  if (current == nullptr || firstCounter == 0U || firstCounter == UINT32_MAX) {
    disable(domain);
    return;
  }
  current->next = firstCounter;
  current->highWater = firstCounter;
  current->enabled = true;
}

bool ZigbeeOutgoingFrameCounterAllocator::allocate(
    ZigbeeOutgoingCounterDomain domain, ZigbeePersistentStateStore* store,
    uint32_t* outCounter) {
  if (outCounter != nullptr) {
    *outCounter = 0U;
  }
  DomainState* const current = domainState(domain);
  if (current == nullptr || store == nullptr || outCounter == nullptr ||
      !current->enabled || allocating_) {
    return false;
  }

  if (current->next >= current->highWater) {
    // UINT32_MAX is the largest representable exclusive end. Reserving up to
    // it permits UINT32_MAX - 1 as the final counter and deliberately leaves
    // UINT32_MAX unused rather than wrapping an unrepresentable 2^32 end.
    if (current->next > UINT32_MAX - kReservationBlockSize) {
      return false;
    }
    const uint32_t reservedHighWater =
        current->next + kReservationBlockSize;

    allocating_ = true;
    const bool committed = store->reserveOutgoingFrameCounterRange(
        persistentFlag(domain),
        domain == ZigbeeOutgoingCounterDomain::kApsTrustCenter,
        current->highWater, reservedHighWater);
    allocating_ = false;
    if (!committed) {
      return false;
    }
    current->highWater = reservedHighWater;
  }

  if (current->next >= current->highWater ||
      current->next == UINT32_MAX) {
    return false;
  }
  *outCounter = current->next;
  ++current->next;
  return true;
}

void ZigbeeOutgoingFrameCounterAllocator::stampPersistentState(
    ZigbeePersistentState* state) const {
  if (state == nullptr) {
    return;
  }
  static constexpr ZigbeeOutgoingCounterDomain kDomains[] = {
      ZigbeeOutgoingCounterDomain::kNwk,
      ZigbeeOutgoingCounterDomain::kApsTrustCenter};
  for (const ZigbeeOutgoingCounterDomain domain : kDomains) {
    const DomainState* const current = domainState(domain);
    const uint8_t flag = persistentFlag(domain);
    if (current == nullptr || flag == 0U || !current->enabled) {
      state->flags = static_cast<uint8_t>(state->flags & ~flag);
      continue;
    }
    setPersistentHighWater(state, domain, current->highWater);
    state->flags = static_cast<uint8_t>(state->flags | flag);
  }
}

bool ZigbeeOutgoingFrameCounterAllocator::enabled(
    ZigbeeOutgoingCounterDomain domain) const {
  const DomainState* const current = domainState(domain);
  return current != nullptr && current->enabled;
}

uint32_t ZigbeeOutgoingFrameCounterAllocator::nextCounter(
    ZigbeeOutgoingCounterDomain domain) const {
  const DomainState* const current = domainState(domain);
  return (current != nullptr) ? current->next : 0U;
}

uint32_t ZigbeeOutgoingFrameCounterAllocator::highWater(
    ZigbeeOutgoingCounterDomain domain) const {
  const DomainState* const current = domainState(domain);
  return (current != nullptr) ? current->highWater : 0U;
}

}  // namespace xiao_nrf54l15
#endif  // NRF54L15_CLEAN_ZIGBEE_AVAILABLE
