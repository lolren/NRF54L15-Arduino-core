#include "zigbee_feature.h"

#if NRF54L15_CLEAN_ZIGBEE_AVAILABLE
#include "zigbee_persistence.h"

#include <nrf54l15.h>

#include <stdio.h>
#include <string.h>
#include <type_traits>

#if !NRF54_RRAMC_DIRECT_ACCESS_AVAILABLE
#error "Zigbee persistence requires secure RRAMC access"
#endif

extern "C" bool nrf54l15_rram_transaction_try_lock(void);
extern "C" void nrf54l15_rram_transaction_unlock(void);

namespace xiao_nrf54l15 {

namespace {

constexpr uint32_t kZigbeeStateMagic = 0x5A425330UL;
constexpr uint16_t kZigbeeStateVersion = 7U;
constexpr char kPrefsKeyLegacyState[] = "state";
constexpr char kPrefsKeyLegacyStateLen[] = "stlen";
constexpr char kPrefsKeyLegacyStateChunkPrefix[] = "st";
constexpr size_t kPrefsChunkValueLen = 48U;
constexpr size_t kPrefsChunkKeyCapacity = 5U;
constexpr size_t kPrefsMaxLegacyChunkCount = 27U;

constexpr uint32_t kJournalMetadataMagic = 0x5A424A52UL;  // "ZBJR"
constexpr uint16_t kJournalFormatVersion = 1U;
constexpr uint16_t kJournalPayloadVersion = 1U;
constexpr uint32_t kJournalCommitMarker = 0x434F4D54UL;  // "COMT"
constexpr uint32_t kJournalStateLive = 1U;
constexpr uint32_t kJournalStateTombstone = 2U;
constexpr size_t kJournalPageLen = 4096U;
constexpr size_t kJournalPartitionCount = 2U;
constexpr size_t kJournalSlotsPerPartition = 2U;
constexpr size_t kJournalSlotCount =
    kJournalPartitionCount * kJournalSlotsPerPartition;
constexpr size_t kJournalSlotLen = kJournalPageLen / kJournalSlotCount;
constexpr size_t kJournalHeaderLen = 64U;
constexpr size_t kJournalCommitOffset = 48U;
constexpr size_t kMaxJournalPayloadLen = 512U;
constexpr size_t kSerializedJournalPayloadLenV6 = 336U;
constexpr size_t kSerializedJournalPayloadLen = 464U;
constexpr size_t kJournalNamespaceCapacity = 15U;
constexpr uint32_t kRramcSpinLimit = 600000UL;

static_assert(kJournalHeaderLen + kMaxJournalPayloadLen <= kJournalSlotLen,
              "Zigbee journal payload exceeds a fixed slot");
static_assert(kSerializedJournalPayloadLen <= kMaxJournalPayloadLen,
              "Zigbee state encoding exceeds its journal payload budget");

__attribute__((section(".zigbee_storage"), aligned(16)))
volatile uint8_t g_zigbeeStoragePage[kJournalPageLen];

static_assert(std::is_trivially_copyable<ZigbeePersistentState>::value,
              "Zigbee persistence requires a trivially copyable state blob");

uint32_t crc32(const uint8_t* data, size_t len) {
  if (data == nullptr) {
    return 0U;
  }
  uint32_t crc = 0xFFFFFFFFUL;
  for (size_t i = 0U; i < len; ++i) {
    crc ^= static_cast<uint32_t>(data[i]);
    for (uint8_t bit = 0U; bit < 8U; ++bit) {
      const uint32_t mask =
          (crc & 1U) != 0U ? 0xFFFFFFFFUL : 0U;
      crc = (crc >> 1U) ^ (0xEDB88320UL & mask);
    }
  }
  return ~crc;
}

void writeLe16(uint8_t* dst, uint16_t value) {
  dst[0] = static_cast<uint8_t>(value & 0xFFU);
  dst[1] = static_cast<uint8_t>((value >> 8U) & 0xFFU);
}

void writeLe32(uint8_t* dst, uint32_t value) {
  dst[0] = static_cast<uint8_t>(value & 0xFFU);
  dst[1] = static_cast<uint8_t>((value >> 8U) & 0xFFU);
  dst[2] = static_cast<uint8_t>((value >> 16U) & 0xFFU);
  dst[3] = static_cast<uint8_t>((value >> 24U) & 0xFFU);
}

uint16_t readLe16(const uint8_t* src) {
  return static_cast<uint16_t>(src[0]) |
         (static_cast<uint16_t>(src[1]) << 8U);
}

uint32_t readLe32(const uint8_t* src) {
  return static_cast<uint32_t>(src[0]) |
         (static_cast<uint32_t>(src[1]) << 8U) |
         (static_cast<uint32_t>(src[2]) << 16U) |
         (static_cast<uint32_t>(src[3]) << 24U);
}

class ByteWriter {
 public:
  ByteWriter(uint8_t* data, size_t capacity)
      : data_(data), capacity_(capacity), offset_(0U), valid_(data != nullptr) {}

  void putU8(uint8_t value) {
    if (!reserve(1U)) {
      return;
    }
    data_[offset_++] = value;
  }

  void putU16(uint16_t value) {
    if (!reserve(2U)) {
      return;
    }
    writeLe16(data_ + offset_, value);
    offset_ += 2U;
  }

  void putU32(uint32_t value) {
    if (!reserve(4U)) {
      return;
    }
    writeLe32(data_ + offset_, value);
    offset_ += 4U;
  }

  void putU64(uint64_t value) {
    putU32(static_cast<uint32_t>(value & 0xFFFFFFFFULL));
    putU32(static_cast<uint32_t>(value >> 32U));
  }

  void putBytes(const uint8_t* value, size_t len) {
    if (value == nullptr || !reserve(len)) {
      return;
    }
    memcpy(data_ + offset_, value, len);
    offset_ += len;
  }

  bool valid() const { return valid_; }
  size_t size() const { return offset_; }

 private:
  bool reserve(size_t len) {
    if (!valid_ || len > capacity_ - offset_) {
      valid_ = false;
      return false;
    }
    return true;
  }

  uint8_t* data_;
  size_t capacity_;
  size_t offset_;
  bool valid_;
};

class ByteReader {
 public:
  ByteReader(const uint8_t* data, size_t len)
      : data_(data), len_(len), offset_(0U), valid_(data != nullptr) {}

  bool getU8(uint8_t* value) {
    if (value == nullptr || !reserve(1U)) {
      return false;
    }
    *value = data_[offset_++];
    return true;
  }

  bool getU16(uint16_t* value) {
    if (value == nullptr || !reserve(2U)) {
      return false;
    }
    *value = readLe16(data_ + offset_);
    offset_ += 2U;
    return true;
  }

  bool getU32(uint32_t* value) {
    if (value == nullptr || !reserve(4U)) {
      return false;
    }
    *value = readLe32(data_ + offset_);
    offset_ += 4U;
    return true;
  }

  bool getU64(uint64_t* value) {
    if (value == nullptr) {
      valid_ = false;
      return false;
    }
    uint32_t low = 0U;
    uint32_t high = 0U;
    if (!getU32(&low) || !getU32(&high)) {
      return false;
    }
    *value = static_cast<uint64_t>(low) |
             (static_cast<uint64_t>(high) << 32U);
    return true;
  }

  bool getBytes(uint8_t* value, size_t len) {
    if (value == nullptr || !reserve(len)) {
      return false;
    }
    memcpy(value, data_ + offset_, len);
    offset_ += len;
    return true;
  }

  bool complete() const { return valid_ && offset_ == len_; }

 private:
  bool reserve(size_t len) {
    if (!valid_ || len > len_ - offset_) {
      valid_ = false;
      return false;
    }
    return true;
  }

  const uint8_t* data_;
  size_t len_;
  size_t offset_;
  bool valid_;
};

void resetPersistentState(ZigbeePersistentState* state) {
  if (state == nullptr) {
    return;
  }

  // The full object representation is persisted, so keep padding deterministic
  // before restoring the nested records' nonzero typed defaults.
  memset(static_cast<void*>(state), 0, sizeof(*state));
  for (ZigbeeReportingConfiguration& reporting : state->reporting) {
    reporting = ZigbeeReportingConfiguration{};
  }
  for (ZigbeeBindingEntry& binding : state->bindings) {
    binding = ZigbeeBindingEntry{};
  }
}

void copyPersistentStateMembers(const ZigbeePersistentState& source,
                                ZigbeePersistentState* destination) {
  if (destination == nullptr) {
    return;
  }
  resetPersistentState(destination);
  destination->magic = source.magic;
  destination->version = source.version;
  destination->channel = source.channel;
  destination->logicalType = source.logicalType;
  destination->panId = source.panId;
  destination->nwkAddress = source.nwkAddress;
  destination->parentShort = source.parentShort;
  destination->manufacturerCode = source.manufacturerCode;
  destination->ieeeAddress = source.ieeeAddress;
  destination->extendedPanId = source.extendedPanId;
  memcpy(destination->networkKey, source.networkKey,
         sizeof(destination->networkKey));
  destination->nwkFrameCounter = source.nwkFrameCounter;
  destination->apsFrameCounter = source.apsFrameCounter;
  destination->keySequence = source.keySequence;
  memcpy(destination->alternateNetworkKey, source.alternateNetworkKey,
         sizeof(destination->alternateNetworkKey));
  destination->alternateKeySequence = source.alternateKeySequence;
  destination->flags = source.flags;
  destination->preconfiguredKeyMode = source.preconfiguredKeyMode;
  destination->onOffState = source.onOffState;
  destination->levelState = source.levelState;
  destination->trustCenterIeee = source.trustCenterIeee;
  destination->reportingCount = source.reportingCount;
  for (size_t i = 0U;
       i < sizeof(destination->reporting) / sizeof(destination->reporting[0]);
       ++i) {
    ZigbeeReportingConfiguration& dst = destination->reporting[i];
    const ZigbeeReportingConfiguration& src = source.reporting[i];
    dst.used = src.used;
    dst.clusterId = src.clusterId;
    dst.attributeId = src.attributeId;
    dst.dataType = src.dataType;
    dst.minimumIntervalSeconds = src.minimumIntervalSeconds;
    dst.maximumIntervalSeconds = src.maximumIntervalSeconds;
    dst.reportableChange = src.reportableChange;
  }
  destination->bindingCount = source.bindingCount;
  for (size_t i = 0U;
       i < sizeof(destination->bindings) / sizeof(destination->bindings[0]);
       ++i) {
    ZigbeeBindingEntry& dst = destination->bindings[i];
    const ZigbeeBindingEntry& src = source.bindings[i];
    dst.used = src.used;
    dst.sourceEndpoint = src.sourceEndpoint;
    dst.clusterId = src.clusterId;
    dst.destinationAddressMode = src.destinationAddressMode;
    dst.destinationGroup = src.destinationGroup;
    dst.destinationIeee = src.destinationIeee;
    dst.destinationEndpoint = src.destinationEndpoint;
  }
  destination->incomingNwkFrameCounter = source.incomingNwkFrameCounter;
  destination->incomingApsFrameCounter = source.incomingApsFrameCounter;
  for (size_t i = 0U;
       i < sizeof(destination->peerSecurity) /
               sizeof(destination->peerSecurity[0]);
       ++i) {
    destination->peerSecurity[i].used = source.peerSecurity[i].used;
    destination->peerSecurity[i].ieeeAddress =
        source.peerSecurity[i].ieeeAddress;
    destination->peerSecurity[i].shortAddress =
        source.peerSecurity[i].shortAddress;
    destination->peerSecurity[i].incomingNwkFrameCounter =
        source.peerSecurity[i].incomingNwkFrameCounter;
    destination->peerSecurity[i].networkKeySequence =
        source.peerSecurity[i].networkKeySequence;
  }
}

bool serializePersistentState(const ZigbeePersistentState& state,
                              uint8_t* payload, size_t payloadCapacity,
                              size_t* payloadLen) {
  if (payload == nullptr || payloadLen == nullptr) {
    return false;
  }
  *payloadLen = 0U;
  ZigbeePersistentState canonical{};
  copyPersistentStateMembers(state, &canonical);

  ByteWriter writer(payload, payloadCapacity);
  writer.putU32(canonical.magic);
  writer.putU16(canonical.version);
  writer.putU8(canonical.channel);
  writer.putU8(canonical.logicalType);
  writer.putU16(canonical.panId);
  writer.putU16(canonical.nwkAddress);
  writer.putU16(canonical.parentShort);
  writer.putU16(canonical.manufacturerCode);
  writer.putU64(canonical.ieeeAddress);
  writer.putU64(canonical.extendedPanId);
  writer.putBytes(canonical.networkKey, sizeof(canonical.networkKey));
  writer.putU32(canonical.nwkFrameCounter);
  writer.putU32(canonical.apsFrameCounter);
  writer.putU8(canonical.keySequence);
  writer.putBytes(canonical.alternateNetworkKey,
                  sizeof(canonical.alternateNetworkKey));
  writer.putU8(canonical.alternateKeySequence);
  writer.putU8(canonical.flags);
  writer.putU8(canonical.preconfiguredKeyMode);
  writer.putU8(canonical.onOffState ? 1U : 0U);
  writer.putU8(canonical.levelState);
  writer.putU64(canonical.trustCenterIeee);
  writer.putU8(canonical.reportingCount);
  for (const ZigbeeReportingConfiguration& reporting : canonical.reporting) {
    writer.putU8(reporting.used ? 1U : 0U);
    writer.putU16(reporting.clusterId);
    writer.putU16(reporting.attributeId);
    writer.putU8(static_cast<uint8_t>(reporting.dataType));
    writer.putU16(reporting.minimumIntervalSeconds);
    writer.putU16(reporting.maximumIntervalSeconds);
    writer.putU32(reporting.reportableChange);
  }
  writer.putU8(canonical.bindingCount);
  for (const ZigbeeBindingEntry& binding : canonical.bindings) {
    writer.putU8(binding.used ? 1U : 0U);
    writer.putU8(binding.sourceEndpoint);
    writer.putU16(binding.clusterId);
    writer.putU8(static_cast<uint8_t>(binding.destinationAddressMode));
    writer.putU16(binding.destinationGroup);
    writer.putU64(binding.destinationIeee);
    writer.putU8(binding.destinationEndpoint);
  }
  writer.putU32(canonical.incomingNwkFrameCounter);
  writer.putU32(canonical.incomingApsFrameCounter);
  for (const ZigbeePersistentPeerSecurityState& peer :
       canonical.peerSecurity) {
    writer.putU8(peer.used ? 1U : 0U);
    writer.putU64(peer.ieeeAddress);
    writer.putU16(peer.shortAddress);
    writer.putU32(peer.incomingNwkFrameCounter);
    writer.putU8(peer.networkKeySequence);
  }
  if (!writer.valid() || writer.size() != kSerializedJournalPayloadLen) {
    return false;
  }
  *payloadLen = writer.size();
  return true;
}

bool deserializePersistentState(const uint8_t* payload, size_t payloadLen,
                                ZigbeePersistentState* outState) {
  if (payload == nullptr ||
      (payloadLen != kSerializedJournalPayloadLenV6 &&
       payloadLen != kSerializedJournalPayloadLen) ||
      outState == nullptr) {
    return false;
  }

  ZigbeePersistentState parsed{};
  resetPersistentState(&parsed);
  ByteReader reader(payload, payloadLen);
  uint8_t rawBool = 0U;
  if (!reader.getU32(&parsed.magic) || !reader.getU16(&parsed.version) ||
      !reader.getU8(&parsed.channel) ||
      !reader.getU8(&parsed.logicalType) || !reader.getU16(&parsed.panId) ||
      !reader.getU16(&parsed.nwkAddress) ||
      !reader.getU16(&parsed.parentShort) ||
      !reader.getU16(&parsed.manufacturerCode) ||
      !reader.getU64(&parsed.ieeeAddress) ||
      !reader.getU64(&parsed.extendedPanId) ||
      !reader.getBytes(parsed.networkKey, sizeof(parsed.networkKey)) ||
      !reader.getU32(&parsed.nwkFrameCounter) ||
      !reader.getU32(&parsed.apsFrameCounter) ||
      !reader.getU8(&parsed.keySequence) ||
      !reader.getBytes(parsed.alternateNetworkKey,
                       sizeof(parsed.alternateNetworkKey)) ||
      !reader.getU8(&parsed.alternateKeySequence) ||
      !reader.getU8(&parsed.flags) ||
      !reader.getU8(&parsed.preconfiguredKeyMode) ||
      !reader.getU8(&rawBool) || rawBool > 1U) {
    return false;
  }
  parsed.onOffState = rawBool != 0U;
  if (!reader.getU8(&parsed.levelState) ||
      !reader.getU64(&parsed.trustCenterIeee) ||
      !reader.getU8(&parsed.reportingCount)) {
    return false;
  }
  for (ZigbeeReportingConfiguration& reporting : parsed.reporting) {
    uint8_t rawType = 0U;
    if (!reader.getU8(&rawBool) || rawBool > 1U ||
        !reader.getU16(&reporting.clusterId) ||
        !reader.getU16(&reporting.attributeId) ||
        !reader.getU8(&rawType) ||
        !reader.getU16(&reporting.minimumIntervalSeconds) ||
        !reader.getU16(&reporting.maximumIntervalSeconds) ||
        !reader.getU32(&reporting.reportableChange)) {
      return false;
    }
    reporting.used = rawBool != 0U;
    reporting.dataType = static_cast<ZigbeeZclDataType>(rawType);
  }
  if (!reader.getU8(&parsed.bindingCount)) {
    return false;
  }
  for (ZigbeeBindingEntry& binding : parsed.bindings) {
    uint8_t rawAddressMode = 0U;
    if (!reader.getU8(&rawBool) || rawBool > 1U ||
        !reader.getU8(&binding.sourceEndpoint) ||
        !reader.getU16(&binding.clusterId) ||
        !reader.getU8(&rawAddressMode) ||
        !reader.getU16(&binding.destinationGroup) ||
        !reader.getU64(&binding.destinationIeee) ||
        !reader.getU8(&binding.destinationEndpoint)) {
      return false;
    }
    binding.used = rawBool != 0U;
    binding.destinationAddressMode =
        static_cast<ZigbeeBindingAddressMode>(rawAddressMode);
  }
  if (!reader.getU32(&parsed.incomingNwkFrameCounter) ||
      !reader.getU32(&parsed.incomingApsFrameCounter)) {
    return false;
  }
  if (payloadLen == kSerializedJournalPayloadLenV6) {
    if (parsed.version != 6U || !reader.complete()) {
      return false;
    }
    // Version 6 predates per-peer Trust Center replay persistence. Upgrade the
    // validated base state with an empty peer table; peers must rejoin before
    // their secured traffic is accepted.
    parsed.version = kZigbeeStateVersion;
  } else {
    if (parsed.version != kZigbeeStateVersion) {
      return false;
    }
    for (ZigbeePersistentPeerSecurityState& peer : parsed.peerSecurity) {
      if (!reader.getU8(&rawBool) || rawBool > 1U ||
          !reader.getU64(&peer.ieeeAddress) ||
          !reader.getU16(&peer.shortAddress) ||
          !reader.getU32(&peer.incomingNwkFrameCounter) ||
          !reader.getU8(&peer.networkKeySequence)) {
        return false;
      }
      peer.used = rawBool != 0U;
    }
    if (!reader.complete()) {
      return false;
    }
  }
  if (!ZigbeePersistentStateStore::isValid(parsed)) {
    return false;
  }
  copyPersistentStateMembers(parsed, outState);
  return true;
}

struct ZigbeePersistentStateV1 {
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
  uint8_t flags = 0U;
  bool onOffState = false;
  uint8_t reportingCount = 0U;
  ZigbeeReportingConfiguration reporting[8] = {};
};

struct ZigbeePersistentStateV2 {
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
  uint8_t flags = 0U;
  bool onOffState = false;
  uint8_t levelState = 0U;
  uint8_t reportingCount = 0U;
  ZigbeeReportingConfiguration reporting[8] = {};
};

struct ZigbeePersistentStateV3 {
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
  uint8_t flags = 0U;
  bool onOffState = false;
  uint8_t levelState = 0U;
  uint8_t reportingCount = 0U;
  ZigbeeReportingConfiguration reporting[8] = {};
  uint8_t bindingCount = 0U;
  ZigbeeBindingEntry bindings[8] = {};
};

struct ZigbeePersistentStateV4 {
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
  uint8_t flags = 0U;
  bool onOffState = false;
  uint8_t levelState = 0U;
  uint8_t reportingCount = 0U;
  ZigbeeReportingConfiguration reporting[8] = {};
  uint8_t bindingCount = 0U;
  ZigbeeBindingEntry bindings[8] = {};
  uint32_t incomingNwkFrameCounter = 0U;
};

struct ZigbeePersistentStateV5 {
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
};

// Version 6 was the last Preferences/direct-journal representation before
// Trust Center peer replay state was added. Keep its native layout solely for
// one-time migration from cores that shipped that representation.
struct ZigbeePersistentStateV6 {
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
};

bool isValidV1(const ZigbeePersistentStateV1& state) {
  return state.magic == kZigbeeStateMagic &&
         state.version == 1U &&
         state.reportingCount <=
             static_cast<uint8_t>(sizeof(state.reporting) / sizeof(state.reporting[0]));
}

bool isValidV2(const ZigbeePersistentStateV2& state) {
  return state.magic == kZigbeeStateMagic &&
         state.version == 2U &&
         state.reportingCount <=
             static_cast<uint8_t>(sizeof(state.reporting) / sizeof(state.reporting[0]));
}

bool isValidV3(const ZigbeePersistentStateV3& state) {
  return state.magic == kZigbeeStateMagic &&
         state.version == 3U &&
         state.reportingCount <=
             static_cast<uint8_t>(sizeof(state.reporting) / sizeof(state.reporting[0])) &&
         state.bindingCount <=
             static_cast<uint8_t>(sizeof(state.bindings) / sizeof(state.bindings[0]));
}

bool isValidV4(const ZigbeePersistentStateV4& state) {
  return state.magic == kZigbeeStateMagic &&
         state.version == 4U &&
         state.reportingCount <=
             static_cast<uint8_t>(sizeof(state.reporting) /
                                  sizeof(state.reporting[0])) &&
         state.bindingCount <=
             static_cast<uint8_t>(sizeof(state.bindings) /
                                  sizeof(state.bindings[0]));
}

bool isValidV5(const ZigbeePersistentStateV5& state) {
  return state.magic == kZigbeeStateMagic &&
         state.version == 5U &&
         state.preconfiguredKeyMode <= 2U &&
         state.reportingCount <=
             static_cast<uint8_t>(sizeof(state.reporting) /
                                  sizeof(state.reporting[0])) &&
         state.bindingCount <=
             static_cast<uint8_t>(sizeof(state.bindings) /
                                  sizeof(state.bindings[0]));
}

bool isSupportedReportingDataType(ZigbeeZclDataType dataType);
bool isSupportedBindingAddressMode(ZigbeeBindingAddressMode addressMode);

bool isValidV6(const ZigbeePersistentStateV6& state) {
  if (state.magic != kZigbeeStateMagic || state.version != 6U ||
      state.logicalType > 2U ||
      (state.channel != 0U &&
       (state.channel < 11U || state.channel > 26U)) ||
      state.preconfiguredKeyMode > 2U ||
      state.reportingCount >
          static_cast<uint8_t>(sizeof(state.reporting) /
                               sizeof(state.reporting[0])) ||
      state.bindingCount >
          static_cast<uint8_t>(sizeof(state.bindings) /
                               sizeof(state.bindings[0]))) {
    return false;
  }
  for (const ZigbeeReportingConfiguration& reporting : state.reporting) {
    if (reporting.used && !isSupportedReportingDataType(reporting.dataType)) {
      return false;
    }
  }
  for (const ZigbeeBindingEntry& binding : state.bindings) {
    if (binding.used &&
        !isSupportedBindingAddressMode(binding.destinationAddressMode)) {
      return false;
    }
  }
  return true;
}

size_t chunkCountForLength(size_t len) {
  return (len + kPrefsChunkValueLen - 1U) / kPrefsChunkValueLen;
}

bool formatLegacyChunkKey(size_t chunkIndex,
                          char outKey[kPrefsChunkKeyCapacity]) {
  if (outKey == nullptr || chunkIndex > 0xFFU) {
    return false;
  }
  const int written =
      snprintf(outKey, kPrefsChunkKeyCapacity, "%s%02X",
               kPrefsKeyLegacyStateChunkPrefix,
               static_cast<unsigned>(chunkIndex));
  return written > 0 && static_cast<size_t>(written) < kPrefsChunkKeyCapacity;
}

bool removeIfPresent(Preferences* prefs, const char* key, bool* removedAny) {
  if (prefs == nullptr || key == nullptr) {
    return false;
  }
  if (!prefs->isKey(key)) {
    return true;
  }
  if (removedAny != nullptr) {
    *removedAny = true;
  }
  return prefs->remove(key) && !prefs->isKey(key);
}

bool clearLegacyStorage(Preferences* prefs, bool* removedAny) {
  if (prefs == nullptr) {
    return false;
  }
  bool ok = removeIfPresent(prefs, kPrefsKeyLegacyState, removedAny);
  ok = removeIfPresent(prefs, kPrefsKeyLegacyStateLen, removedAny) && ok;
  for (size_t i = 0U; i < kPrefsMaxLegacyChunkCount; ++i) {
    char key[kPrefsChunkKeyCapacity] = {0};
    if (!formatLegacyChunkKey(i, key)) {
      return false;
    }
    ok = removeIfPresent(prefs, key, removedAny) && ok;
  }
  return ok;
}

bool loadChunkedState(Preferences* prefs, ZigbeePersistentState* outState) {
  if (prefs == nullptr || outState == nullptr) {
    return false;
  }
  resetPersistentState(outState);

  const uint32_t storedLen = prefs->getUInt(kPrefsKeyLegacyStateLen, 0U);
  if (storedLen != sizeof(*outState)) {
    return false;
  }

  const size_t chunkCount = chunkCountForLength(storedLen);
  if (chunkCount == 0U || chunkCount > kPrefsMaxLegacyChunkCount) {
    return false;
  }

  ZigbeePersistentState parsed{};
  resetPersistentState(&parsed);
  uint8_t* dst = reinterpret_cast<uint8_t*>(&parsed);
  size_t offset = 0U;
  for (size_t i = 0U; i < chunkCount; ++i) {
    char key[kPrefsChunkKeyCapacity] = {0};
    if (!formatLegacyChunkKey(i, key)) {
      return false;
    }
    const size_t expectedLen =
        ((storedLen - offset) < kPrefsChunkValueLen) ? (storedLen - offset)
                                                     : kPrefsChunkValueLen;
    if (prefs->getBytesLength(key) != expectedLen ||
        prefs->getBytes(key, dst + offset, expectedLen) != expectedLen) {
      return false;
    }
    offset += expectedLen;
  }

  if (!ZigbeePersistentStateStore::isValid(parsed)) {
    return false;
  }
  copyPersistentStateMembers(parsed, outState);
  return true;
}

uint32_t namespaceHash(const char* name, size_t len) {
  if (name == nullptr || len == 0U || len > kJournalNamespaceCapacity) {
    return 0U;
  }
  uint32_t hash = 2166136261UL;
  for (size_t i = 0U; i < len; ++i) {
    hash ^= static_cast<uint8_t>(name[i]);
    hash *= 16777619UL;
  }
  return hash;
}

void copyFromVolatile(const volatile uint8_t* source, uint8_t* destination,
                      size_t len) {
  if (source == nullptr || destination == nullptr) {
    return;
  }
  for (size_t i = 0U; i < len; ++i) {
    destination[i] = source[i];
  }
}

bool volatileBytesEqual(const volatile uint8_t* source,
                        const uint8_t* expected, size_t len) {
  if (source == nullptr || expected == nullptr) {
    return false;
  }
  for (size_t i = 0U; i < len; ++i) {
    if (source[i] != expected[i]) {
      return false;
    }
  }
  return true;
}

bool waitRramcReady(NRF_RRAMC_Type* rramc, uint32_t spinLimit) {
  if (rramc == nullptr) {
    return false;
  }
  while (spinLimit-- > 0U) {
    if (((rramc->READY & RRAMC_READY_READY_Msk) >> RRAMC_READY_READY_Pos) ==
        RRAMC_READY_READY_Ready) {
      return true;
    }
  }
  return false;
}

bool waitRramcReadyNext(NRF_RRAMC_Type* rramc, uint32_t spinLimit) {
  if (rramc == nullptr) {
    return false;
  }
  while (spinLimit-- > 0U) {
    if (((rramc->READYNEXT & RRAMC_READYNEXT_READYNEXT_Msk) >>
         RRAMC_READYNEXT_READYNEXT_Pos) ==
        RRAMC_READYNEXT_READYNEXT_Ready) {
      return true;
    }
  }
  return false;
}

class ZigbeeRramWriteSession {
 public:
  ZigbeeRramWriteSession()
      : rramc_(NRF_RRAMC), previousConfig_(0U), locked_(false), ready_(false) {
    if (rramc_ == nullptr || !nrf54l15_rram_transaction_try_lock()) {
      return;
    }
    locked_ = true;
    previousConfig_ = rramc_->CONFIG;
    rramc_->CONFIG = previousConfig_ | RRAMC_CONFIG_WEN_Msk;
    ready_ = waitRramcReady(rramc_, kRramcSpinLimit);
    if (ready_) {
      rramc_->EVENTS_ACCESSERROR = 0U;
    }
  }

  ~ZigbeeRramWriteSession() {
    if (!locked_) {
      return;
    }
    rramc_->CONFIG = previousConfig_;
    (void)waitRramcReady(rramc_, kRramcSpinLimit);
    nrf54l15_rram_transaction_unlock();
  }

  bool ready() const { return ready_; }

  bool write(size_t pageOffset, const uint8_t* source, size_t len) {
    if (!ready_ || source == nullptr || len == 0U ||
        pageOffset > kJournalPageLen || len > kJournalPageLen - pageOffset) {
      return false;
    }
    volatile uint8_t* const destination =
        g_zigbeeStoragePage + pageOffset;
    for (size_t i = 0U; i < len; ++i) {
      if (!waitRramcReadyNext(rramc_, kRramcSpinLimit)) {
        ready_ = false;
        return false;
      }
      destination[i] = source[i];
    }
    if (rramc_->EVENTS_ACCESSERROR != 0U) {
      ready_ = false;
      return false;
    }
    return true;
  }

  bool commit() {
    if (!ready_) {
      return false;
    }
    rramc_->EVENTS_READY = 0U;
    rramc_->TASKS_COMMITWRITEBUF = 1U;
    ready_ = waitRramcReady(rramc_, kRramcSpinLimit) &&
             rramc_->EVENTS_ACCESSERROR == 0U;
    return ready_;
  }

 private:
  NRF_RRAMC_Type* rramc_;
  uint32_t previousConfig_;
  bool locked_;
  bool ready_;
};

struct ZigbeeJournalMetadata {
  uint32_t namespaceHash = 0U;
  uint32_t generation = 0U;
  uint32_t payloadLen = 0U;
  uint32_t payloadCrc32 = 0U;
  uint32_t state = 0U;
  uint8_t namespaceLength = 0U;
  char namespaceName[kJournalNamespaceCapacity + 1U] = {0};
};

struct ZigbeeJournalSlot {
  bool valid = false;
  uint8_t slot = 0U;
  ZigbeeJournalMetadata metadata{};
  ZigbeePersistentState state{};
};

void encodeJournalHeader(const ZigbeeJournalMetadata& metadata,
                         bool committed,
                         uint8_t encoded[kJournalHeaderLen]) {
  memset(encoded, 0, kJournalHeaderLen);
  writeLe32(encoded + 0U, kJournalMetadataMagic);
  writeLe16(encoded + 4U, kJournalFormatVersion);
  writeLe16(encoded + 6U, kJournalPayloadVersion);
  writeLe32(encoded + 8U, metadata.namespaceHash);
  writeLe32(encoded + 12U, metadata.generation);
  writeLe32(encoded + 16U, metadata.payloadLen);
  writeLe32(encoded + 20U, metadata.payloadCrc32);
  writeLe32(encoded + 24U, metadata.state);
  encoded[28U] = metadata.namespaceLength;
  memcpy(encoded + 29U, metadata.namespaceName, metadata.namespaceLength);
  writeLe32(encoded + 44U, crc32(encoded, 44U));
  writeLe32(encoded + kJournalCommitOffset,
            committed ? kJournalCommitMarker : 0U);
}

bool decodeJournalHeader(const uint8_t* encoded, size_t encodedLen,
                         ZigbeeJournalMetadata* outMetadata) {
  if (encoded == nullptr || encodedLen != kJournalHeaderLen ||
      outMetadata == nullptr ||
      readLe32(encoded + 0U) != kJournalMetadataMagic ||
      readLe16(encoded + 4U) != kJournalFormatVersion ||
      readLe16(encoded + 6U) != kJournalPayloadVersion ||
      readLe32(encoded + 44U) != crc32(encoded, 44U) ||
      readLe32(encoded + kJournalCommitOffset) != kJournalCommitMarker) {
    return false;
  }
  for (size_t i = 52U; i < kJournalHeaderLen; ++i) {
    if (encoded[i] != 0U) {
      return false;
    }
  }

  ZigbeeJournalMetadata parsed{};
  parsed.namespaceHash = readLe32(encoded + 8U);
  parsed.generation = readLe32(encoded + 12U);
  parsed.payloadLen = readLe32(encoded + 16U);
  parsed.payloadCrc32 = readLe32(encoded + 20U);
  parsed.state = readLe32(encoded + 24U);
  parsed.namespaceLength = encoded[28U];
  if (parsed.namespaceLength == 0U ||
      parsed.namespaceLength > kJournalNamespaceCapacity) {
    return false;
  }
  memcpy(parsed.namespaceName, encoded + 29U, parsed.namespaceLength);
  for (size_t i = parsed.namespaceLength; i < kJournalNamespaceCapacity; ++i) {
    if (encoded[29U + i] != 0U) {
      return false;
    }
  }
  if (namespaceHash(parsed.namespaceName, parsed.namespaceLength) !=
      parsed.namespaceHash) {
    return false;
  }

  if (parsed.state == kJournalStateLive) {
    if (parsed.payloadLen == 0U ||
        parsed.payloadLen > kMaxJournalPayloadLen) {
      return false;
    }
  } else if (parsed.state == kJournalStateTombstone) {
    if (parsed.payloadLen != 0U || parsed.payloadCrc32 != 0U) {
      return false;
    }
  } else {
    return false;
  }

  *outMetadata = parsed;
  return true;
}

bool metadataMatchesNamespace(const ZigbeeJournalMetadata& metadata,
                              const char* name, uint8_t nameLength,
                              uint32_t nameHash) {
  return name != nullptr && nameLength != 0U &&
         metadata.namespaceHash == nameHash &&
         metadata.namespaceLength == nameLength &&
         memcmp(metadata.namespaceName, name, nameLength) == 0;
}

bool readJournalSlot(uint8_t slot, ZigbeeJournalSlot* outSlot) {
  if (slot >= kJournalSlotCount || outSlot == nullptr) {
    return false;
  }
  *outSlot = ZigbeeJournalSlot{};
  outSlot->slot = slot;
  resetPersistentState(&outSlot->state);

  const size_t slotOffset = static_cast<size_t>(slot) * kJournalSlotLen;
  uint8_t header[kJournalHeaderLen] = {0};
  copyFromVolatile(g_zigbeeStoragePage + slotOffset, header, sizeof(header));
  if (!decodeJournalHeader(header, sizeof(header), &outSlot->metadata)) {
    return false;
  }

  if (outSlot->metadata.state == kJournalStateLive) {
    uint8_t payload[kMaxJournalPayloadLen] = {0};
    copyFromVolatile(g_zigbeeStoragePage + slotOffset + kJournalHeaderLen,
                     payload, outSlot->metadata.payloadLen);
    if (crc32(payload, outSlot->metadata.payloadLen) !=
            outSlot->metadata.payloadCrc32 ||
        !deserializePersistentState(payload, outSlot->metadata.payloadLen,
                                    &outSlot->state)) {
      return false;
    }
  }
  outSlot->valid = true;
  return true;
}

bool journalSlotIsErased(uint8_t slot) {
  if (slot >= kJournalSlotCount) {
    return false;
  }
  const size_t slotOffset = static_cast<size_t>(slot) * kJournalSlotLen;
  bool allZero = true;
  bool allOnes = true;
  for (size_t i = 0U; i < kJournalSlotLen; ++i) {
    const uint8_t value = g_zigbeeStoragePage[slotOffset + i];
    allZero = allZero && value == 0U;
    allOnes = allOnes && value == 0xFFU;
    if (!allZero && !allOnes) {
      return false;
    }
  }
  return allZero || allOnes;
}

bool generationIsNewer(uint32_t candidate, uint32_t reference) {
  const uint32_t delta = candidate - reference;
  return delta != 0U && delta < 0x80000000UL;
}

const ZigbeeJournalSlot* newestValidSlotForNamespace(
    const ZigbeeJournalSlot& slotA, const ZigbeeJournalSlot& slotB,
    const char* name, uint8_t nameLength, uint32_t nameHash) {
  const bool aMatches =
      slotA.valid && metadataMatchesNamespace(slotA.metadata, name, nameLength,
                                              nameHash);
  const bool bMatches =
      slotB.valid && metadataMatchesNamespace(slotB.metadata, name, nameLength,
                                              nameHash);
  if (!aMatches) {
    return bMatches ? &slotB : nullptr;
  }
  if (!bMatches) {
    return &slotA;
  }
  return generationIsNewer(slotB.metadata.generation,
                           slotA.metadata.generation)
             ? &slotB
             : &slotA;
}

uint8_t chooseTargetSlot(const ZigbeeJournalSlot& slotA,
                         const ZigbeeJournalSlot& slotB,
                         const ZigbeeJournalSlot* newestForNamespace) {
  if (newestForNamespace != nullptr) {
    return newestForNamespace->slot == slotA.slot ? slotB.slot : slotA.slot;
  }
  if (!slotA.valid) {
    return slotA.slot;
  }
  if (!slotB.valid) {
    return slotB.slot;
  }
  return generationIsNewer(slotA.metadata.generation,
                           slotB.metadata.generation)
             ? slotB.slot
             : slotA.slot;
}

bool writeJournalRecord(uint8_t slot, const ZigbeeJournalMetadata& metadata,
                        const uint8_t* payload,
                        ZigbeeRramWriteSession* session) {
  if (slot >= kJournalSlotCount || session == nullptr || !session->ready() ||
      (metadata.state == kJournalStateLive &&
       (payload == nullptr || metadata.payloadLen == 0U ||
        metadata.payloadLen > kMaxJournalPayloadLen)) ||
      (metadata.state == kJournalStateTombstone &&
       (metadata.payloadLen != 0U || metadata.payloadCrc32 != 0U))) {
    return false;
  }

  const size_t slotOffset = static_cast<size_t>(slot) * kJournalSlotLen;
  uint8_t header[kJournalHeaderLen] = {0};
  encodeJournalHeader(metadata, false, header);
  uint8_t invalidCommit[sizeof(uint32_t)] = {0};
  uint8_t validCommit[sizeof(uint32_t)] = {0};
  writeLe32(validCommit, kJournalCommitMarker);

  if (!session->write(slotOffset + kJournalCommitOffset, invalidCommit,
                      sizeof(invalidCommit)) ||
      !session->commit() ||
      !volatileBytesEqual(g_zigbeeStoragePage + slotOffset +
                              kJournalCommitOffset,
                          invalidCommit, sizeof(invalidCommit))) {
    return false;
  }

  if (metadata.state == kJournalStateLive) {
    if (!session->write(slotOffset + kJournalHeaderLen, payload,
                        metadata.payloadLen)) {
      return false;
    }
  } else {
    const uint8_t zeroes[32] = {0};
    const size_t bodyLength = kJournalSlotLen - kJournalHeaderLen;
    for (size_t bodyOffset = 0U; bodyOffset < bodyLength;
         bodyOffset += sizeof(zeroes)) {
      const size_t remaining = bodyLength - bodyOffset;
      const size_t chunk =
          remaining < sizeof(zeroes) ? remaining : sizeof(zeroes);
      if (!session->write(slotOffset + kJournalHeaderLen + bodyOffset,
                          zeroes, chunk)) {
        return false;
      }
    }
  }
  if (!session->write(slotOffset, header, sizeof(header)) ||
      !session->commit() ||
      !volatileBytesEqual(g_zigbeeStoragePage + slotOffset, header,
                          sizeof(header)) ||
      (metadata.state == kJournalStateLive &&
       !volatileBytesEqual(g_zigbeeStoragePage + slotOffset +
                               kJournalHeaderLen,
                           payload, metadata.payloadLen))) {
    return false;
  }

  // The exact four-byte commit marker is the only final phase. If power fails
  // earlier, this slot is invalid and the other committed slot remains intact.
  if (!session->write(slotOffset + kJournalCommitOffset, validCommit,
                      sizeof(validCommit)) ||
      !session->commit() ||
      !volatileBytesEqual(g_zigbeeStoragePage + slotOffset +
                              kJournalCommitOffset,
                          validCommit, sizeof(validCommit))) {
    return false;
  }

  ZigbeeJournalSlot verified{};
  if (!readJournalSlot(slot, &verified) ||
      verified.metadata.generation != metadata.generation ||
      verified.metadata.namespaceHash != metadata.namespaceHash ||
      verified.metadata.state != metadata.state ||
      verified.metadata.payloadLen != metadata.payloadLen ||
      verified.metadata.payloadCrc32 != metadata.payloadCrc32) {
    return false;
  }
  if (metadata.state == kJournalStateTombstone) {
    const size_t bodyOffset = slotOffset + kJournalHeaderLen;
    const size_t bodyLength = kJournalSlotLen - kJournalHeaderLen;
    for (size_t i = 0U; i < bodyLength; ++i) {
      if (g_zigbeeStoragePage[bodyOffset + i] != 0U) {
        return false;
      }
    }
    return true;
  }
  uint8_t verifiedPayload[kMaxJournalPayloadLen] = {0};
  size_t verifiedPayloadLen = 0U;
  return serializePersistentState(verified.state, verifiedPayload,
                                  sizeof(verifiedPayload),
                                  &verifiedPayloadLen) &&
         verifiedPayloadLen == metadata.payloadLen &&
         memcmp(verifiedPayload, payload, metadata.payloadLen) == 0;
}

bool commitStateForNamespaceLocked(
    const char* name, uint8_t nameLength, uint32_t nameHash,
    uint32_t recordState, const ZigbeePersistentState* state,
    bool allowNamespaceReplacement, ZigbeeRramWriteSession* session) {
  if (name == nullptr || nameLength == 0U ||
      nameLength > kJournalNamespaceCapacity || nameHash == 0U ||
      session == nullptr || !session->ready() ||
      (recordState != kJournalStateLive &&
       recordState != kJournalStateTombstone) ||
      (recordState == kJournalStateLive && state == nullptr)) {
    return false;
  }

  uint8_t payload[kMaxJournalPayloadLen] = {0};
  size_t payloadLen = 0U;
  if (recordState == kJournalStateLive) {
    if (!serializePersistentState(*state, payload, sizeof(payload),
                                  &payloadLen)) {
      return false;
    }
  }

  const uint8_t firstSlot = static_cast<uint8_t>(
      (nameHash % kJournalPartitionCount) * kJournalSlotsPerPartition);
  ZigbeeJournalSlot slotA{};
  ZigbeeJournalSlot slotB{};
  (void)readJournalSlot(firstSlot, &slotA);
  (void)readJournalSlot(static_cast<uint8_t>(firstSlot + 1U), &slotB);
  const bool slotAForeign =
      slotA.valid && !metadataMatchesNamespace(
                         slotA.metadata, name, nameLength, nameHash);
  const bool slotBForeign =
      slotB.valid && !metadataMatchesNamespace(
                         slotB.metadata, name, nameLength, nameHash);
  if ((slotAForeign || slotBForeign) && !allowNamespaceReplacement) {
    // A hash-partition collision is explicit capacity exhaustion. Never evict
    // another namespace's committed state.
    return false;
  }
  const ZigbeeJournalSlot* const newest = newestValidSlotForNamespace(
      slotA, slotB, name, nameLength, nameHash);
  const uint8_t target = chooseTargetSlot(slotA, slotB, newest);

  ZigbeeJournalMetadata metadata{};
  metadata.namespaceHash = nameHash;
  metadata.namespaceLength = nameLength;
  memcpy(metadata.namespaceName, name, nameLength);
  metadata.generation =
      newest == nullptr ? 1U : newest->metadata.generation + 1U;
  metadata.state = recordState;

  if (recordState == kJournalStateLive) {
    metadata.payloadLen = static_cast<uint32_t>(payloadLen);
    metadata.payloadCrc32 = crc32(payload, payloadLen);
  }

  if (!writeJournalRecord(target, metadata,
                          recordState == kJournalStateLive ? payload
                                                           : nullptr,
                          session)) {
    return false;
  }

  ZigbeeJournalSlot verifyA{};
  ZigbeeJournalSlot verifyB{};
  (void)readJournalSlot(firstSlot, &verifyA);
  (void)readJournalSlot(static_cast<uint8_t>(firstSlot + 1U), &verifyB);
  const ZigbeeJournalSlot* const verified = newestValidSlotForNamespace(
      verifyA, verifyB, name, nameLength, nameHash);
  return verified != nullptr && verified->slot == target &&
         verified->metadata.generation == metadata.generation &&
         verified->metadata.state == recordState;
}

bool commitStateForNamespace(const char* name, uint8_t nameLength,
                             uint32_t nameHash, uint32_t recordState,
                             const ZigbeePersistentState* state,
                             bool allowNamespaceReplacement) {
  // Acquire the global RRAM transaction lock before reading either slot. The
  // target/generation decision, all three commit phases, and final readback
  // therefore form one serialized journal transaction.
  ZigbeeRramWriteSession session;
  return commitStateForNamespaceLocked(
      name, nameLength, nameHash, recordState, state,
      allowNamespaceReplacement, &session);
}

bool isSupportedReportingDataType(ZigbeeZclDataType dataType) {
  switch (dataType) {
    case ZigbeeZclDataType::kBoolean:
    case ZigbeeZclDataType::kBitmap8:
    case ZigbeeZclDataType::kBitmap16:
    case ZigbeeZclDataType::kBitmap32:
    case ZigbeeZclDataType::kUint8:
    case ZigbeeZclDataType::kUint16:
    case ZigbeeZclDataType::kUint32:
    case ZigbeeZclDataType::kInt16:
    case ZigbeeZclDataType::kCharString:
      return true;
  }
  return false;
}

bool isSupportedBindingAddressMode(ZigbeeBindingAddressMode addressMode) {
  return addressMode == ZigbeeBindingAddressMode::kGroup ||
         addressMode == ZigbeeBindingAddressMode::kExtended;
}

bool finalizeLoadedState(ZigbeePersistentState* state) {
  if (state == nullptr || !ZigbeePersistentStateStore::isValid(*state)) {
    return false;
  }
  ZigbeePersistentState canonical{};
  copyPersistentStateMembers(*state, &canonical);
  copyPersistentStateMembers(canonical, state);
  return true;
}

}  // namespace

ZigbeePersistentStateStore::ZigbeePersistentStateStore()
    : prefs_(),
      open_(false),
      legacyPrefsOpen_(false),
      namespaceLength_(0U),
      namespaceName_{0},
      namespaceHash_(0U) {}

bool ZigbeePersistentStateStore::begin(const char* name) {
  if (name == nullptr) {
    return false;
  }
  const size_t len = strnlen(name, kJournalNamespaceCapacity + 1U);
  if (len == 0U || len > kJournalNamespaceCapacity) {
    return false;
  }
  if (open_) {
    return len == namespaceLength_ &&
           memcmp(name, namespaceName_, len) == 0;
  }
  memset(namespaceName_, 0, sizeof(namespaceName_));
  memcpy(namespaceName_, name, len);
  namespaceLength_ = static_cast<uint8_t>(len);
  namespaceHash_ = namespaceHash(namespaceName_, namespaceLength_);
  if (namespaceHash_ == 0U) {
    return false;
  }

  // The dedicated journal is usable even if the legacy Preferences blob is
  // damaged. Preferences is opened only as a best-effort migration source.
  legacyPrefsOpen_ = prefs_.begin(name, false);
  open_ = true;
  return true;
}

void ZigbeePersistentStateStore::end() {
  if (!open_) {
    return;
  }
  if (legacyPrefsOpen_) {
    prefs_.end();
  }
  legacyPrefsOpen_ = false;
  open_ = false;
  namespaceLength_ = 0U;
  memset(namespaceName_, 0, sizeof(namespaceName_));
  namespaceHash_ = 0U;
}

void ZigbeePersistentStateStore::initialize(ZigbeePersistentState* state) {
  if (state == nullptr) {
    return;
  }
  resetPersistentState(state);
  state->magic = kZigbeeStateMagic;
  state->version = kZigbeeStateVersion;
}

bool ZigbeePersistentStateStore::isValid(const ZigbeePersistentState& state) {
  if (state.magic != kZigbeeStateMagic ||
      state.version != kZigbeeStateVersion || state.logicalType > 2U ||
      (state.channel != 0U && (state.channel < 11U || state.channel > 26U)) ||
      state.preconfiguredKeyMode > 2U ||
      state.reportingCount >
          static_cast<uint8_t>(sizeof(state.reporting) /
                               sizeof(state.reporting[0])) ||
      state.bindingCount >
          static_cast<uint8_t>(sizeof(state.bindings) /
                               sizeof(state.bindings[0]))) {
    return false;
  }
  for (const ZigbeeReportingConfiguration& reporting : state.reporting) {
    if (reporting.used && !isSupportedReportingDataType(reporting.dataType)) {
      return false;
    }
  }
  for (const ZigbeeBindingEntry& binding : state.bindings) {
    if (binding.used &&
        !isSupportedBindingAddressMode(binding.destinationAddressMode)) {
      return false;
    }
  }
  for (const ZigbeePersistentPeerSecurityState& peer : state.peerSecurity) {
    if (peer.used &&
        (peer.ieeeAddress == 0U || peer.shortAddress == 0U ||
         peer.shortAddress == 0xFFFFU || peer.networkKeySequence == 0U)) {
      return false;
    }
  }
  return true;
}

bool ZigbeePersistentStateStore::load(ZigbeePersistentState* outState) {
  if (!open_ || outState == nullptr) {
    return false;
  }
  initialize(outState);
  const uint8_t firstSlot = static_cast<uint8_t>(
      (namespaceHash_ % kJournalPartitionCount) *
      kJournalSlotsPerPartition);
  ZigbeeJournalSlot slotA{};
  ZigbeeJournalSlot slotB{};
  (void)readJournalSlot(firstSlot, &slotA);
  (void)readJournalSlot(static_cast<uint8_t>(firstSlot + 1U), &slotB);
  const bool slotAForeign =
      slotA.valid && !metadataMatchesNamespace(
                         slotA.metadata, namespaceName_, namespaceLength_,
                         namespaceHash_);
  const bool slotBForeign =
      slotB.valid && !metadataMatchesNamespace(
                         slotB.metadata, namespaceName_, namespaceLength_,
                         namespaceHash_);
  if (slotAForeign || slotBForeign) {
    return false;
  }
  const ZigbeeJournalSlot* const newest = newestValidSlotForNamespace(
      slotA, slotB, namespaceName_, namespaceLength_, namespaceHash_);
  if (newest != nullptr) {
    if (newest->metadata.state == kJournalStateTombstone) {
      return false;
    }
    copyPersistentStateMembers(newest->state, outState);
    return true;
  }
  if (!journalSlotIsErased(firstSlot) ||
      !journalSlotIsErased(static_cast<uint8_t>(firstSlot + 1U))) {
    // Legacy state is only an admissible migration source before this
    // namespace has any direct-journal history. Falling back after corruption
    // or a namespace collision could resurrect stale keys or frame counters.
    return false;
  }
  if (!legacyPrefsOpen_) {
    return false;
  }

  const auto completeLegacyLoad = [this, outState]() -> bool {
    if (!finalizeLoadedState(outState)) {
      return false;
    }
    // A migration failure leaves every legacy key untouched and the caller can
    // still use the validated RAM copy. Only a committed/read-back-verified
    // direct journal permits destructive legacy cleanup.
    if (commitStateForNamespace(namespaceName_, namespaceLength_,
                                namespaceHash_, kJournalStateLive,
                                outState, false)) {
      bool removedLegacy = false;
      (void)clearLegacyStorage(&prefs_, &removedLegacy);
    }
    return true;
  };

  initialize(outState);
  const size_t len = prefs_.getBytesLength(kPrefsKeyLegacyState);
  if (len == sizeof(*outState)) {
    ZigbeePersistentState legacy{};
    resetPersistentState(&legacy);
    if (prefs_.getBytes(kPrefsKeyLegacyState, &legacy, sizeof(legacy)) !=
            sizeof(legacy) ||
        !isValid(legacy)) {
      return false;
    }
    copyPersistentStateMembers(legacy, outState);
    return completeLegacyLoad();
  }
  if (loadChunkedState(&prefs_, outState)) {
    return completeLegacyLoad();
  }
  if (len == sizeof(ZigbeePersistentStateV6)) {
    ZigbeePersistentStateV6 legacy{};
    if (prefs_.getBytes(kPrefsKeyLegacyState, &legacy, sizeof(legacy)) !=
            sizeof(legacy) ||
        !isValidV6(legacy)) {
      return false;
    }

    outState->magic = legacy.magic;
    outState->version = kZigbeeStateVersion;
    outState->channel = legacy.channel;
    outState->logicalType = legacy.logicalType;
    outState->panId = legacy.panId;
    outState->nwkAddress = legacy.nwkAddress;
    outState->parentShort = legacy.parentShort;
    outState->manufacturerCode = legacy.manufacturerCode;
    outState->ieeeAddress = legacy.ieeeAddress;
    outState->extendedPanId = legacy.extendedPanId;
    memcpy(outState->networkKey, legacy.networkKey,
           sizeof(outState->networkKey));
    outState->nwkFrameCounter = legacy.nwkFrameCounter;
    outState->apsFrameCounter = legacy.apsFrameCounter;
    outState->keySequence = legacy.keySequence;
    memcpy(outState->alternateNetworkKey, legacy.alternateNetworkKey,
           sizeof(outState->alternateNetworkKey));
    outState->alternateKeySequence = legacy.alternateKeySequence;
    outState->flags = legacy.flags;
    outState->preconfiguredKeyMode = legacy.preconfiguredKeyMode;
    outState->onOffState = legacy.onOffState;
    outState->levelState = legacy.levelState;
    outState->trustCenterIeee = legacy.trustCenterIeee;
    outState->reportingCount = legacy.reportingCount;
    memcpy(outState->reporting, legacy.reporting, sizeof(outState->reporting));
    outState->bindingCount = legacy.bindingCount;
    memcpy(outState->bindings, legacy.bindings, sizeof(outState->bindings));
    outState->incomingNwkFrameCounter = legacy.incomingNwkFrameCounter;
    outState->incomingApsFrameCounter = legacy.incomingApsFrameCounter;
    return completeLegacyLoad();
  }
  if (len == sizeof(ZigbeePersistentStateV5)) {
    ZigbeePersistentStateV5 legacy{};
    if (prefs_.getBytes(kPrefsKeyLegacyState, &legacy, sizeof(legacy)) !=
            sizeof(legacy) ||
        !isValidV5(legacy)) {
      return false;
    }

    outState->magic = legacy.magic;
    outState->version = kZigbeeStateVersion;
    outState->channel = legacy.channel;
    outState->logicalType = legacy.logicalType;
    outState->panId = legacy.panId;
    outState->nwkAddress = legacy.nwkAddress;
    outState->parentShort = legacy.parentShort;
    outState->manufacturerCode = legacy.manufacturerCode;
    outState->ieeeAddress = legacy.ieeeAddress;
    outState->extendedPanId = legacy.extendedPanId;
    memcpy(outState->networkKey, legacy.networkKey,
           sizeof(outState->networkKey));
    outState->nwkFrameCounter = legacy.nwkFrameCounter;
    outState->apsFrameCounter = legacy.apsFrameCounter;
    outState->keySequence = legacy.keySequence;
    memset(outState->alternateNetworkKey, 0,
           sizeof(outState->alternateNetworkKey));
    outState->alternateKeySequence = 0U;
    outState->flags = legacy.flags;
    outState->preconfiguredKeyMode = legacy.preconfiguredKeyMode;
    outState->onOffState = legacy.onOffState;
    outState->levelState = legacy.levelState;
    outState->trustCenterIeee = legacy.trustCenterIeee;
    outState->reportingCount = legacy.reportingCount;
    memcpy(outState->reporting, legacy.reporting, sizeof(outState->reporting));
    outState->bindingCount = legacy.bindingCount;
    memcpy(outState->bindings, legacy.bindings, sizeof(outState->bindings));
    outState->incomingNwkFrameCounter = legacy.incomingNwkFrameCounter;
    outState->incomingApsFrameCounter = legacy.incomingApsFrameCounter;
    return completeLegacyLoad();
  }
  if (len == sizeof(ZigbeePersistentStateV4)) {
    ZigbeePersistentStateV4 legacy{};
    if (prefs_.getBytes(kPrefsKeyLegacyState, &legacy, sizeof(legacy)) !=
            sizeof(legacy) ||
        !isValidV4(legacy)) {
      return false;
    }

    outState->magic = legacy.magic;
    outState->version = kZigbeeStateVersion;
    outState->channel = legacy.channel;
    outState->logicalType = legacy.logicalType;
    outState->panId = legacy.panId;
    outState->nwkAddress = legacy.nwkAddress;
    outState->parentShort = legacy.parentShort;
    outState->manufacturerCode = legacy.manufacturerCode;
    outState->ieeeAddress = legacy.ieeeAddress;
    outState->extendedPanId = legacy.extendedPanId;
    memcpy(outState->networkKey, legacy.networkKey,
           sizeof(outState->networkKey));
    outState->nwkFrameCounter = legacy.nwkFrameCounter;
    outState->apsFrameCounter = legacy.apsFrameCounter;
    outState->keySequence = legacy.keySequence;
    outState->flags = legacy.flags;
    outState->preconfiguredKeyMode = 0U;
    outState->onOffState = legacy.onOffState;
    outState->levelState = legacy.levelState;
    outState->trustCenterIeee = 0U;
    outState->reportingCount = legacy.reportingCount;
    memcpy(outState->reporting, legacy.reporting, sizeof(outState->reporting));
    outState->bindingCount = legacy.bindingCount;
    memcpy(outState->bindings, legacy.bindings, sizeof(outState->bindings));
    outState->incomingNwkFrameCounter = legacy.incomingNwkFrameCounter;
    outState->incomingApsFrameCounter = 0U;
    return completeLegacyLoad();
  }
  if (len == sizeof(ZigbeePersistentStateV3)) {
    ZigbeePersistentStateV3 legacy{};
    if (prefs_.getBytes(kPrefsKeyLegacyState, &legacy, sizeof(legacy)) !=
            sizeof(legacy) ||
        !isValidV3(legacy)) {
      return false;
    }

    outState->magic = legacy.magic;
    outState->version = kZigbeeStateVersion;
    outState->channel = legacy.channel;
    outState->logicalType = legacy.logicalType;
    outState->panId = legacy.panId;
    outState->nwkAddress = legacy.nwkAddress;
    outState->parentShort = legacy.parentShort;
    outState->manufacturerCode = legacy.manufacturerCode;
    outState->ieeeAddress = legacy.ieeeAddress;
    outState->extendedPanId = legacy.extendedPanId;
    memcpy(outState->networkKey, legacy.networkKey,
           sizeof(outState->networkKey));
    outState->nwkFrameCounter = legacy.nwkFrameCounter;
    outState->apsFrameCounter = legacy.apsFrameCounter;
    outState->keySequence = legacy.keySequence;
    outState->flags = legacy.flags;
    outState->preconfiguredKeyMode = 0U;
    outState->onOffState = legacy.onOffState;
    outState->levelState = legacy.levelState;
    outState->trustCenterIeee = 0U;
    outState->reportingCount = legacy.reportingCount;
    memcpy(outState->reporting, legacy.reporting, sizeof(outState->reporting));
    outState->bindingCount = legacy.bindingCount;
    memcpy(outState->bindings, legacy.bindings, sizeof(outState->bindings));
    outState->incomingNwkFrameCounter = 0U;
    outState->incomingApsFrameCounter = 0U;
    return completeLegacyLoad();
  }
  if (len == sizeof(ZigbeePersistentStateV2)) {
    ZigbeePersistentStateV2 legacy{};
    if (prefs_.getBytes(kPrefsKeyLegacyState, &legacy, sizeof(legacy)) !=
            sizeof(legacy) ||
        !isValidV2(legacy)) {
      return false;
    }

    outState->magic = legacy.magic;
    outState->version = kZigbeeStateVersion;
    outState->channel = legacy.channel;
    outState->logicalType = legacy.logicalType;
    outState->panId = legacy.panId;
    outState->nwkAddress = legacy.nwkAddress;
    outState->parentShort = legacy.parentShort;
    outState->manufacturerCode = legacy.manufacturerCode;
    outState->ieeeAddress = legacy.ieeeAddress;
    outState->extendedPanId = legacy.extendedPanId;
    memcpy(outState->networkKey, legacy.networkKey,
           sizeof(outState->networkKey));
    outState->nwkFrameCounter = legacy.nwkFrameCounter;
    outState->apsFrameCounter = legacy.apsFrameCounter;
    outState->keySequence = legacy.keySequence;
    outState->flags = legacy.flags;
    outState->preconfiguredKeyMode = 0U;
    outState->onOffState = legacy.onOffState;
    outState->levelState = legacy.levelState;
    outState->trustCenterIeee = 0U;
    outState->reportingCount = legacy.reportingCount;
    memcpy(outState->reporting, legacy.reporting, sizeof(outState->reporting));
    outState->incomingNwkFrameCounter = 0U;
    outState->incomingApsFrameCounter = 0U;
    return completeLegacyLoad();
  }

  if (len != sizeof(ZigbeePersistentStateV1)) {
    return false;
  }

  ZigbeePersistentStateV1 legacy{};
  if (prefs_.getBytes(kPrefsKeyLegacyState, &legacy, sizeof(legacy)) !=
          sizeof(legacy) ||
      !isValidV1(legacy)) {
    return false;
  }

  outState->magic = legacy.magic;
  outState->version = kZigbeeStateVersion;
  outState->channel = legacy.channel;
  outState->logicalType = legacy.logicalType;
  outState->panId = legacy.panId;
  outState->nwkAddress = legacy.nwkAddress;
  outState->parentShort = legacy.parentShort;
  outState->manufacturerCode = legacy.manufacturerCode;
  outState->ieeeAddress = legacy.ieeeAddress;
  outState->extendedPanId = legacy.extendedPanId;
  memcpy(outState->networkKey, legacy.networkKey, sizeof(outState->networkKey));
  outState->nwkFrameCounter = legacy.nwkFrameCounter;
  outState->apsFrameCounter = legacy.apsFrameCounter;
  outState->keySequence = legacy.keySequence;
  outState->flags = legacy.flags;
  outState->preconfiguredKeyMode = 0U;
  outState->onOffState = legacy.onOffState;
  outState->levelState = legacy.onOffState ? 0xFEU : 0U;
  outState->trustCenterIeee = 0U;
  outState->reportingCount = legacy.reportingCount;
  memcpy(outState->reporting, legacy.reporting, sizeof(outState->reporting));
  outState->incomingNwkFrameCounter = 0U;
  outState->incomingApsFrameCounter = 0U;
  return completeLegacyLoad();
}

ZigbeePersistentLoadStatus ZigbeePersistentStateStore::loadWithStatus(
    ZigbeePersistentState* outState) {
  if (!open_ || outState == nullptr) {
    return ZigbeePersistentLoadStatus::kNotOpen;
  }
  initialize(outState);

  const uint8_t firstSlot = static_cast<uint8_t>(
      (namespaceHash_ % kJournalPartitionCount) *
      kJournalSlotsPerPartition);
  ZigbeeJournalSlot slotA{};
  ZigbeeJournalSlot slotB{};
  (void)readJournalSlot(firstSlot, &slotA);
  (void)readJournalSlot(static_cast<uint8_t>(firstSlot + 1U), &slotB);

  const bool slotAForeign =
      slotA.valid && !metadataMatchesNamespace(
                         slotA.metadata, namespaceName_, namespaceLength_,
                         namespaceHash_);
  const bool slotBForeign =
      slotB.valid && !metadataMatchesNamespace(
                         slotB.metadata, namespaceName_, namespaceLength_,
                         namespaceHash_);
  if (slotAForeign || slotBForeign) {
    return ZigbeePersistentLoadStatus::kNamespaceCollision;
  }

  const bool erasedA = journalSlotIsErased(firstSlot);
  const bool erasedB =
      journalSlotIsErased(static_cast<uint8_t>(firstSlot + 1U));
  if ((!slotA.valid && !erasedA) || (!slotB.valid && !erasedB)) {
    // A valid older record does not prove that an invalid sibling was merely
    // an interrupted write. It may have been a newer committed counter
    // reservation that later suffered latent corruption; falling back would
    // then reuse CCM nonces. Security-bearing callers must fail closed.
    return ZigbeePersistentLoadStatus::kCorrupt;
  }

  const ZigbeeJournalSlot* const newest = newestValidSlotForNamespace(
      slotA, slotB, namespaceName_, namespaceLength_, namespaceHash_);
  if (newest != nullptr) {
    if (newest->metadata.state == kJournalStateTombstone) {
      return ZigbeePersistentLoadStatus::kTombstone;
    }
    copyPersistentStateMembers(newest->state, outState);
    return ZigbeePersistentLoadStatus::kLive;
  }

  if (!erasedA || !erasedB) {
    return ZigbeePersistentLoadStatus::kCorrupt;
  }

  // A truly erased direct partition may still have a pre-journal Preferences
  // record. Preserve the existing migration path only in that unambiguous
  // case; load() commits and verifies it before deleting legacy keys.
  if (load(outState)) {
    return ZigbeePersistentLoadStatus::kLive;
  }
  initialize(outState);
  return ZigbeePersistentLoadStatus::kEmpty;
}

bool ZigbeePersistentStateStore::save(const ZigbeePersistentState& state) {
  if (!open_ || !isValid(state)) {
    return false;
  }
  if (!commitStateForNamespace(namespaceName_, namespaceLength_,
                               namespaceHash_, kJournalStateLive, &state,
                               false)) {
    return false;
  }
  if (legacyPrefsOpen_) {
    bool removedLegacy = false;
    (void)clearLegacyStorage(&prefs_, &removedLegacy);
  }
  return true;
}

bool ZigbeePersistentStateStore::reserveOutgoingFrameCounterRange(
    uint8_t persistentFlag, bool apsTrustCenter,
    uint32_t expectedHighWater, uint32_t reservedHighWater) {
  if (!open_ || persistentFlag == 0U || expectedHighWater == 0U ||
      reservedHighWater <= expectedHighWater) {
    return false;
  }

  ZigbeeRramWriteSession session;
  if (!session.ready()) {
    return false;
  }

  const uint8_t firstSlot = static_cast<uint8_t>(
      (namespaceHash_ % kJournalPartitionCount) *
      kJournalSlotsPerPartition);
  ZigbeeJournalSlot slotA{};
  ZigbeeJournalSlot slotB{};
  (void)readJournalSlot(firstSlot, &slotA);
  (void)readJournalSlot(static_cast<uint8_t>(firstSlot + 1U), &slotB);

  const bool slotAForeign =
      slotA.valid && !metadataMatchesNamespace(
                         slotA.metadata, namespaceName_, namespaceLength_,
                         namespaceHash_);
  const bool slotBForeign =
      slotB.valid && !metadataMatchesNamespace(
                         slotB.metadata, namespaceName_, namespaceLength_,
                         namespaceHash_);
  const bool erasedA = journalSlotIsErased(firstSlot);
  const bool erasedB =
      journalSlotIsErased(static_cast<uint8_t>(firstSlot + 1U));
  if (slotAForeign || slotBForeign ||
      (!slotA.valid && !erasedA) || (!slotB.valid && !erasedB)) {
    return false;
  }

  const ZigbeeJournalSlot* const newest = newestValidSlotForNamespace(
      slotA, slotB, namespaceName_, namespaceLength_, namespaceHash_);
  if (newest == nullptr ||
      newest->metadata.state != kJournalStateLive) {
    return false;
  }

  ZigbeePersistentState staged{};
  copyPersistentStateMembers(newest->state, &staged);
  const uint32_t storedHighWater =
      apsTrustCenter ? staged.apsFrameCounter : staged.nwkFrameCounter;
  if ((staged.flags & persistentFlag) == 0U ||
      storedHighWater != expectedHighWater) {
    return false;
  }
  if (apsTrustCenter) {
    staged.apsFrameCounter = reservedHighWater;
  } else {
    staged.nwkFrameCounter = reservedHighWater;
  }
  staged.flags = static_cast<uint8_t>(staged.flags | persistentFlag);

  return commitStateForNamespaceLocked(
      namespaceName_, namespaceLength_, namespaceHash_, kJournalStateLive,
      &staged, false, &session);
}

bool ZigbeePersistentStateStore::clear() {
  if (!open_) {
    return false;
  }
  // A higher-generation committed tombstone is the clear operation. Old live
  // slots are intentionally not erased: an interrupted clear can select either
  // the prior live record or the newer tombstone, never a torn record, while a
  // completed clear can never resurrect the prior membership.
  if (!commitStateForNamespace(namespaceName_, namespaceLength_,
                               namespaceHash_, kJournalStateTombstone,
                               nullptr, true)) {
    return false;
  }
  // Replicate the tombstone into the other slot. The first verified tombstone
  // already makes clear() durable, so failure here must not undo it; a later
  // clear() retries the second replica.
  (void)commitStateForNamespace(namespaceName_, namespaceLength_,
                                namespaceHash_, kJournalStateTombstone,
                                nullptr, true);
  if (legacyPrefsOpen_) {
    bool removedLegacy = false;
    (void)clearLegacyStorage(&prefs_, &removedLegacy);
  }
  return true;
}

}  // namespace xiao_nrf54l15
#endif  // NRF54L15_CLEAN_ZIGBEE_AVAILABLE
