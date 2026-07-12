
struct BleBondRecordV1 {
  uint8_t peerAddress[6];
  uint8_t peerAddressRandom;
  uint8_t localAddress[6];
  uint8_t localAddressRandom;
  uint8_t ltk[16];
  uint8_t rand[8];
  uint16_t ediv;
  uint8_t keySize;
  uint8_t reserved[3];
};

extern "C" bool nrf54l15_rram_transaction_try_lock(void);
extern "C" void nrf54l15_rram_transaction_unlock(void);

struct BleBondRecordV2 {
  uint8_t peerAddress[6];
  uint8_t peerAddressRandom;
  uint8_t peerIdentityAddress[6];
  uint8_t peerIdentityAddressRandom;
  uint8_t localAddress[6];
  uint8_t localAddressRandom;
  uint8_t ltk[16];
  uint8_t rand[8];
  uint8_t peerIrk[16];
  uint8_t peerIrkValid;
  uint16_t ediv;
  uint8_t keySize;
  uint8_t reserved[2];
};

struct BleBondRetentionBlob {
  uint32_t magic;
  uint16_t version;
  uint16_t recordLength;
  uint32_t crc32;
  xiao_nrf54l15::BleBondRecord record;
};

// The original flash slot is intentionally frozen at the version-2 size.
// Preferences, EEPROM, and bonded CCCDs follow it in the same 4 KB RRAM page.
struct BleBondFlashBlob {
  uint32_t magic;
  uint16_t version;
  uint16_t recordLength;
  uint32_t crc32;
  BleBondRecordV2 record;
};

struct BleBondSigningState {
  uint8_t peerCsrk[16];
  uint8_t localCsrk[16];
  uint32_t peerSignCounter;
  uint32_t localSignCounter;
  uint8_t peerCsrkValid;
  uint8_t localCsrkValid;
  uint8_t peerSignCounterValid;
  uint8_t reserved;
};

struct BleBondSigningBlob {
  uint32_t magic;
  uint32_t baseRecordCrc32;
  uint32_t signingStateCrc32;
  BleBondSigningState state;
};

constexpr uint32_t kBleBondSigningMagic = 0x33474953UL;  // "SIG3"
static_assert(sizeof(BleBondFlashBlob) == 80U,
              "the legacy BLE bond flash slot must remain 80 bytes");
static_assert(sizeof(BleBondSigningState) == 44U,
              "the signing extension must fit the legacy page slack");
static_assert(sizeof(BleBondSigningBlob) == 56U,
              "the signing extension consumes the 56-byte page slack");

constexpr uint32_t kBleBondDbActiveMagic = 0x34444E42UL;     // "BND4"
constexpr uint32_t kBleBondDbTombstoneMagic = 0x344C4544UL;  // "DEL4"
constexpr uint32_t kBleBondDbClearAllMagic = 0x34524C43UL;   // "CLR4"
constexpr uint8_t kBleBondDbReplicaCount = 2U;
constexpr uint8_t kBleBondDbCccdValueBytes = 6U;

constexpr uint8_t kBleBondDbSecurityAuthenticated = 0x01U;
constexpr uint8_t kBleBondDbSecuritySecureConnections = 0x02U;
constexpr uint8_t kBleBondDbKeyPeerIrkValid = 0x01U;
constexpr uint8_t kBleBondDbKeyLocalOrSharedValid = 0x02U;
constexpr uint8_t kBleBondDbKeyPeerLegacyValid = 0x04U;
constexpr uint8_t kBleBondDbKeySharedSecureConnections = 0x08U;
constexpr uint8_t kBleBondDbKeyLegacyProjectionUnknown = 0x10U;
constexpr uint8_t kBleBondDbSigningPeerCsrkValid = 0x01U;
constexpr uint8_t kBleBondDbSigningLocalCsrkValid = 0x02U;
constexpr uint8_t kBleBondDbSigningPeerCounterValid = 0x04U;

struct BleBondDbLongTermKey {
  uint8_t ltk[16];
  uint8_t rand[8];
  uint16_t ediv;
};

// Frozen internal representation. The two key tuples allow a later SMP phase
// to retain locally and peer-distributed legacy keys for GAP role reversal.
struct BleBondDbCore {
  uint8_t peerAddress[6];
  uint8_t peerAddressRandom;
  uint8_t peerIdentityAddress[6];
  uint8_t peerIdentityAddressRandom;
  uint8_t localAddress[6];
  uint8_t localAddressRandom;
  uint8_t keySize;
  uint8_t securityFlags;
  uint8_t keyValidityFlags;
  uint8_t peerIrk[16];
  uint8_t peerCsrk[16];
  uint8_t localCsrk[16];
  uint32_t peerSignCounter;
  uint32_t localSignCounter;
  BleBondDbLongTermKey localOrSharedKey;
  BleBondDbLongTermKey peerLegacyKey;
  uint8_t signingFlags;
  uint8_t pairedLocalRole;
  uint8_t reserved[6];
};

struct BleBondDbPeerState {
  uint32_t schemaFingerprint;
  uint32_t serviceChangedGeneration;
  uint32_t lastUsedGeneration;
  uint16_t serviceChangedStartHandle;
  uint16_t serviceChangedEndHandle;
  uint8_t fixedCccdFlags;
  uint8_t cccdCount;
  uint8_t cccdValues[kBleBondDbCccdValueBytes];
  uint16_t cccdHandles[xiao_nrf54l15::BleRadio::kCustomGattMaxCharacteristics];
};

struct BleBondDbPayload {
  BleBondDbCore core;
  BleBondDbPeerState peerState;
  uint32_t clearGeneration;
  uint8_t reserved[24];
};

struct BleBondDbHeader {
  uint32_t magic;
  uint32_t generation;
  uint32_t payloadCrc32;
  uint32_t headerCrc32;
};

struct BleBondDbReplica {
  BleBondDbHeader header;
  BleBondDbPayload payload;
};

struct BleBondDbPage {
  BleBondDbReplica replicas[kBleBondDbReplicaCount]
                           [xiao_nrf54l15::BleRadio::kBleMaxBonds];
};

static_assert(sizeof(BleBondDbLongTermKey) == 26U,
              "BLE bond DB LTK tuple layout changed");
static_assert(sizeof(BleBondDbCore) == 140U,
              "BLE bond DB normalized core must remain 140 bytes");
static_assert(sizeof(BleBondDbPeerState) == 72U,
              "BLE bond DB peer state must remain 72 bytes");
static_assert(sizeof(BleBondDbPayload) == 240U,
              "BLE bond DB payload must remain 240 bytes");
static_assert(sizeof(BleBondDbHeader) == 16U,
              "BLE bond DB commit header must be one RRAM wordline");
static_assert(sizeof(BleBondDbReplica) == 256U,
              "BLE bond DB replica must remain 256 bytes");
static_assert(sizeof(BleBondDbPage) == 4096U,
              "eight A/B bond replicas must fill one 4 KB page");

struct BleCccdBondEntry {
  uint16_t handle;
  uint16_t value;
};

constexpr uint8_t kBleCccdBondEntryCapacityV1 =
    static_cast<uint8_t>(2U + xiao_nrf54l15::BleRadio::kCustomGattMaxCharacteristics);

struct BleCccdBondRecordV1 {
  uint8_t peerAddress[6];
  uint8_t peerAddressRandom;
  uint8_t peerIdentityAddress[6];
  uint8_t peerIdentityAddressRandom;
  uint8_t localAddress[6];
  uint8_t localAddressRandom;
  uint8_t entryCount;
  uint8_t reserved[2];
  BleCccdBondEntry entries[kBleCccdBondEntryCapacityV1];
};

constexpr uint8_t kBleCccdFixedServiceChanged = 0x01U;
constexpr uint8_t kBleCccdFixedBattery = 0x02U;
constexpr uint8_t kBleCccdFixedMask =
    kBleCccdFixedServiceChanged | kBleCccdFixedBattery;

struct BleCccdBondRecord {
  uint8_t peerAddress[6];
  uint8_t peerAddressRandom;
  uint8_t peerIdentityAddress[6];
  uint8_t peerIdentityAddressRandom;
  uint8_t localAddress[6];
  uint8_t localAddressRandom;
  uint8_t entryCount;
  uint8_t fixedCccdFlags;
  uint8_t reserved;
  uint32_t schemaFingerprint;
  uint16_t serviceChangedStartHandle;
  uint16_t serviceChangedEndHandle;
  BleCccdBondEntry
      entries[xiao_nrf54l15::BleRadio::kCustomGattMaxCharacteristics];
};

struct BleCccdRetentionBlob {
  uint32_t magic;
  uint16_t version;
  uint16_t recordLength;
  uint32_t crc32;
  BleCccdBondRecord record;
};

struct BleCccdRetentionBlobV1 {
  uint32_t magic;
  uint16_t version;
  uint16_t recordLength;
  uint32_t crc32;
  BleCccdBondRecordV1 record;
};

bool appendCccdBondEntry(BleCccdBondRecord* record, uint16_t handle,
                         uint16_t value);

static_assert(sizeof(BleCccdBondRecordV1) == 128U,
              "legacy CCCD record layout must remain readable");
static_assert(sizeof(BleCccdBondRecord) == 128U,
              "CCCD v2 metadata must fit the stable record slot");
static_assert(sizeof(BleCccdRetentionBlobV1) == 140U,
              "legacy CCCD blob must remain 140 bytes");
static_assert(sizeof(BleCccdRetentionBlob) == 140U,
              "CCCD v2 blob must remain 140 bytes");

__attribute__((section(".noinit"))) BleBondRetentionBlob g_bleBondRetention;
__attribute__((section(".noinit"))) BleCccdRetentionBlob g_bleCccdRetention;
__attribute__((section(".bond_base_storage"), aligned(4)))
volatile BleBondFlashBlob g_bleBondFlashStorage;
__attribute__((section(".bond_cccd_storage"), aligned(4)))
volatile BleCccdRetentionBlob g_bleCccdFlashStorage;
__attribute__((section(".bond_signing_storage"), aligned(4)))
volatile BleBondSigningBlob g_bleBondSigningFlashStorage;
__attribute__((section(".bond_db_storage"), aligned(16)))
volatile BleBondDbPage g_bleBondDbStorage;
bool g_bleBondStorageWriteInProgress = false;

constexpr uint8_t kAttOpErrorRsp = 0x01U;
constexpr uint8_t kAttOpExchangeMtuReq = 0x02U;
constexpr uint8_t kAttOpExchangeMtuRsp = 0x03U;
constexpr uint8_t kAttOpFindInfoReq = 0x04U;
constexpr uint8_t kAttOpFindInfoRsp = 0x05U;
constexpr uint8_t kAttOpFindByTypeValueReq = 0x06U;
constexpr uint8_t kAttOpFindByTypeValueRsp = 0x07U;
constexpr uint8_t kAttOpReadByTypeReq = 0x08U;
constexpr uint8_t kAttOpReadByTypeRsp = 0x09U;
constexpr uint8_t kAttOpReadReq = 0x0AU;
constexpr uint8_t kAttOpReadRsp = 0x0BU;
constexpr uint8_t kAttOpReadBlobReq = 0x0CU;
constexpr uint8_t kAttOpReadBlobRsp = 0x0DU;
constexpr uint8_t kAttOpReadMultipleReq = 0x0EU;
constexpr uint8_t kAttOpReadMultipleRsp = 0x0FU;
constexpr uint8_t kAttOpReadMultipleVariableReq = 0x20U;
constexpr uint8_t kAttOpReadMultipleVariableRsp = 0x21U;
constexpr uint8_t kAttOpReadByGroupTypeReq = 0x10U;
constexpr uint8_t kAttOpReadByGroupTypeRsp = 0x11U;
constexpr uint8_t kAttOpWriteReq = 0x12U;
constexpr uint8_t kAttOpWriteRsp = 0x13U;
constexpr uint8_t kAttOpSignedWriteCmd = 0xD2U;
constexpr uint8_t kAttOpPrepareWriteReq = 0x16U;
constexpr uint8_t kAttOpPrepareWriteRsp = 0x17U;
constexpr uint8_t kAttOpExecuteWriteReq = 0x18U;
constexpr uint8_t kAttOpExecuteWriteRsp = 0x19U;
constexpr uint8_t kAttOpHandleValueNtf = 0x1BU;
constexpr uint8_t kAttOpHandleValueInd = 0x1DU;
constexpr uint8_t kAttOpHandleValueCfm = 0x1EU;
constexpr uint8_t kAttOpWriteCmd = 0x52U;

static inline bool attOpcodeIsClientProgressPdu(uint8_t opcode) {
  switch (opcode) {
    case kAttOpExchangeMtuReq:
    case kAttOpFindInfoReq:
    case kAttOpFindByTypeValueReq:
    case kAttOpReadByTypeReq:
    case kAttOpReadReq:
    case kAttOpReadBlobReq:
    case kAttOpReadMultipleReq:
    case kAttOpReadMultipleVariableReq:
    case kAttOpReadByGroupTypeReq:
    case kAttOpWriteReq:
    case kAttOpPrepareWriteReq:
    case kAttOpExecuteWriteReq:
    case kAttOpWriteCmd:
    case kAttOpSignedWriteCmd:
    case kAttOpHandleValueCfm:
      return true;
    default:
      return false;
  }
}

static inline bool attOpcodeIsServerResponsePdu(uint8_t opcode) {
  switch (opcode) {
    case kAttOpErrorRsp:
    case kAttOpExchangeMtuRsp:
    case kAttOpFindInfoRsp:
    case kAttOpFindByTypeValueRsp:
    case kAttOpReadByTypeRsp:
    case kAttOpReadRsp:
    case kAttOpReadBlobRsp:
    case kAttOpReadMultipleRsp:
    case kAttOpReadMultipleVariableRsp:
    case kAttOpReadByGroupTypeRsp:
    case kAttOpWriteRsp:
    case kAttOpPrepareWriteRsp:
    case kAttOpExecuteWriteRsp:
      return true;
    default:
      return false;
  }
}

constexpr uint8_t kAttErrInvalidHandle = 0x01U;
constexpr uint8_t kAttErrReadNotPermitted = 0x02U;
constexpr uint8_t kAttErrWriteNotPermitted = 0x03U;
constexpr uint8_t kAttErrInvalidPdu = 0x04U;
constexpr uint8_t kAttErrInsufficientAuthentication = 0x05U;
constexpr uint8_t kAttErrRequestNotSupported = 0x06U;
constexpr uint8_t kAttErrInvalidOffset = 0x07U;
constexpr uint8_t kAttErrInsufficientAuthorization = 0x08U;
constexpr uint8_t kAttErrPrepareQueueFull = 0x09U;
constexpr uint8_t kAttErrInvalidAttrValueLen = 0x0DU;
constexpr uint8_t kAttErrInsufficientEncryption = 0x0FU;
constexpr uint8_t kAttErrUnsupportedGroupType = 0x10U;
constexpr uint8_t kAttErrAttributeNotFound = 0x0AU;

constexpr uint16_t kUuidPrimaryService = 0x2800U;
constexpr uint16_t kUuidCharacteristic = 0x2803U;
constexpr uint16_t kUuidCharacteristicUserDescription = 0x2901U;
constexpr uint16_t kUuidCharacteristicPresentationFormat = 0x2904U;
constexpr uint16_t kUuidClientCharacteristicConfig = 0x2902U;
constexpr uint16_t kUuidReportReference = 0x2908U;
constexpr uint16_t kUuidGapService = 0x1800U;
constexpr uint16_t kUuidGattService = 0x1801U;
constexpr uint16_t kUuidBatteryService = 0x180FU;
constexpr uint16_t kUuidDeviceName = 0x2A00U;
constexpr uint16_t kUuidAppearance = 0x2A01U;
constexpr uint16_t kUuidPpcp = 0x2A04U;
constexpr uint16_t kUuidServiceChanged = 0x2A05U;
constexpr uint16_t kUuidBatteryLevel = 0x2A19U;

constexpr uint16_t kHandleGapService = 0x0001U;
constexpr uint16_t kHandleGapDeviceNameDecl = 0x0002U;
constexpr uint16_t kHandleGapDeviceNameValue = 0x0003U;
constexpr uint16_t kHandleGapAppearanceDecl = 0x0004U;
constexpr uint16_t kHandleGapAppearanceValue = 0x0005U;
constexpr uint16_t kHandleGapPpcpDecl = 0x0006U;
constexpr uint16_t kHandleGapPpcpValue = 0x0007U;
constexpr uint16_t kHandleGattService = 0x0008U;
constexpr uint16_t kHandleGattServiceChangedDecl = 0x0009U;
constexpr uint16_t kHandleGattServiceChangedValue = 0x000AU;
constexpr uint16_t kHandleGattServiceChangedCccd = 0x000BU;
constexpr uint16_t kHandleBatteryService = 0x0018U;
constexpr uint16_t kHandleBatteryLevelDecl = 0x0019U;
constexpr uint16_t kHandleBatteryLevelValue = 0x001AU;
constexpr uint16_t kHandleBatteryLevelCccd = 0x001BU;

constexpr uint8_t kGattCharacteristicPropRead = 0x02U;
constexpr uint8_t kGattCharacteristicPropWrite = 0x08U;
constexpr uint8_t kGattCharacteristicPropNotify = 0x10U;
constexpr uint8_t kGattCharacteristicPropIndicate = 0x20U;
constexpr uint8_t kBleUuid16Size = 2U;
constexpr uint8_t kBleUuid128Size = 16U;
constexpr uint8_t kAttrReadInvalidHandleLen = 0xFFU;
constexpr uint8_t kAttrReadInvalidOffsetLen = 0xFEU;
constexpr uint8_t kAttrReadNotPermittedLen = 0xFDU;
constexpr uint8_t kAttrReadInsufficientEncryptionLen = 0xFCU;
constexpr uint8_t kAttrReadInsufficientAuthenticationLen = 0xFBU;
constexpr uint8_t kAttFindInfoFormat16Bit = 0x01U;
constexpr uint8_t kAttFindInfoFormat128Bit = 0x02U;
constexpr uint8_t kBleUuidLength16 = 2U;
constexpr uint8_t kBleUuidLength128 = 16U;

uint16_t readLe16(const uint8_t* p);
void writeLe16(uint8_t* p, uint16_t v);

struct BlePrimaryServiceRecord {
  uint16_t startHandle;
  uint16_t endHandle;
  uint16_t uuid16;
};

struct BleCharacteristicRecord {
  uint16_t declarationHandle;
  uint8_t properties;
  uint16_t valueHandle;
  uint16_t uuid16;
};

struct BleAttributeUuidRecord {
  uint16_t handle;
  uint16_t uuid16;
};

uint8_t customUuidLength(uint8_t uuidLength) {
  if (uuidLength == kBleUuidLength16 || uuidLength == kBleUuidLength128) {
    return uuidLength;
  }
  return 0U;
}

[[maybe_unused]] void setCustomUuid16(uint8_t* uuidLength, uint8_t* uuidBytes,
                                     uint16_t uuid16) {
  if (uuidLength == nullptr || uuidBytes == nullptr) {
    return;
  }

  memset(uuidBytes, 0, kBleUuidLength128);
  *uuidLength = kBleUuidLength16;
  writeLe16(uuidBytes, uuid16);
}

[[maybe_unused]] bool setCustomUuid128(uint8_t* uuidLength, uint8_t* uuidBytes,
                                      const uint8_t* uuid128Canonical) {
  if (uuidLength == nullptr || uuidBytes == nullptr || uuid128Canonical == nullptr) {
    return false;
  }

  memset(uuidBytes, 0, kBleUuidLength128);
  *uuidLength = kBleUuidLength128;
  // Store 128-bit UUIDs in ATT/BLE little-endian order so wire encoding and
  // incoming ATT request matching do not need extra byte swapping later.
  for (uint8_t i = 0U; i < kBleUuidLength128; ++i) {
    uuidBytes[i] = uuid128Canonical[kBleUuidLength128 - 1U - i];
  }
  return true;
}

[[maybe_unused]] bool customUuidMatchesRaw(const uint8_t* uuidBytes,
                                           uint8_t uuidLength,
                                           const uint8_t* rawUuid,
                                           uint8_t rawUuidLength) {
  uuidLength = customUuidLength(uuidLength);
  if (uuidLength == 0U || rawUuid == nullptr || rawUuidLength != uuidLength) {
    return false;
  }
  return memcmp(uuidBytes, rawUuid, uuidLength) == 0;
}

[[maybe_unused]] uint8_t customCharacteristicDeclarationValueLength(
    uint8_t uuidLength) {
  uuidLength = customUuidLength(uuidLength);
  return uuidLength == 0U ? 0U : static_cast<uint8_t>(3U + uuidLength);
}

constexpr BlePrimaryServiceRecord kBlePrimaryServices[] = {
    {kHandleGapService, kHandleGapPpcpValue, kUuidGapService},
    {kHandleGattService, kHandleGattServiceChangedCccd, kUuidGattService},
    {kHandleBatteryService, kHandleBatteryLevelCccd, kUuidBatteryService},
};

constexpr BleCharacteristicRecord kBleCharacteristics[] = {
    {kHandleGapDeviceNameDecl, kGattCharacteristicPropRead,
     kHandleGapDeviceNameValue, kUuidDeviceName},
    {kHandleGapAppearanceDecl, kGattCharacteristicPropRead,
     kHandleGapAppearanceValue, kUuidAppearance},
    {kHandleGapPpcpDecl, kGattCharacteristicPropRead,
     kHandleGapPpcpValue, kUuidPpcp},
    {kHandleGattServiceChangedDecl, kGattCharacteristicPropIndicate,
     kHandleGattServiceChangedValue, kUuidServiceChanged},
    {kHandleBatteryLevelDecl, static_cast<uint8_t>(kGattCharacteristicPropRead |
                                                   kGattCharacteristicPropNotify),
     kHandleBatteryLevelValue, kUuidBatteryLevel},
};

constexpr BleAttributeUuidRecord kBleAttributeUuids[] = {
    {kHandleGapService, kUuidPrimaryService},
    {kHandleGapDeviceNameDecl, kUuidCharacteristic},
    {kHandleGapDeviceNameValue, kUuidDeviceName},
    {kHandleGapAppearanceDecl, kUuidCharacteristic},
    {kHandleGapAppearanceValue, kUuidAppearance},
    {kHandleGapPpcpDecl, kUuidCharacteristic},
    {kHandleGapPpcpValue, kUuidPpcp},
    {kHandleGattService, kUuidPrimaryService},
    {kHandleGattServiceChangedDecl, kUuidCharacteristic},
    {kHandleGattServiceChangedValue, kUuidServiceChanged},
    {kHandleGattServiceChangedCccd, kUuidClientCharacteristicConfig},
    {kHandleBatteryService, kUuidPrimaryService},
    {kHandleBatteryLevelDecl, kUuidCharacteristic},
    {kHandleBatteryLevelValue, kUuidBatteryLevel},
    {kHandleBatteryLevelCccd, kUuidClientCharacteristicConfig},
};

uint8_t bleChannelToFrequency(xiao_nrf54l15::BleAdvertisingChannel channel) {
  switch (channel) {
    case xiao_nrf54l15::BleAdvertisingChannel::k37:
      return 2U;
    case xiao_nrf54l15::BleAdvertisingChannel::k38:
      return 26U;
    case xiao_nrf54l15::BleAdvertisingChannel::k39:
    default:
      return 80U;
  }
}

uint8_t bleChannelToIndex(xiao_nrf54l15::BleAdvertisingChannel channel) {
  switch (channel) {
    case xiao_nrf54l15::BleAdvertisingChannel::k37:
      return 37U;
    case xiao_nrf54l15::BleAdvertisingChannel::k38:
      return 38U;
    case xiao_nrf54l15::BleAdvertisingChannel::k39:
    default:
      return 39U;
  }
}

uint8_t bleDataChannelToFrequency(uint8_t dataChannel) {
  if (dataChannel > 36U) {
    return 4U;
  }
  if (dataChannel <= 10U) {
    return static_cast<uint8_t>(4U + (2U * dataChannel));
  }
  return static_cast<uint8_t>(6U + (2U * dataChannel));
}

bool ieee802154ChannelValid(uint8_t channel) {
  return (channel >= 11U) && (channel <= 26U);
}

uint8_t ieee802154ChannelToFrequency(uint8_t channel) {
  return static_cast<uint8_t>(5U + ((channel - 11U) * 5U));
}

uint32_t radioTxPowerRegFromDbm(int8_t dbm) {
  if (dbm >= 8) {
    return RADIO_TXPOWER_TXPOWER_Pos8dBm;
  }
  if (dbm >= 7) {
    return RADIO_TXPOWER_TXPOWER_Pos7dBm;
  }
  if (dbm >= 6) {
    return RADIO_TXPOWER_TXPOWER_Pos6dBm;
  }
  if (dbm >= 5) {
    return RADIO_TXPOWER_TXPOWER_Pos5dBm;
  }
  if (dbm >= 4) {
    return RADIO_TXPOWER_TXPOWER_Pos4dBm;
  }
  if (dbm >= 3) {
    return RADIO_TXPOWER_TXPOWER_Pos3dBm;
  }
  if (dbm >= 2) {
    return RADIO_TXPOWER_TXPOWER_Pos2dBm;
  }
  if (dbm >= 1) {
    return RADIO_TXPOWER_TXPOWER_Pos1dBm;
  }
  if (dbm >= 0) {
    return RADIO_TXPOWER_TXPOWER_0dBm;
  }
  if (dbm >= -1) {
    return RADIO_TXPOWER_TXPOWER_Neg1dBm;
  }
  if (dbm >= -2) {
    return RADIO_TXPOWER_TXPOWER_Neg2dBm;
  }
  if (dbm >= -3) {
    return RADIO_TXPOWER_TXPOWER_Neg3dBm;
  }
  if (dbm >= -4) {
    return RADIO_TXPOWER_TXPOWER_Neg4dBm;
  }
  if (dbm >= -5) {
    return RADIO_TXPOWER_TXPOWER_Neg5dBm;
  }
  if (dbm >= -6) {
    return RADIO_TXPOWER_TXPOWER_Neg6dBm;
  }
  if (dbm >= -7) {
    return RADIO_TXPOWER_TXPOWER_Neg7dBm;
  }
  if (dbm >= -8) {
    return RADIO_TXPOWER_TXPOWER_Neg8dBm;
  }
  if (dbm >= -9) {
    return RADIO_TXPOWER_TXPOWER_Neg9dBm;
  }
  if (dbm >= -10) {
    return RADIO_TXPOWER_TXPOWER_Neg10dBm;
  }
  if (dbm >= -12) {
    return RADIO_TXPOWER_TXPOWER_Neg12dBm;
  }
  if (dbm >= -14) {
    return RADIO_TXPOWER_TXPOWER_Neg14dBm;
  }
  if (dbm >= -16) {
    return RADIO_TXPOWER_TXPOWER_Neg16dBm;
  }
  if (dbm >= -18) {
    return RADIO_TXPOWER_TXPOWER_Neg18dBm;
  }
  if (dbm >= -20) {
    return RADIO_TXPOWER_TXPOWER_Neg20dBm;
  }
  if (dbm >= -28) {
    return RADIO_TXPOWER_TXPOWER_Neg28dBm;
  }
  if (dbm >= -40) {
    return RADIO_TXPOWER_TXPOWER_Neg40dBm;
  }
  return RADIO_TXPOWER_TXPOWER_Neg46dBm;
}

uint16_t readLe16(const uint8_t* p) {
  return static_cast<uint16_t>(p[0]) |
         (static_cast<uint16_t>(p[1]) << 8U);
}

uint32_t readLe24(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) |
         (static_cast<uint32_t>(p[1]) << 8U) |
         (static_cast<uint32_t>(p[2]) << 16U);
}

uint32_t readLe32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) |
         (static_cast<uint32_t>(p[1]) << 8U) |
         (static_cast<uint32_t>(p[2]) << 16U) |
         (static_cast<uint32_t>(p[3]) << 24U);
}

void writeLe16(uint8_t* p, uint16_t v) {
  if (p == nullptr) {
    return;
  }
  p[0] = static_cast<uint8_t>(v & 0xFFU);
  p[1] = static_cast<uint8_t>((v >> 8U) & 0xFFU);
}

uint8_t bleConnectionPayloadProtocolOpcode(uint8_t llid, const uint8_t* payload,
                                           uint8_t length) {
  if (payload == nullptr || length == 0U) {
    return 0U;
  }
  if (llid == kBlePduLlControl) {
    return payload[0];
  }
  if ((llid == kBlePduDataStartOrComplete) && (length >= 5U)) {
    const uint16_t l2capLen = readLe16(&payload[0]);
    const uint16_t cid = readLe16(&payload[2]);
    const uint8_t availableAttLen =
        (length > kBleL2capHeaderLen)
            ? static_cast<uint8_t>(length - kBleL2capHeaderLen)
            : 0U;
    if ((cid == kBleL2capCidAtt) && (l2capLen > 0U) && (availableAttLen > 0U)) {
      return payload[kBleL2capHeaderLen];
    }
  }
  return payload[0];
}

void writeLe24(uint8_t* p, uint32_t v) {
  if (p == nullptr) {
    return;
  }
  p[0] = static_cast<uint8_t>(v & 0xFFU);
  p[1] = static_cast<uint8_t>((v >> 8U) & 0xFFU);
  p[2] = static_cast<uint8_t>((v >> 16U) & 0xFFU);
}

void writeLe32(uint8_t* p, uint32_t v) {
  if (p == nullptr) {
    return;
  }
  p[0] = static_cast<uint8_t>(v & 0xFFU);
  p[1] = static_cast<uint8_t>((v >> 8U) & 0xFFU);
  p[2] = static_cast<uint8_t>((v >> 16U) & 0xFFU);
  p[3] = static_cast<uint8_t>((v >> 24U) & 0xFFU);
}

uint32_t crc32(const uint8_t* data, size_t length) {
  if (data == nullptr || length == 0U) {
    return 0U;
  }

  uint32_t crc = 0xFFFFFFFFUL;
  for (size_t i = 0U; i < length; ++i) {
    crc ^= static_cast<uint32_t>(data[i]);
    for (uint8_t bit = 0U; bit < 8U; ++bit) {
      const uint32_t mask = (crc & 1U) ? 0xFFFFFFFFUL : 0UL;
      crc = (crc >> 1U) ^ (0xEDB88320UL & mask);
    }
  }
  return ~crc;
}

bool bleBondRecordLooksSane(const xiao_nrf54l15::BleBondRecord& record) {
  if (record.peerAddressRandom > 1U || record.peerIdentityAddressRandom > 1U ||
      record.localAddressRandom > 1U || record.peerIrkValid > 1U ||
      record.peerCsrkValid > 1U || record.localCsrkValid > 1U ||
      record.peerSignCounterValid > 1U) {
    return false;
  }
  if (record.keySize < 7U || record.keySize > 16U) {
    return false;
  }
  bool keyNonZero = false;
  for (uint8_t i = 0U; i < 16U; ++i) {
    if (record.ltk[i] != 0U) {
      keyNonZero = true;
      break;
    }
  }
  if (!keyNonZero) {
    return false;
  }

  if (record.peerSignCounterValid != 0U && record.peerCsrkValid == 0U) {
    return false;
  }
  return true;
}

void clearBondRecordV3Tail(xiao_nrf54l15::BleBondRecord* record) {
  if (record == nullptr) {
    return;
  }
  memset(record->peerCsrk, 0, sizeof(record->peerCsrk));
  memset(record->localCsrk, 0, sizeof(record->localCsrk));
  record->peerSignCounter = 0U;
  record->localSignCounter = 0U;
  record->peerCsrkValid = 0U;
  record->localCsrkValid = 0U;
  record->peerSignCounterValid = 0U;
  record->signingReserved = 0U;
}

void clearBondRecordV2Tail(xiao_nrf54l15::BleBondRecord* record) {
  if (record == nullptr) {
    return;
  }
  memset(record->peerIdentityAddress, 0, sizeof(record->peerIdentityAddress));
  record->peerIdentityAddressRandom = 0U;
  memset(record->peerIrk, 0, sizeof(record->peerIrk));
  record->peerIrkValid = 0U;
  memset(record->reserved, 0, sizeof(record->reserved));
  clearBondRecordV3Tail(record);
}

void convertLegacyBondRecord(const BleBondRecordV1& legacy,
                             xiao_nrf54l15::BleBondRecord* outRecord) {
  if (outRecord == nullptr) {
    return;
  }
  memset(outRecord, 0, sizeof(*outRecord));
  memcpy(outRecord->peerAddress, legacy.peerAddress, sizeof(outRecord->peerAddress));
  outRecord->peerAddressRandom = legacy.peerAddressRandom;
  memcpy(outRecord->localAddress, legacy.localAddress, sizeof(outRecord->localAddress));
  outRecord->localAddressRandom = legacy.localAddressRandom;
  memcpy(outRecord->ltk, legacy.ltk, sizeof(outRecord->ltk));
  memcpy(outRecord->rand, legacy.rand, sizeof(outRecord->rand));
  outRecord->ediv = legacy.ediv;
  outRecord->keySize = legacy.keySize;
  clearBondRecordV2Tail(outRecord);
}

void convertV2BondRecord(const BleBondRecordV2& legacy,
                         xiao_nrf54l15::BleBondRecord* outRecord) {
  if (outRecord == nullptr) {
    return;
  }
  memset(outRecord, 0, sizeof(*outRecord));
  memcpy(outRecord->peerAddress, legacy.peerAddress,
         sizeof(outRecord->peerAddress));
  outRecord->peerAddressRandom = legacy.peerAddressRandom;
  memcpy(outRecord->peerIdentityAddress, legacy.peerIdentityAddress,
         sizeof(outRecord->peerIdentityAddress));
  outRecord->peerIdentityAddressRandom = legacy.peerIdentityAddressRandom;
  memcpy(outRecord->localAddress, legacy.localAddress,
         sizeof(outRecord->localAddress));
  outRecord->localAddressRandom = legacy.localAddressRandom;
  memcpy(outRecord->ltk, legacy.ltk, sizeof(outRecord->ltk));
  memcpy(outRecord->rand, legacy.rand, sizeof(outRecord->rand));
  memcpy(outRecord->peerIrk, legacy.peerIrk, sizeof(outRecord->peerIrk));
  outRecord->peerIrkValid = legacy.peerIrkValid;
  outRecord->ediv = legacy.ediv;
  outRecord->keySize = legacy.keySize;
  memcpy(outRecord->reserved, legacy.reserved, sizeof(outRecord->reserved));
  clearBondRecordV3Tail(outRecord);
}

NRF_RRAMC_Type* bondRramc() {
  return NRF_RRAMC;
}

bool waitForBondRramcReady(NRF_RRAMC_Type* rramc, uint32_t spinLimit) {
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

bool waitForBondRramcReadyNext(NRF_RRAMC_Type* rramc, uint32_t spinLimit) {
  if (rramc == nullptr) {
    return false;
  }
  while (spinLimit-- > 0U) {
    if (((rramc->READYNEXT & RRAMC_READYNEXT_READYNEXT_Msk) >>
         RRAMC_READYNEXT_READYNEXT_Pos) == RRAMC_READYNEXT_READYNEXT_Ready) {
      return true;
    }
  }
  return false;
}

void copyFromVolatileMemory(const volatile uint8_t* src, uint8_t* dst, size_t len) {
  if (src == nullptr || dst == nullptr) {
    return;
  }
  for (size_t i = 0U; i < len; ++i) {
    dst[i] = src[i];
  }
}

bool writeBondStorageToRram(volatile void* target, const void* src, size_t len) {
  NRF_RRAMC_Type* const rramc = bondRramc();
  if (rramc == nullptr || target == nullptr || src == nullptr || len == 0U ||
      bleRunningInIsr()) {
    return false;
  }

  const uint32_t targetAddress =
      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(target));
  const uint32_t bufferedWordCount = static_cast<uint32_t>(
      ((targetAddress & 0x0FUL) + len + 15U) / 16U);
  if (bufferedWordCount == 0U || bufferedWordCount > 32U) {
    return false;
  }
  if (!nrf54l15_rram_transaction_try_lock()) {
    return false;
  }
  g_bleBondStorageWriteInProgress = true;

  const uint8_t* const source = reinterpret_cast<const uint8_t*>(src);
  const uint32_t prevConfig = rramc->CONFIG;
  const uint32_t writeConfig =
      (prevConfig & ~RRAMC_CONFIG_WRITEBUFSIZE_Msk) |
      RRAMC_CONFIG_WEN_Msk |
      (bufferedWordCount << RRAMC_CONFIG_WRITEBUFSIZE_Pos);
  bool writeOk = true;
  bool bufferNeedsClear = false;

  if ((prevConfig & RRAMC_CONFIG_WRITEBUFSIZE_Msk) != 0U &&
      ((rramc->BUFSTATUS.WRITEBUFEMPTY &
        RRAMC_BUFSTATUS_WRITEBUFEMPTY_EMPTY_Msk) == 0U)) {
    rramc->EVENTS_READY = 0U;
    rramc->TASKS_COMMITWRITEBUF =
        RRAMC_TASKS_COMMITWRITEBUF_TASKS_COMMITWRITEBUF_Trigger;
    writeOk = waitForBondRramcReady(rramc, kBleBondRramcSpinLimit);
    bufferNeedsClear = !writeOk;
  }
  if (writeOk) {
    rramc->CONFIG = writeConfig;
    bufferNeedsClear = true;
    if (!waitForBondRramcReady(rramc, kBleBondRramcSpinLimit)) {
      writeOk = false;
    }
  }

  if (writeOk) {
    rramc->EVENTS_ACCESSERROR = 0U;
    for (size_t i = 0U; i < len; ++i) {
      if (!waitForBondRramcReadyNext(rramc, kBleBondRramcSpinLimit)) {
        writeOk = false;
        break;
      }
      *reinterpret_cast<volatile uint8_t*>(targetAddress + static_cast<uint32_t>(i)) =
          source[i];
    }
    if (rramc->EVENTS_ACCESSERROR != 0U) {
      writeOk = false;
    }
  }

  if (writeOk) {
    rramc->EVENTS_READY = 0U;
    rramc->TASKS_COMMITWRITEBUF =
        RRAMC_TASKS_COMMITWRITEBUF_TASKS_COMMITWRITEBUF_Trigger;
    writeOk = waitForBondRramcReady(rramc, kBleBondRramcSpinLimit);
    if (writeOk) {
      bufferNeedsClear = false;
    }
  }

  if (bufferNeedsClear) {
    rramc->EVENTS_READY = 0U;
    rramc->TASKS_CLRWRITEBUF =
        RRAMC_TASKS_CLRWRITEBUF_TASKS_CLRWRITEBUF_Trigger;
    if (!waitForBondRramcReady(rramc, kBleBondRramcSpinLimit)) {
      writeOk = false;
    }
  }

  rramc->EVENTS_ACCESSERROR = 0U;
  rramc->CONFIG = prevConfig;
  if (!waitForBondRramcReady(rramc, kBleBondRramcSpinLimit)) {
    writeOk = false;
  }
  const uint32_t unlockIrqState = bleEnterCritical();
  g_bleBondStorageWriteInProgress = false;
  bleExitCritical(unlockIrqState);
  nrf54l15_rram_transaction_unlock();
  return writeOk;
}

bool bleBondDbGenerationNewer(uint32_t lhs, uint32_t rhs) {
  return lhs != rhs && static_cast<int32_t>(lhs - rhs) > 0;
}

bool bleBondDbMagicValid(uint32_t magic) {
  return magic == kBleBondDbActiveMagic ||
         magic == kBleBondDbTombstoneMagic ||
         magic == kBleBondDbClearAllMagic;
}

bool bleBondDbBytesAllZero(const uint8_t* bytes, size_t length) {
  if (bytes == nullptr) {
    return false;
  }
  uint8_t aggregate = 0U;
  for (size_t i = 0U; i < length; ++i) {
    aggregate = static_cast<uint8_t>(aggregate | bytes[i]);
  }
  return aggregate == 0U;
}

uint8_t bleBondDbCccdValue(const BleBondDbPeerState& state, uint8_t index) {
  if (index >= xiao_nrf54l15::BleRadio::kCustomGattMaxCharacteristics) {
    return 0U;
  }
  const uint8_t bitOffset = static_cast<uint8_t>(index * 2U);
  return static_cast<uint8_t>(
      (state.cccdValues[bitOffset >> 3U] >> (bitOffset & 0x07U)) & 0x03U);
}

void bleBondDbSetCccdValue(BleBondDbPeerState* state, uint8_t index,
                           uint8_t value) {
  if (state == nullptr ||
      index >= xiao_nrf54l15::BleRadio::kCustomGattMaxCharacteristics) {
    return;
  }
  const uint8_t bitOffset = static_cast<uint8_t>(index * 2U);
  const uint8_t byteIndex = static_cast<uint8_t>(bitOffset >> 3U);
  const uint8_t shift = static_cast<uint8_t>(bitOffset & 0x07U);
  state->cccdValues[byteIndex] = static_cast<uint8_t>(
      (state->cccdValues[byteIndex] & ~(0x03U << shift)) |
      ((value & 0x03U) << shift));
}

bool bleBondDbKeyLooksSane(const BleBondDbLongTermKey& key) {
  return !bleBondDbBytesAllZero(key.ltk, sizeof(key.ltk));
}

bool bleBondDbCoreLooksSane(const BleBondDbCore& core) {
  const uint8_t allowedSecurity =
      kBleBondDbSecurityAuthenticated | kBleBondDbSecuritySecureConnections;
  const uint8_t allowedKeys =
      kBleBondDbKeyPeerIrkValid | kBleBondDbKeyLocalOrSharedValid |
      kBleBondDbKeyPeerLegacyValid |
      kBleBondDbKeySharedSecureConnections |
      kBleBondDbKeyLegacyProjectionUnknown;
  const uint8_t allowedSigning =
      kBleBondDbSigningPeerCsrkValid |
      kBleBondDbSigningLocalCsrkValid |
      kBleBondDbSigningPeerCounterValid;
  if (core.peerAddressRandom > 1U ||
      core.peerIdentityAddressRandom > 1U ||
      core.localAddressRandom > 1U || core.keySize < 7U ||
      core.keySize > 16U || (core.securityFlags & ~allowedSecurity) != 0U ||
      (core.keyValidityFlags & ~allowedKeys) != 0U ||
      (core.signingFlags & ~allowedSigning) != 0U ||
      core.pairedLocalRole >
          static_cast<uint8_t>(xiao_nrf54l15::BleConnectionRole::kCentral) ||
      !bleBondDbBytesAllZero(core.reserved, sizeof(core.reserved))) {
    return false;
  }
  if ((core.keyValidityFlags & kBleBondDbKeyLocalOrSharedValid) != 0U &&
      !bleBondDbKeyLooksSane(core.localOrSharedKey)) {
    return false;
  }
  if ((core.keyValidityFlags & kBleBondDbKeyPeerLegacyValid) != 0U &&
      !bleBondDbKeyLooksSane(core.peerLegacyKey)) {
    return false;
  }
  if ((core.keyValidityFlags & kBleBondDbKeySharedSecureConnections) != 0U &&
      ((core.securityFlags & kBleBondDbSecuritySecureConnections) == 0U ||
       (core.keyValidityFlags & kBleBondDbKeyLocalOrSharedValid) == 0U)) {
    return false;
  }
  if ((core.signingFlags & kBleBondDbSigningPeerCounterValid) != 0U &&
      (core.signingFlags & kBleBondDbSigningPeerCsrkValid) == 0U) {
    return false;
  }
  return (core.keyValidityFlags & (kBleBondDbKeyLocalOrSharedValid |
                                   kBleBondDbKeyPeerLegacyValid)) != 0U;
}

bool bleBondDbPeerStateLooksSane(const BleBondDbPeerState& state) {
  if ((state.fixedCccdFlags & ~kBleCccdFixedMask) != 0U ||
      state.cccdCount >
          xiao_nrf54l15::BleRadio::kCustomGattMaxCharacteristics) {
    return false;
  }
  const bool hasPending = state.serviceChangedStartHandle != 0U ||
                          state.serviceChangedEndHandle != 0U;
  if (hasPending &&
      (state.serviceChangedStartHandle == 0U ||
       state.serviceChangedStartHandle > state.serviceChangedEndHandle)) {
    return false;
  }
  for (uint8_t i = 0U;
       i < xiao_nrf54l15::BleRadio::kCustomGattMaxCharacteristics; ++i) {
    const uint16_t handle = state.cccdHandles[i];
    const uint8_t value = bleBondDbCccdValue(state, i);
    if (i >= state.cccdCount) {
      if (handle != 0U || value != 0U) {
        return false;
      }
      continue;
    }
    if (handle == 0U || handle == kHandleGattServiceChangedCccd ||
        handle == kHandleBatteryLevelCccd || value == 0U) {
      return false;
    }
    for (uint8_t previous = 0U; previous < i; ++previous) {
      if (state.cccdHandles[previous] == handle) {
        return false;
      }
    }
  }
  return true;
}

bool bleBondDbPayloadLooksSane(const BleBondDbPayload& payload) {
  return bleBondDbCoreLooksSane(payload.core) &&
         bleBondDbPeerStateLooksSane(payload.peerState) &&
         bleBondDbBytesAllZero(payload.reserved, sizeof(payload.reserved));
}

bool bleBondDbMarkerPayloadLooksSane(const BleBondDbPayload& payload) {
  BleBondDbPayload normalized = payload;
  normalized.clearGeneration = 0U;
  return bleBondDbBytesAllZero(
      reinterpret_cast<const uint8_t*>(&normalized), sizeof(normalized));
}

void readBleBondDbReplica(uint8_t replicaIndex, uint8_t bondId,
                          BleBondDbReplica* outReplica) {
  if (outReplica == nullptr || replicaIndex >= kBleBondDbReplicaCount ||
      bondId >= xiao_nrf54l15::BleRadio::kBleMaxBonds) {
    return;
  }
  copyFromVolatileMemory(
      reinterpret_cast<const volatile uint8_t*>(
          &g_bleBondDbStorage.replicas[replicaIndex][bondId]),
      reinterpret_cast<uint8_t*>(outReplica), sizeof(*outReplica));
}

bool bleBondDbHeaderValid(const BleBondDbHeader& header);

bool bleBondDbReplicaValid(const BleBondDbReplica& replica) {
  if (!bleBondDbMagicValid(replica.header.magic) ||
      replica.header.generation == 0U ||
      replica.header.headerCrc32 !=
          crc32(reinterpret_cast<const uint8_t*>(&replica.header), 12U)) {
    return false;
  }
  const uint32_t payloadCrc =
      crc32(reinterpret_cast<const uint8_t*>(&replica.payload),
            sizeof(replica.payload));
  if (payloadCrc != replica.header.payloadCrc32) {
    return false;
  }
  const uint32_t clearGeneration = replica.payload.clearGeneration;
  if (replica.header.magic == kBleBondDbClearAllMagic) {
    return clearGeneration == replica.header.generation &&
           bleBondDbMarkerPayloadLooksSane(replica.payload);
  }
  if (clearGeneration != 0U &&
      !bleBondDbGenerationNewer(replica.header.generation,
                                clearGeneration)) {
    return false;
  }
  if (replica.header.magic == kBleBondDbActiveMagic) {
    return bleBondDbPayloadLooksSane(replica.payload);
  }
  if (!bleBondDbMarkerPayloadLooksSane(replica.payload)) {
    return false;
  }
  return true;
}

bool bleBondDbHeaderValid(const BleBondDbHeader& header) {
  return bleBondDbMagicValid(header.magic) && header.generation != 0U &&
         header.headerCrc32 ==
             crc32(reinterpret_cast<const uint8_t*>(&header), 12U);
}

uint32_t bleBondDbLatestClearGeneration() {
  uint32_t generation = 0U;
  for (uint8_t replicaIndex = 0U; replicaIndex < kBleBondDbReplicaCount;
       ++replicaIndex) {
    for (uint8_t bondId = 0U;
         bondId < xiao_nrf54l15::BleRadio::kBleMaxBonds; ++bondId) {
      BleBondDbReplica replica{};
      readBleBondDbReplica(replicaIndex, bondId, &replica);
      uint32_t candidate = 0U;
      if (bleBondDbHeaderValid(replica.header) &&
          replica.header.magic == kBleBondDbClearAllMagic) {
        candidate = replica.header.generation;
      } else if (bleBondDbReplicaValid(replica)) {
        candidate = replica.payload.clearGeneration;
      }
      if (candidate != 0U &&
          (generation == 0U ||
           bleBondDbGenerationNewer(candidate, generation))) {
        generation = candidate;
      }
    }
  }
  return generation;
}

bool readNewestBleBondDbReplica(uint8_t bondId, BleBondDbReplica* outReplica,
                                uint8_t* outReplicaIndex = nullptr) {
  if (bondId >= xiao_nrf54l15::BleRadio::kBleMaxBonds ||
      outReplica == nullptr) {
    return false;
  }
  BleBondDbReplica candidates[kBleBondDbReplicaCount]{};
  bool headerValid[kBleBondDbReplicaCount]{};
  for (uint8_t i = 0U; i < kBleBondDbReplicaCount; ++i) {
    readBleBondDbReplica(i, bondId, &candidates[i]);
    headerValid[i] = bleBondDbHeaderValid(candidates[i].header);
  }
  if (!headerValid[0] && !headerValid[1]) {
    return false;
  }
  uint8_t selected = !headerValid[0]
                         ? 1U
                         : (!headerValid[1] || !bleBondDbGenerationNewer(
                                                  candidates[1].header.generation,
                                                  candidates[0].header.generation)
                                ? 0U
                                : 1U);
  const uint32_t clearGeneration = bleBondDbLatestClearGeneration();
  if (clearGeneration != 0U &&
      !bleBondDbGenerationNewer(candidates[selected].header.generation,
                                clearGeneration)) {
    return false;
  }
  // A newer committed header always supersedes the older replica. If its
  // payload is corrupt, fail closed instead of resurrecting stale key data.
  if (!bleBondDbReplicaValid(candidates[selected])) {
    return false;
  }
  memcpy(outReplica, &candidates[selected], sizeof(*outReplica));
  if (outReplicaIndex != nullptr) {
    *outReplicaIndex = selected;
  }
  return true;
}

bool readActiveBleBondDbPayload(uint8_t bondId, BleBondDbPayload* outPayload,
                                uint32_t* outGeneration = nullptr) {
  BleBondDbReplica replica{};
  if (!readNewestBleBondDbReplica(bondId, &replica) ||
      replica.header.magic != kBleBondDbActiveMagic) {
    return false;
  }
  if (outPayload != nullptr) {
    memcpy(outPayload, &replica.payload, sizeof(*outPayload));
  }
  if (outGeneration != nullptr) {
    *outGeneration = replica.header.generation;
  }
  return true;
}

uint32_t nextBleBondDbGeneration() {
  uint32_t newest = 0U;
  for (uint8_t replicaIndex = 0U; replicaIndex < kBleBondDbReplicaCount;
       ++replicaIndex) {
    for (uint8_t bondId = 0U;
         bondId < xiao_nrf54l15::BleRadio::kBleMaxBonds; ++bondId) {
      BleBondDbReplica replica{};
      readBleBondDbReplica(replicaIndex, bondId, &replica);
      if (bleBondDbHeaderValid(replica.header) &&
          (newest == 0U || bleBondDbGenerationNewer(
                               replica.header.generation, newest))) {
        newest = replica.header.generation;
      }
    }
  }
  ++newest;
  return newest == 0U ? 1U : newest;
}

uint8_t selectBleBondDbWriteReplica(uint8_t bondId) {
  BleBondDbReplica replicas[kBleBondDbReplicaCount]{};
  bool headerValid[kBleBondDbReplicaCount]{};
  for (uint8_t i = 0U; i < kBleBondDbReplicaCount; ++i) {
    readBleBondDbReplica(i, bondId, &replicas[i]);
    headerValid[i] = bleBondDbHeaderValid(replicas[i].header);
  }
  if (!headerValid[0]) {
    return 0U;
  }
  if (!headerValid[1]) {
    return 1U;
  }
  return bleBondDbGenerationNewer(replicas[0].header.generation,
                                  replicas[1].header.generation)
             ? 1U
             : 0U;
}

bool writeBleBondDbReplica(uint8_t bondId, uint32_t magic,
                           const BleBondDbPayload* payload,
                           uint32_t* outGeneration = nullptr) {
  if (bondId >= xiao_nrf54l15::BleRadio::kBleMaxBonds ||
      !bleBondDbMagicValid(magic) ||
      (magic == kBleBondDbActiveMagic &&
       (payload == nullptr || !bleBondDbPayloadLooksSane(*payload)))) {
    return false;
  }
  const uint8_t targetReplica = selectBleBondDbWriteReplica(bondId);
  const uint32_t generation = nextBleBondDbGeneration();
  const uint32_t inheritedClearGeneration =
      bleBondDbLatestClearGeneration();
  volatile BleBondDbReplica* const target =
      &g_bleBondDbStorage.replicas[targetReplica][bondId];
  const BleBondDbHeader invalidHeader{};
  if (!writeBondStorageToRram(&target->header, &invalidHeader,
                              sizeof(invalidHeader))) {
    return false;
  }
  BleBondDbHeader verifyHeader{};
  copyFromVolatileMemory(
      reinterpret_cast<const volatile uint8_t*>(&target->header),
      reinterpret_cast<uint8_t*>(&verifyHeader), sizeof(verifyHeader));
  if (memcmp(&verifyHeader, &invalidHeader, sizeof(invalidHeader)) != 0) {
    return false;
  }

  BleBondDbPayload image{};
  if (magic == kBleBondDbActiveMagic) {
    memcpy(&image, payload, sizeof(image));
  }
  image.clearGeneration = magic == kBleBondDbClearAllMagic
                              ? generation
                              : inheritedClearGeneration;
  if (!writeBondStorageToRram(&target->payload, &image, sizeof(image))) {
    return false;
  }
  BleBondDbPayload verifyPayload{};
  copyFromVolatileMemory(
      reinterpret_cast<const volatile uint8_t*>(&target->payload),
      reinterpret_cast<uint8_t*>(&verifyPayload), sizeof(verifyPayload));
  if (memcmp(&verifyPayload, &image, sizeof(image)) != 0) {
    return false;
  }

  BleBondDbHeader finalHeader{};
  finalHeader.magic = magic;
  finalHeader.generation = generation;
  finalHeader.payloadCrc32 = crc32(
      reinterpret_cast<const uint8_t*>(&image), sizeof(image));
  finalHeader.headerCrc32 =
      crc32(reinterpret_cast<const uint8_t*>(&finalHeader), 12U);
  if (!writeBondStorageToRram(&target->header, &finalHeader,
                              sizeof(finalHeader))) {
    return false;
  }
  BleBondDbReplica verifyReplica{};
  readBleBondDbReplica(targetReplica, bondId, &verifyReplica);
  if (!bleBondDbReplicaValid(verifyReplica) ||
      memcmp(&verifyReplica.header, &finalHeader, sizeof(finalHeader)) != 0) {
    return false;
  }
  if (outGeneration != nullptr) {
    *outGeneration = finalHeader.generation;
  }
  return true;
}

bool eraseBleBondDbRecord(uint8_t bondId) {
  return writeBleBondDbReplica(bondId, kBleBondDbTombstoneMagic, nullptr);
}

bool clearBleBondDbRecords() {
  return writeBleBondDbReplica(0U, kBleBondDbClearAllMagic, nullptr);
}

bool bleBondDbAddressNonZero(const uint8_t address[6]) {
  return address != nullptr && !bleBondDbBytesAllZero(address, 6U);
}

bool bleBondDbIdentityMatches(const BleBondDbCore& core,
                              const xiao_nrf54l15::BleBondRecord& record) {
  if ((core.keyValidityFlags & kBleBondDbKeyPeerIrkValid) != 0U &&
      record.peerIrkValid != 0U &&
      memcmp(core.peerIrk, record.peerIrk, sizeof(core.peerIrk)) == 0) {
    return true;
  }
  if (bleBondDbAddressNonZero(core.peerIdentityAddress) &&
      bleBondDbAddressNonZero(record.peerIdentityAddress) &&
      core.peerIdentityAddressRandom == record.peerIdentityAddressRandom &&
      memcmp(core.peerIdentityAddress, record.peerIdentityAddress,
             sizeof(core.peerIdentityAddress)) == 0) {
    return true;
  }
  return core.peerAddressRandom == record.peerAddressRandom &&
         memcmp(core.peerAddress, record.peerAddress,
                sizeof(core.peerAddress)) == 0;
}

void bleBondDbCopyKeyFromRecord(
    const xiao_nrf54l15::BleBondRecord& record,
    BleBondDbLongTermKey* outKey) {
  if (outKey == nullptr) {
    return;
  }
  memcpy(outKey->ltk, record.ltk, sizeof(outKey->ltk));
  memcpy(outKey->rand, record.rand, sizeof(outKey->rand));
  outKey->ediv = record.ediv;
}

void bleBondDbUpdateCoreFromRecord(
    const xiao_nrf54l15::BleBondRecord& record,
    xiao_nrf54l15::BleConnectionRole localRole, BleBondDbCore* core,
    bool preserveExistingKeys) {
  if (core == nullptr) {
    return;
  }
  BleBondDbCore updated{};
  if (preserveExistingKeys) {
    memcpy(&updated, core, sizeof(updated));
  }
  memcpy(updated.peerAddress, record.peerAddress,
         sizeof(updated.peerAddress));
  updated.peerAddressRandom = record.peerAddressRandom & 0x01U;
  memcpy(updated.peerIdentityAddress, record.peerIdentityAddress,
         sizeof(updated.peerIdentityAddress));
  updated.peerIdentityAddressRandom =
      record.peerIdentityAddressRandom & 0x01U;
  memcpy(updated.localAddress, record.localAddress,
         sizeof(updated.localAddress));
  updated.localAddressRandom = record.localAddressRandom & 0x01U;
  updated.keySize = record.keySize;
  updated.securityFlags = 0U;
  if ((record.reserved[0] & kBleBondRecordFlagAuthenticated) != 0U) {
    updated.securityFlags |= kBleBondDbSecurityAuthenticated;
  }
  if ((record.reserved[0] & kBleBondRecordFlagSecureConnections) != 0U) {
    updated.securityFlags |= kBleBondDbSecuritySecureConnections;
  }
  memcpy(updated.peerIrk, record.peerIrk, sizeof(updated.peerIrk));
  if (record.peerIrkValid != 0U) {
    updated.keyValidityFlags |= kBleBondDbKeyPeerIrkValid;
  } else {
    updated.keyValidityFlags &=
        static_cast<uint8_t>(~kBleBondDbKeyPeerIrkValid);
  }
  memcpy(updated.peerCsrk, record.peerCsrk, sizeof(updated.peerCsrk));
  memcpy(updated.localCsrk, record.localCsrk, sizeof(updated.localCsrk));
  updated.peerSignCounter = record.peerSignCounter;
  updated.localSignCounter = record.localSignCounter;
  updated.signingFlags = 0U;
  if (record.peerCsrkValid != 0U) {
    updated.signingFlags |= kBleBondDbSigningPeerCsrkValid;
  }
  if (record.localCsrkValid != 0U) {
    updated.signingFlags |= kBleBondDbSigningLocalCsrkValid;
  }
  if (record.peerSignCounterValid != 0U) {
    updated.signingFlags |= kBleBondDbSigningPeerCounterValid;
  }

  const bool secureConnections =
      (updated.securityFlags & kBleBondDbSecuritySecureConnections) != 0U;
  if (secureConnections) {
    bleBondDbCopyKeyFromRecord(record, &updated.localOrSharedKey);
    memset(&updated.peerLegacyKey, 0, sizeof(updated.peerLegacyKey));
    updated.keyValidityFlags &= static_cast<uint8_t>(
        ~(kBleBondDbKeyPeerLegacyValid |
          kBleBondDbKeyLegacyProjectionUnknown));
    updated.keyValidityFlags |=
        kBleBondDbKeyLocalOrSharedValid |
        kBleBondDbKeySharedSecureConnections;
  } else if (!preserveExistingKeys ||
             (updated.keyValidityFlags &
              (kBleBondDbKeyLocalOrSharedValid |
               kBleBondDbKeyPeerLegacyValid)) == 0U) {
    // Until local legacy key distribution is implemented, the old public
    // record contains one role-projected tuple. Preserve compatibility by
    // tagging that ambiguity and keeping it in both ownership slots.
    bleBondDbCopyKeyFromRecord(record, &updated.localOrSharedKey);
    bleBondDbCopyKeyFromRecord(record, &updated.peerLegacyKey);
    updated.keyValidityFlags &=
        static_cast<uint8_t>(~kBleBondDbKeySharedSecureConnections);
    updated.keyValidityFlags |=
        kBleBondDbKeyLocalOrSharedValid |
        kBleBondDbKeyPeerLegacyValid |
        kBleBondDbKeyLegacyProjectionUnknown;
  } else if (localRole == xiao_nrf54l15::BleConnectionRole::kCentral) {
    bleBondDbCopyKeyFromRecord(record, &updated.peerLegacyKey);
    updated.keyValidityFlags |= kBleBondDbKeyPeerLegacyValid;
  } else if (localRole == xiao_nrf54l15::BleConnectionRole::kPeripheral) {
    bleBondDbCopyKeyFromRecord(record, &updated.localOrSharedKey);
    updated.keyValidityFlags |= kBleBondDbKeyLocalOrSharedValid;
  }
  updated.pairedLocalRole = static_cast<uint8_t>(localRole);
  if (localRole == xiao_nrf54l15::BleConnectionRole::kNone &&
      preserveExistingKeys) {
    updated.pairedLocalRole = core->pairedLocalRole;
  }
  memset(updated.reserved, 0, sizeof(updated.reserved));
  memcpy(core, &updated, sizeof(*core));
}

bool bleBondDbCoreToRecord(const BleBondDbCore& core,
                           xiao_nrf54l15::BleConnectionRole localRole,
                           xiao_nrf54l15::BleBondRecord* outRecord) {
  if (outRecord == nullptr || !bleBondDbCoreLooksSane(core)) {
    return false;
  }
  xiao_nrf54l15::BleConnectionRole projectionRole = localRole;
  if (projectionRole == xiao_nrf54l15::BleConnectionRole::kNone &&
      core.pairedLocalRole <=
          static_cast<uint8_t>(xiao_nrf54l15::BleConnectionRole::kCentral)) {
    projectionRole =
        static_cast<xiao_nrf54l15::BleConnectionRole>(core.pairedLocalRole);
  }
  const BleBondDbLongTermKey* selectedKey = nullptr;
  const bool secureConnections =
      (core.securityFlags & kBleBondDbSecuritySecureConnections) != 0U;
  if (secureConnections &&
      (core.keyValidityFlags & kBleBondDbKeyLocalOrSharedValid) != 0U) {
    selectedKey = &core.localOrSharedKey;
  } else if (projectionRole ==
                 xiao_nrf54l15::BleConnectionRole::kCentral &&
             (core.keyValidityFlags & kBleBondDbKeyPeerLegacyValid) != 0U) {
    selectedKey = &core.peerLegacyKey;
  } else if (projectionRole ==
                 xiao_nrf54l15::BleConnectionRole::kPeripheral &&
             (core.keyValidityFlags & kBleBondDbKeyLocalOrSharedValid) != 0U) {
    selectedKey = &core.localOrSharedKey;
  } else if ((core.keyValidityFlags &
              kBleBondDbKeyLegacyProjectionUnknown) != 0U) {
    selectedKey =
        (core.keyValidityFlags & kBleBondDbKeyLocalOrSharedValid) != 0U
            ? &core.localOrSharedKey
            : &core.peerLegacyKey;
  }
  if (selectedKey == nullptr) {
    return false;
  }

  xiao_nrf54l15::BleBondRecord record{};
  memcpy(record.peerAddress, core.peerAddress, sizeof(record.peerAddress));
  record.peerAddressRandom = core.peerAddressRandom;
  memcpy(record.peerIdentityAddress, core.peerIdentityAddress,
         sizeof(record.peerIdentityAddress));
  record.peerIdentityAddressRandom = core.peerIdentityAddressRandom;
  memcpy(record.localAddress, core.localAddress, sizeof(record.localAddress));
  record.localAddressRandom = core.localAddressRandom;
  memcpy(record.ltk, selectedKey->ltk, sizeof(record.ltk));
  memcpy(record.rand, selectedKey->rand, sizeof(record.rand));
  record.ediv = selectedKey->ediv;
  record.keySize = core.keySize;
  memcpy(record.peerIrk, core.peerIrk, sizeof(record.peerIrk));
  record.peerIrkValid =
      (core.keyValidityFlags & kBleBondDbKeyPeerIrkValid) != 0U ? 1U : 0U;
  if ((core.securityFlags & kBleBondDbSecurityAuthenticated) != 0U) {
    record.reserved[0] |= kBleBondRecordFlagAuthenticated;
  }
  if (secureConnections) {
    record.reserved[0] |= kBleBondRecordFlagSecureConnections;
  }
  memcpy(record.peerCsrk, core.peerCsrk, sizeof(record.peerCsrk));
  memcpy(record.localCsrk, core.localCsrk, sizeof(record.localCsrk));
  record.peerSignCounter = core.peerSignCounter;
  record.localSignCounter = core.localSignCounter;
  record.peerCsrkValid =
      (core.signingFlags & kBleBondDbSigningPeerCsrkValid) != 0U ? 1U : 0U;
  record.localCsrkValid =
      (core.signingFlags & kBleBondDbSigningLocalCsrkValid) != 0U ? 1U : 0U;
  record.peerSignCounterValid =
      (core.signingFlags & kBleBondDbSigningPeerCounterValid) != 0U ? 1U : 0U;
  memcpy(outRecord, &record, sizeof(*outRecord));
  return true;
}

void bleBondDbPeerStateFromCccdRecord(const BleCccdBondRecord& record,
                                      BleBondDbPeerState* outState) {
  if (outState == nullptr) {
    return;
  }
  BleBondDbPeerState state{};
  state.schemaFingerprint = record.schemaFingerprint;
  state.serviceChangedGeneration = 1U;
  state.serviceChangedStartHandle = record.serviceChangedStartHandle;
  state.serviceChangedEndHandle = record.serviceChangedEndHandle;
  state.fixedCccdFlags = record.fixedCccdFlags;
  for (uint8_t i = 0U; i < record.entryCount &&
                       state.cccdCount <
                           xiao_nrf54l15::BleRadio::kCustomGattMaxCharacteristics;
       ++i) {
    const BleCccdBondEntry& entry = record.entries[i];
    bool duplicate = false;
    for (uint8_t j = 0U; j < state.cccdCount; ++j) {
      if (state.cccdHandles[j] == entry.handle) {
        duplicate = true;
        break;
      }
    }
    if (duplicate || entry.handle == 0U || entry.value == 0U ||
        (entry.value & ~0x0003U) != 0U) {
      continue;
    }
    const uint8_t index = state.cccdCount++;
    state.cccdHandles[index] = entry.handle;
    bleBondDbSetCccdValue(&state, index,
                          static_cast<uint8_t>(entry.value));
  }
  memcpy(outState, &state, sizeof(*outState));
}

void bleBondDbPeerStateToCccdRecord(
    const BleBondDbCore& core, const BleBondDbPeerState& state,
    BleCccdBondRecord* outRecord) {
  if (outRecord == nullptr) {
    return;
  }
  BleCccdBondRecord record{};
  memcpy(record.peerAddress, core.peerAddress, sizeof(record.peerAddress));
  record.peerAddressRandom = core.peerAddressRandom;
  memcpy(record.peerIdentityAddress, core.peerIdentityAddress,
         sizeof(record.peerIdentityAddress));
  record.peerIdentityAddressRandom = core.peerIdentityAddressRandom;
  memcpy(record.localAddress, core.localAddress, sizeof(record.localAddress));
  record.localAddressRandom = core.localAddressRandom;
  record.fixedCccdFlags = state.fixedCccdFlags;
  record.schemaFingerprint = state.schemaFingerprint;
  record.serviceChangedStartHandle = state.serviceChangedStartHandle;
  record.serviceChangedEndHandle = state.serviceChangedEndHandle;
  for (uint8_t i = 0U; i < state.cccdCount; ++i) {
    (void)appendCccdBondEntry(&record, state.cccdHandles[i],
                              bleBondDbCccdValue(state, i));
  }
  memcpy(outRecord, &record, sizeof(*outRecord));
}

void bleBondDbPayloadToInfo(uint8_t bondId, const BleBondDbPayload& payload,
                            xiao_nrf54l15::BleBondInfo* outInfo) {
  if (outInfo == nullptr) {
    return;
  }
  xiao_nrf54l15::BleBondInfo info{};
  info.id = bondId;
  memcpy(info.peerAddress, payload.core.peerAddress,
         sizeof(info.peerAddress));
  info.peerAddressRandom = payload.core.peerAddressRandom;
  memcpy(info.peerIdentityAddress, payload.core.peerIdentityAddress,
         sizeof(info.peerIdentityAddress));
  info.peerIdentityAddressRandom = payload.core.peerIdentityAddressRandom;
  if ((payload.core.securityFlags & kBleBondDbSecurityAuthenticated) != 0U) {
    info.flags |= xiao_nrf54l15::kBleBondInfoAuthenticated;
  }
  if ((payload.core.securityFlags &
       kBleBondDbSecuritySecureConnections) != 0U) {
    info.flags |= xiao_nrf54l15::kBleBondInfoSecureConnections;
  }
  if (bleBondDbAddressNonZero(payload.core.peerIdentityAddress)) {
    info.flags |= xiao_nrf54l15::kBleBondInfoPeerIdentityValid;
  }
  if ((payload.core.keyValidityFlags & kBleBondDbKeyPeerIrkValid) != 0U) {
    info.flags |= xiao_nrf54l15::kBleBondInfoPeerIrkValid;
  }
  if (payload.peerState.serviceChangedStartHandle != 0U) {
    info.flags |= xiao_nrf54l15::kBleBondInfoServiceChangedPending;
  }
  info.lastUsedGeneration = payload.peerState.lastUsedGeneration;
  info.schemaFingerprint = payload.peerState.schemaFingerprint;
  info.serviceChangedStartHandle =
      payload.peerState.serviceChangedStartHandle;
  info.serviceChangedEndHandle = payload.peerState.serviceChangedEndHandle;
  memcpy(outInfo, &info, sizeof(*outInfo));
}

void copyBondRecordV2(const xiao_nrf54l15::BleBondRecord& record,
                      BleBondRecordV2* outRecord) {
  if (outRecord == nullptr) {
    return;
  }
  memset(outRecord, 0, sizeof(*outRecord));
  memcpy(outRecord->peerAddress, record.peerAddress,
         sizeof(outRecord->peerAddress));
  outRecord->peerAddressRandom = record.peerAddressRandom;
  memcpy(outRecord->peerIdentityAddress, record.peerIdentityAddress,
         sizeof(outRecord->peerIdentityAddress));
  outRecord->peerIdentityAddressRandom = record.peerIdentityAddressRandom;
  memcpy(outRecord->localAddress, record.localAddress,
         sizeof(outRecord->localAddress));
  outRecord->localAddressRandom = record.localAddressRandom;
  memcpy(outRecord->ltk, record.ltk, sizeof(outRecord->ltk));
  memcpy(outRecord->rand, record.rand, sizeof(outRecord->rand));
  memcpy(outRecord->peerIrk, record.peerIrk, sizeof(outRecord->peerIrk));
  outRecord->peerIrkValid = record.peerIrkValid;
  outRecord->ediv = record.ediv;
  outRecord->keySize = record.keySize;
  memcpy(outRecord->reserved, record.reserved, sizeof(outRecord->reserved));
}

void buildFlashBondBlob(const xiao_nrf54l15::BleBondRecord& record,
                        BleBondFlashBlob* blob) {
  if (blob == nullptr) {
    return;
  }
  memset(blob, 0, sizeof(*blob));
  blob->magic = kBleBondRetentionMagic;
  blob->version = 2U;
  blob->recordLength = static_cast<uint16_t>(sizeof(blob->record));
  copyBondRecordV2(record, &blob->record);
  blob->crc32 = crc32(reinterpret_cast<const uint8_t*>(&blob->record),
                      sizeof(blob->record));
}

void buildFlashSigningBlob(const xiao_nrf54l15::BleBondRecord& record,
                           uint32_t baseRecordCrc32,
                           BleBondSigningBlob* blob) {
  if (blob == nullptr) {
    return;
  }
  memset(blob, 0, sizeof(*blob));
  blob->magic = kBleBondSigningMagic;
  blob->baseRecordCrc32 = baseRecordCrc32;
  memcpy(blob->state.peerCsrk, record.peerCsrk,
         sizeof(blob->state.peerCsrk));
  memcpy(blob->state.localCsrk, record.localCsrk,
         sizeof(blob->state.localCsrk));
  blob->state.peerSignCounter = record.peerSignCounter;
  blob->state.localSignCounter = record.localSignCounter;
  blob->state.peerCsrkValid = record.peerCsrkValid;
  blob->state.localCsrkValid = record.localCsrkValid;
  blob->state.peerSignCounterValid = record.peerSignCounterValid;
  blob->signingStateCrc32 =
      crc32(reinterpret_cast<const uint8_t*>(&blob->state),
            sizeof(blob->state));
}

bool signingStateLooksSane(const BleBondSigningState& state) {
  if (state.peerCsrkValid > 1U || state.localCsrkValid > 1U ||
      state.peerSignCounterValid > 1U || state.reserved != 0U) {
    return false;
  }
  return state.peerSignCounterValid == 0U || state.peerCsrkValid != 0U;
}

void applySigningState(const BleBondSigningState& state,
                       xiao_nrf54l15::BleBondRecord* record) {
  if (record == nullptr) {
    return;
  }
  memcpy(record->peerCsrk, state.peerCsrk, sizeof(record->peerCsrk));
  memcpy(record->localCsrk, state.localCsrk, sizeof(record->localCsrk));
  record->peerSignCounter = state.peerSignCounter;
  record->localSignCounter = state.localSignCounter;
  record->peerCsrkValid = state.peerCsrkValid;
  record->localCsrkValid = state.localCsrkValid;
  record->peerSignCounterValid = state.peerSignCounterValid;
  record->signingReserved = 0U;
}

bool readFlashBondBlob(BleBondFlashBlob* outBlob) {
  if (outBlob == nullptr) {
    return false;
  }
  copyFromVolatileMemory(
      reinterpret_cast<const volatile uint8_t*>(&g_bleBondFlashStorage),
      reinterpret_cast<uint8_t*>(outBlob), sizeof(*outBlob));
  return true;
}

bool readFlashSigningBlob(BleBondSigningBlob* outBlob) {
  if (outBlob == nullptr) {
    return false;
  }
  copyFromVolatileMemory(
      reinterpret_cast<const volatile uint8_t*>(&g_bleBondSigningFlashStorage),
      reinterpret_cast<uint8_t*>(outBlob), sizeof(*outBlob));
  return true;
}

bool writeFlashBondBlob(const BleBondFlashBlob& blob) {
  if (!writeBondStorageToRram(&g_bleBondFlashStorage, &blob, sizeof(blob))) {
    return false;
  }
  BleBondFlashBlob verify{};
  return readFlashBondBlob(&verify) &&
         memcmp(&verify, &blob, sizeof(blob)) == 0;
}

bool writeFlashSigningBlob(const BleBondSigningBlob& blob) {
  if (!writeBondStorageToRram(&g_bleBondSigningFlashStorage, &blob,
                              sizeof(blob))) {
    return false;
  }
  BleBondSigningBlob verify{};
  return readFlashSigningBlob(&verify) &&
         memcmp(&verify, &blob, sizeof(blob)) == 0;
}

bool clearFlashBondRecord() {
  const BleBondFlashBlob base{};
  const BleBondSigningBlob signing{};
  const bool baseOk = writeFlashBondBlob(base);
  const bool signingOk = writeFlashSigningBlob(signing);
  return baseOk && signingOk;
}

bool writeFlashBondRecord(const xiao_nrf54l15::BleBondRecord& record) {
  if (!bleBondRecordLooksSane(record)) {
    return false;
  }
  BleBondFlashBlob base{};
  BleBondSigningBlob signing{};
  buildFlashBondBlob(record, &base);
  buildFlashSigningBlob(record, base.crc32, &signing);
  const bool baseOk = writeFlashBondBlob(base);
  const bool signingOk = baseOk && writeFlashSigningBlob(signing);
  return baseOk && signingOk;
}

bool writeFlashBondSigningState(
    const xiao_nrf54l15::BleBondRecord& record) {
  if (!bleBondRecordLooksSane(record)) {
    return false;
  }
  BleBondFlashBlob base{};
  if (!readFlashBondBlob(&base) || base.magic != kBleBondRetentionMagic ||
      base.version != 2U || base.recordLength != sizeof(base.record) ||
      base.crc32 != crc32(reinterpret_cast<const uint8_t*>(&base.record),
                          sizeof(base.record))) {
    return false;
  }
  BleBondRecordV2 expectedBase{};
  copyBondRecordV2(record, &expectedBase);
  if (memcmp(&base.record, &expectedBase, sizeof(expectedBase)) != 0) {
    return false;
  }
  BleBondSigningBlob signing{};
  buildFlashSigningBlob(record, base.crc32, &signing);
  return writeFlashSigningBlob(signing);
}

bool readFlashBondRecord(xiao_nrf54l15::BleBondRecord* outRecord) {
  if (outRecord == nullptr) {
    return false;
  }

  BleBondFlashBlob base{};
  if (!readFlashBondBlob(&base) || base.magic != kBleBondRetentionMagic) {
    return false;
  }

  if (base.version == 2U &&
      base.recordLength == sizeof(BleBondRecordV2)) {
    const uint32_t computedCrc =
        crc32(reinterpret_cast<const uint8_t*>(&base.record),
              sizeof(base.record));
    if (computedCrc != base.crc32) {
      return false;
    }
    convertV2BondRecord(base.record, outRecord);

    BleBondSigningBlob signing{};
    if (readFlashSigningBlob(&signing) &&
        signing.magic == kBleBondSigningMagic &&
        signing.baseRecordCrc32 == base.crc32 &&
        signing.signingStateCrc32 ==
            crc32(reinterpret_cast<const uint8_t*>(&signing.state),
                  sizeof(signing.state)) &&
        signingStateLooksSane(signing.state)) {
      applySigningState(signing.state, outRecord);
    }
    return bleBondRecordLooksSane(*outRecord);
  }

  if (base.version == 1U &&
      base.recordLength == sizeof(BleBondRecordV1)) {
    BleBondRecordV1 legacy{};
    memcpy(&legacy, &base.record, sizeof(legacy));
    const uint32_t computedCrc =
        crc32(reinterpret_cast<const uint8_t*>(&legacy), sizeof(legacy));
    if (computedCrc != base.crc32) {
      return false;
    }
    convertLegacyBondRecord(legacy, outRecord);
    return bleBondRecordLooksSane(*outRecord);
  }
  return false;
}

void clearRetainedBondBlob() {
  memset(&g_bleBondRetention, 0, sizeof(g_bleBondRetention));
}

bool writeRetainedBondRecord(const xiao_nrf54l15::BleBondRecord& record) {
  if (!bleBondRecordLooksSane(record)) {
    return false;
  }
  g_bleBondRetention.magic = kBleBondRetentionMagic;
  g_bleBondRetention.version = kBleBondRetentionVersion;
  g_bleBondRetention.recordLength = static_cast<uint16_t>(sizeof(record));
  memcpy(&g_bleBondRetention.record, &record, sizeof(record));
  g_bleBondRetention.crc32 =
      crc32(reinterpret_cast<const uint8_t*>(&g_bleBondRetention.record),
            sizeof(g_bleBondRetention.record));
  return true;
}

bool readRetainedBondRecord(xiao_nrf54l15::BleBondRecord* outRecord) {
  if (outRecord == nullptr) {
    return false;
  }
  if (g_bleBondRetention.magic != kBleBondRetentionMagic) {
    return false;
  }
  if (g_bleBondRetention.version == kBleBondRetentionVersion &&
      g_bleBondRetention.recordLength == sizeof(g_bleBondRetention.record)) {
    const uint32_t computedCrc =
        crc32(reinterpret_cast<const uint8_t*>(&g_bleBondRetention.record),
              sizeof(g_bleBondRetention.record));
    if (computedCrc != g_bleBondRetention.crc32) {
      return false;
    }
    if (!bleBondRecordLooksSane(g_bleBondRetention.record)) {
      return false;
    }
    memcpy(outRecord, &g_bleBondRetention.record, sizeof(*outRecord));
    return true;
  }
  if (g_bleBondRetention.version == 2U &&
      g_bleBondRetention.recordLength == sizeof(BleBondRecordV2)) {
    BleBondRecordV2 legacy{};
    memcpy(&legacy, &g_bleBondRetention.record, sizeof(legacy));
    const uint32_t computedCrc =
        crc32(reinterpret_cast<const uint8_t*>(&legacy), sizeof(legacy));
    if (computedCrc != g_bleBondRetention.crc32) {
      return false;
    }
    convertV2BondRecord(legacy, outRecord);
    return bleBondRecordLooksSane(*outRecord);
  }
  if (g_bleBondRetention.version == 1U &&
      g_bleBondRetention.recordLength == sizeof(BleBondRecordV1)) {
    BleBondRecordV1 legacy{};
    memcpy(&legacy, &g_bleBondRetention.record, sizeof(legacy));
    const uint32_t computedCrc =
        crc32(reinterpret_cast<const uint8_t*>(&legacy), sizeof(legacy));
    if (computedCrc != g_bleBondRetention.crc32) {
      return false;
    }
    convertLegacyBondRecord(legacy, outRecord);
    return bleBondRecordLooksSane(*outRecord);
  }
  return false;
}

void clearCccdBondBlobImage(BleCccdRetentionBlob* blob) {
  if (blob == nullptr) {
    return;
  }
  memset(blob, 0, sizeof(*blob));
}

bool cccdBondRecordLooksSane(const BleCccdBondRecord& record) {
  if (record.peerAddressRandom > 1U ||
      record.peerIdentityAddressRandom > 1U ||
      record.localAddressRandom > 1U ||
      (record.fixedCccdFlags & ~kBleCccdFixedMask) != 0U ||
      record.reserved != 0U ||
      record.entryCount > (sizeof(record.entries) / sizeof(record.entries[0]))) {
    return false;
  }
  const bool hasPendingRange = record.serviceChangedStartHandle != 0U ||
                               record.serviceChangedEndHandle != 0U;
  if (hasPendingRange &&
      (record.serviceChangedStartHandle == 0U ||
       record.serviceChangedStartHandle > record.serviceChangedEndHandle)) {
    return false;
  }
  for (uint8_t i = 0U; i < record.entryCount; ++i) {
    if (record.entries[i].handle == 0U ||
        record.entries[i].handle == kHandleGattServiceChangedCccd ||
        record.entries[i].handle == kHandleBatteryLevelCccd ||
        (record.entries[i].value & ~0x0003U) != 0U) {
      return false;
    }
  }
  return true;
}

bool cccdBondRecordV1LooksSane(const BleCccdBondRecordV1& record) {
  if (record.peerAddressRandom > 1U ||
      record.peerIdentityAddressRandom > 1U ||
      record.localAddressRandom > 1U ||
      record.entryCount > (sizeof(record.entries) / sizeof(record.entries[0]))) {
    return false;
  }
  for (uint8_t i = 0U; i < record.entryCount; ++i) {
    if (record.entries[i].handle == 0U ||
        (record.entries[i].value & ~0x0003U) != 0U) {
      return false;
    }
  }
  return true;
}

bool cccdBondRecordMatchesBond(const BleCccdBondRecord& cccdRecord,
                               const xiao_nrf54l15::BleBondRecord& bondRecord) {
  return cccdRecord.peerAddressRandom == bondRecord.peerAddressRandom &&
         cccdRecord.peerIdentityAddressRandom ==
             bondRecord.peerIdentityAddressRandom &&
         cccdRecord.localAddressRandom == bondRecord.localAddressRandom &&
         memcmp(cccdRecord.peerAddress, bondRecord.peerAddress,
                sizeof(cccdRecord.peerAddress)) == 0 &&
         memcmp(cccdRecord.peerIdentityAddress, bondRecord.peerIdentityAddress,
                sizeof(cccdRecord.peerIdentityAddress)) == 0 &&
         memcmp(cccdRecord.localAddress, bondRecord.localAddress,
                sizeof(cccdRecord.localAddress)) == 0;
}

void seedCccdRecordIdentityFromBond(const xiao_nrf54l15::BleBondRecord& bondRecord,
                                    BleCccdBondRecord* outRecord) {
  if (outRecord == nullptr) {
    return;
  }
  memset(outRecord, 0, sizeof(*outRecord));
  memcpy(outRecord->peerAddress, bondRecord.peerAddress,
         sizeof(outRecord->peerAddress));
  outRecord->peerAddressRandom = bondRecord.peerAddressRandom;
  memcpy(outRecord->peerIdentityAddress, bondRecord.peerIdentityAddress,
         sizeof(outRecord->peerIdentityAddress));
  outRecord->peerIdentityAddressRandom = bondRecord.peerIdentityAddressRandom;
  memcpy(outRecord->localAddress, bondRecord.localAddress,
         sizeof(outRecord->localAddress));
  outRecord->localAddressRandom = bondRecord.localAddressRandom;
}

bool appendCccdBondEntry(BleCccdBondRecord* record, uint16_t handle,
                         uint16_t value) {
  if (record == nullptr || handle == 0U || value == 0U ||
      (value & ~0x0003U) != 0U) {
    return false;
  }
  const uint8_t capacity =
      static_cast<uint8_t>(sizeof(record->entries) / sizeof(record->entries[0]));
  if (record->entryCount >= capacity) {
    return false;
  }
  record->entries[record->entryCount].handle = handle;
  record->entries[record->entryCount].value = value;
  ++record->entryCount;
  return true;
}

bool migrateCccdBondRecordV1(const BleCccdBondRecordV1& legacy,
                             BleCccdBondRecord* outRecord) {
  if (outRecord == nullptr || !cccdBondRecordV1LooksSane(legacy)) {
    return false;
  }
  memset(outRecord, 0, sizeof(*outRecord));
  memcpy(outRecord->peerAddress, legacy.peerAddress,
         sizeof(outRecord->peerAddress));
  outRecord->peerAddressRandom = legacy.peerAddressRandom;
  memcpy(outRecord->peerIdentityAddress, legacy.peerIdentityAddress,
         sizeof(outRecord->peerIdentityAddress));
  outRecord->peerIdentityAddressRandom = legacy.peerIdentityAddressRandom;
  memcpy(outRecord->localAddress, legacy.localAddress,
         sizeof(outRecord->localAddress));
  outRecord->localAddressRandom = legacy.localAddressRandom;

  for (uint8_t i = 0U; i < legacy.entryCount; ++i) {
    const BleCccdBondEntry& entry = legacy.entries[i];
    if (entry.handle == kHandleGattServiceChangedCccd &&
        (entry.value & 0x0002U) != 0U) {
      outRecord->fixedCccdFlags |= kBleCccdFixedServiceChanged;
      continue;
    }
    if (entry.handle == kHandleBatteryLevelCccd &&
        (entry.value & 0x0001U) != 0U) {
      outRecord->fixedCccdFlags |= kBleCccdFixedBattery;
      continue;
    }
    if (!appendCccdBondEntry(outRecord, entry.handle, entry.value)) {
      return false;
    }
  }
  return true;
}

void buildCccdFlashBlob(const BleCccdBondRecord& record,
                        BleCccdRetentionBlob* blob) {
  if (blob == nullptr) {
    return;
  }
  memset(blob, 0, sizeof(*blob));
  blob->magic = kBleCccdRetentionMagic;
  blob->version = kBleCccdRetentionVersion;
  blob->recordLength = static_cast<uint16_t>(sizeof(record));
  memcpy(&blob->record, &record, sizeof(record));
  blob->crc32 = crc32(reinterpret_cast<const uint8_t*>(&blob->record),
                      sizeof(blob->record));
}

bool readFlashCccdBondBlob(BleCccdRetentionBlob* outBlob) {
  if (outBlob == nullptr) {
    return false;
  }
  copyFromVolatileMemory(reinterpret_cast<const volatile uint8_t*>(
                             &g_bleCccdFlashStorage),
                         reinterpret_cast<uint8_t*>(outBlob), sizeof(*outBlob));
  return true;
}

bool writeFlashCccdBondBlob(const BleCccdRetentionBlob& blob) {
  if (!writeBondStorageToRram(&g_bleCccdFlashStorage, &blob, sizeof(blob))) {
    return false;
  }
  BleCccdRetentionBlob verify{};
  if (!readFlashCccdBondBlob(&verify)) {
    return false;
  }
  return memcmp(&verify, &blob, sizeof(blob)) == 0;
}

bool writeFlashCccdBondRecord(const BleCccdBondRecord& record) {
  if (!cccdBondRecordLooksSane(record)) {
    return false;
  }
  BleCccdRetentionBlob blob{};
  buildCccdFlashBlob(record, &blob);
  return writeFlashCccdBondBlob(blob);
}

bool decodeCccdBondBlob(const BleCccdRetentionBlob& blob,
                        BleCccdBondRecord* outRecord, bool* outMigrated) {
  if (outRecord == nullptr || blob.magic != kBleCccdRetentionMagic ||
      blob.recordLength != sizeof(blob.record)) {
    return false;
  }
  if (outMigrated != nullptr) {
    *outMigrated = false;
  }
  const uint32_t computedCrc =
      crc32(reinterpret_cast<const uint8_t*>(&blob.record), sizeof(blob.record));
  if (computedCrc != blob.crc32) {
    return false;
  }
  if (blob.version == kBleCccdRetentionVersion) {
    if (!cccdBondRecordLooksSane(blob.record)) {
      return false;
    }
    memcpy(outRecord, &blob.record, sizeof(*outRecord));
    return true;
  }
  if (blob.version == 1U) {
    BleCccdBondRecordV1 legacy{};
    memcpy(&legacy, &blob.record, sizeof(legacy));
    if (!migrateCccdBondRecordV1(legacy, outRecord)) {
      return false;
    }
    if (outMigrated != nullptr) {
      *outMigrated = true;
    }
    return true;
  }
  return false;
}

bool readFlashCccdBondRecord(BleCccdBondRecord* outRecord,
                             bool* outMigrated = nullptr) {
  BleCccdRetentionBlob blob{};
  return readFlashCccdBondBlob(&blob) &&
         decodeCccdBondBlob(blob, outRecord, outMigrated);
}

void clearRetainedCccdBondBlob() {
  memset(&g_bleCccdRetention, 0, sizeof(g_bleCccdRetention));
}

bool writeRetainedCccdBondRecord(const BleCccdBondRecord& record) {
  if (!cccdBondRecordLooksSane(record)) {
    return false;
  }
  g_bleCccdRetention.magic = kBleCccdRetentionMagic;
  g_bleCccdRetention.version = kBleCccdRetentionVersion;
  g_bleCccdRetention.recordLength = static_cast<uint16_t>(sizeof(record));
  memcpy(&g_bleCccdRetention.record, &record, sizeof(record));
  g_bleCccdRetention.crc32 =
      crc32(reinterpret_cast<const uint8_t*>(&g_bleCccdRetention.record),
            sizeof(g_bleCccdRetention.record));
  return true;
}

bool readRetainedCccdBondRecord(BleCccdBondRecord* outRecord,
                                bool* outMigrated = nullptr) {
  return decodeCccdBondBlob(g_bleCccdRetention, outRecord, outMigrated);
}

bool clearFlashCccdBondRecord() {
  BleCccdRetentionBlob blob{};
  clearCccdBondBlobImage(&blob);
  return writeFlashCccdBondBlob(blob);
}

bool bleBondDbHasValidHeader() {
  for (uint8_t replicaIndex = 0U; replicaIndex < kBleBondDbReplicaCount;
       ++replicaIndex) {
    for (uint8_t bondId = 0U;
         bondId < xiao_nrf54l15::BleRadio::kBleMaxBonds; ++bondId) {
      BleBondDbReplica replica{};
      readBleBondDbReplica(replicaIndex, bondId, &replica);
      // A committed new-format header is authoritative even when its payload
      // is corrupt. Falling back to a stale legacy key would be unsafe.
      if (bleBondDbHeaderValid(replica.header)) {
        return true;
      }
    }
  }
  return false;
}

bool migrateLegacyBondStorageToDatabase() {
  if (bleBondDbHasValidHeader()) {
    return true;
  }

  xiao_nrf54l15::BleBondRecord legacyBond{};
  if (!readFlashBondRecord(&legacyBond) ||
      !bleBondRecordLooksSane(legacyBond)) {
    return true;
  }

  BleBondDbPayload payload{};
  bleBondDbUpdateCoreFromRecord(
      legacyBond, xiao_nrf54l15::BleConnectionRole::kNone, &payload.core,
      false);
  if ((payload.core.securityFlags &
       kBleBondDbSecuritySecureConnections) == 0U) {
    payload.core.keyValidityFlags |= kBleBondDbKeyLegacyProjectionUnknown;
  }
  payload.core.pairedLocalRole =
      static_cast<uint8_t>(xiao_nrf54l15::BleConnectionRole::kNone);
  payload.peerState.lastUsedGeneration = 1U;

  BleCccdBondRecord legacyCccd{};
  bool cccdMigrated = false;
  if (readFlashCccdBondRecord(&legacyCccd, &cccdMigrated) &&
      cccdBondRecordMatchesBond(legacyCccd, legacyBond)) {
    bleBondDbPeerStateFromCccdRecord(legacyCccd, &payload.peerState);
    payload.peerState.lastUsedGeneration = 1U;
  }
  if (!writeBleBondDbReplica(0U, kBleBondDbActiveMagic, &payload)) {
    return false;
  }

  // The new valid header is authoritative before any legacy byte is changed.
  // Preferences and EEPROM occupy different frozen offsets and are untouched.
  (void)clearFlashBondRecord();
  (void)clearFlashCccdBondRecord();
  clearRetainedBondBlob();
  clearRetainedCccdBondBlob();
  return true;
}

uint8_t activeBleBondDbCount() {
  uint8_t count = 0U;
  for (uint8_t bondId = 0U;
       bondId < xiao_nrf54l15::BleRadio::kBleMaxBonds; ++bondId) {
    if (readActiveBleBondDbPayload(bondId, nullptr)) {
      ++count;
    }
  }
  return count;
}

uint8_t mostRecentBleBondDbId() {
  uint8_t selected = xiao_nrf54l15::BleRadio::kBleInvalidBondId;
  uint32_t selectedUse = 0U;
  uint32_t selectedGeneration = 0U;
  for (uint8_t bondId = 0U;
       bondId < xiao_nrf54l15::BleRadio::kBleMaxBonds; ++bondId) {
    BleBondDbPayload payload{};
    uint32_t generation = 0U;
    if (!readActiveBleBondDbPayload(bondId, &payload, &generation)) {
      continue;
    }
    const uint32_t use = payload.peerState.lastUsedGeneration;
    if (selected == xiao_nrf54l15::BleRadio::kBleInvalidBondId ||
        (use != 0U &&
         (selectedUse == 0U || bleBondDbGenerationNewer(use, selectedUse))) ||
        (use == selectedUse && bleBondDbGenerationNewer(
                                   generation, selectedGeneration))) {
      selected = bondId;
      selectedUse = use;
      selectedGeneration = generation;
    }
  }
  return selected;
}

uint8_t findBleBondDbIdForRecord(
    const xiao_nrf54l15::BleBondRecord& record) {
  for (uint8_t bondId = 0U;
       bondId < xiao_nrf54l15::BleRadio::kBleMaxBonds; ++bondId) {
    BleBondDbPayload payload{};
    if (readActiveBleBondDbPayload(bondId, &payload) &&
        bleBondDbIdentityMatches(payload.core, record)) {
      return bondId;
    }
  }
  return xiao_nrf54l15::BleRadio::kBleInvalidBondId;
}

}  // namespace

namespace xiao_nrf54l15 {

bool BleRadio::clearPersistentCccdRecord() {
  clearRetainedCccdBondBlob();
  cccdFlashPersistPending_ = false;
  resetBondedServiceChangedState();
  if (activeBondId_ != kBleInvalidBondId && bondRecordValid_ &&
      bondLoadCallback_ == nullptr && bondSaveCallback_ == nullptr) {
    for (uint8_t i = 0U; i < customGattCharacteristicCount_; ++i) {
      customGattCharacteristics_[i].cccdValue = 0U;
    }
    connectionServiceChangedIndicationsEnabled_ = false;
    connectionBatteryNotificationsEnabled_ = false;
    return persistActiveBondDatabaseRecord(true);
  }
  return clearFlashCccdBondRecord();
}

void BleRadio::resetBondedServiceChangedState(uint32_t schemaFingerprint) {
  bondedGattSchemaFingerprint_ = schemaFingerprint;
  bondedServiceChangedStartHandle_ = 0U;
  bondedServiceChangedEndHandle_ = 0U;
  ++bondedServiceChangedGeneration_;
  if (bondedServiceChangedGeneration_ == 0U) {
    bondedServiceChangedGeneration_ = 1U;
  }
  bondedGattSchemaStateLoaded_ = schemaFingerprint != 0U;
  connectionServiceChangedCccdRestored_ = false;
  connectionServiceChangedIndicationPending_ = false;
  connectionServiceChangedIndicationAwaitingConfirm_ = false;
  connectionServiceChangedIndicationStartHandle_ = 0U;
  connectionServiceChangedIndicationEndHandle_ = 0U;
  connectionServiceChangedIndicationGeneration_ = 0U;
}

bool BleRadio::persistBondedCccdState(bool forceFlash) {
  if (!bondRecordValid_) {
    return false;
  }

  BleCccdBondRecord record{};
  seedCccdRecordIdentityFromBond(bondRecord_, &record);
  if (connectionServiceChangedIndicationsEnabled_) {
    record.fixedCccdFlags |= kBleCccdFixedServiceChanged;
  }
  if (connectionBatteryNotificationsEnabled_) {
    record.fixedCccdFlags |= kBleCccdFixedBattery;
  }
  record.schemaFingerprint = bondedGattSchemaFingerprint_;
  record.serviceChangedStartHandle = bondedServiceChangedStartHandle_;
  record.serviceChangedEndHandle = bondedServiceChangedEndHandle_;
  for (uint8_t i = 0U; i < customGattCharacteristicCount_; ++i) {
    const BleCustomCharacteristicState& ch = customGattCharacteristics_[i];
    if (ch.cccdHandle != 0U && ch.cccdValue != 0U) {
      appendCccdBondEntry(&record, ch.cccdHandle, ch.cccdValue);
    }
  }

  const bool retentionOk = writeRetainedCccdBondRecord(record);
  bool flashOk = false;
  if (connected_ && !forceFlash) {
    cccdFlashPersistPending_ = true;
  } else if (bondLoadCallback_ == nullptr && bondSaveCallback_ == nullptr &&
             activeBondId_ != kBleInvalidBondId) {
    flashOk = persistActiveBondDatabaseRecord(true);
    if (flashOk) {
      cccdFlashPersistPending_ = false;
    }
  } else {
    flashOk = writeFlashCccdBondRecord(record);
    if (flashOk) {
      cccdFlashPersistPending_ = false;
    }
  }
  if (retentionOk || flashOk) {
    emitBleTrace(connected_ && !forceFlash ? "CCCD_DEFERRED" : "CCCD_SAVED");
  }
  return forceFlash ? flashOk : (retentionOk || flashOk);
}

bool BleRadio::restoreBondedCccdState() {
  if (!bondRecordValid_) {
    return false;
  }

  BleCccdBondRecord record{};
  bool migrated = false;
  bool loaded = false;
  BleBondDbPayload payload{};
  if (bondLoadCallback_ == nullptr && bondSaveCallback_ == nullptr &&
      activeBondId_ != kBleInvalidBondId &&
      readActiveBleBondDbPayload(activeBondId_, &payload,
                                 &activeBondGeneration_)) {
    bleBondDbPeerStateToCccdRecord(payload.core, payload.peerState, &record);
    loaded = true;
  } else {
    loaded = readFlashCccdBondRecord(&record, &migrated);
    if (!loaded) {
      loaded = readRetainedCccdBondRecord(&record, &migrated);
    }
  }
  if (!loaded || !cccdBondRecordMatchesBond(record, bondRecord_)) {
    resetBondedServiceChangedState();
    return false;
  }

  bondedGattSchemaFingerprint_ = record.schemaFingerprint;
  bondedServiceChangedStartHandle_ = record.serviceChangedStartHandle;
  bondedServiceChangedEndHandle_ = record.serviceChangedEndHandle;
  bondedGattSchemaStateLoaded_ = true;
  bondedServiceChangedGeneration_ =
      payload.peerState.serviceChangedGeneration != 0U
          ? payload.peerState.serviceChangedGeneration
          : 1U;

  bool appliedAny = false;
  connectionServiceChangedCccdRestored_ = false;
  if ((record.fixedCccdFlags & kBleCccdFixedServiceChanged) != 0U &&
      applyCccdState(kHandleGattServiceChangedCccd, 0x0002U)) {
    connectionServiceChangedCccdRestored_ = true;
    appliedAny = true;
  }
  if ((record.fixedCccdFlags & kBleCccdFixedBattery) != 0U &&
      applyCccdState(kHandleBatteryLevelCccd, 0x0001U)) {
    appliedAny = true;
  }
  for (uint8_t i = 0U; i < record.entryCount; ++i) {
    if (applyCccdState(record.entries[i].handle, record.entries[i].value)) {
      appliedAny = true;
    }
  }
  if (appliedAny) {
    emitBleTrace("CCCD_RESTORED");
  }
  if (migrated) {
    (void)persistBondedCccdState();
  }
  scheduleBondedServiceChangedIfReady();
  return true;
}

void BleRadio::setGattServiceChangedEnabled(bool enabled) {
  gattServiceChangedEnabled_ = enabled;
  if (!enabled) {
    connectionServiceChangedIndicationPending_ = false;
  } else {
    scheduleBondedServiceChangedIfReady();
  }
}

bool BleRadio::commitGattSchemaFingerprint(uint32_t fingerprint) {
  if (fingerprint == 0U) {
    return false;
  }
  gattSchemaFingerprint_ = fingerprint;
  if (!gattServiceChangedEnabled_ || !bondRecordValid_) {
    return true;
  }

  if (!bondedGattSchemaStateLoaded_) {
    BleCccdBondRecord record{};
    bool migrated = false;
    bool loaded = false;
    BleBondDbPayload payload{};
    if (bondLoadCallback_ == nullptr && bondSaveCallback_ == nullptr &&
        activeBondId_ != kBleInvalidBondId &&
        readActiveBleBondDbPayload(activeBondId_, &payload)) {
      bleBondDbPeerStateToCccdRecord(payload.core, payload.peerState, &record);
      loaded = true;
    } else {
      loaded = readFlashCccdBondRecord(&record, &migrated);
      if (!loaded) {
        loaded = readRetainedCccdBondRecord(&record, &migrated);
      }
    }
    if (loaded && cccdBondRecordMatchesBond(record, bondRecord_)) {
      bondedGattSchemaFingerprint_ = record.schemaFingerprint;
      bondedServiceChangedStartHandle_ = record.serviceChangedStartHandle;
      bondedServiceChangedEndHandle_ = record.serviceChangedEndHandle;
      bondedGattSchemaStateLoaded_ = true;
    }
  }

  // Before a peer is identified and its CCCDs are restored, the live CCCD
  // fields are all zero. Defer mutation so finalizing advertising cannot erase
  // a bonded peer's subscriptions.
  if (!connected_ || !bondKeyPrimedForConnection_ ||
      !bondIdentityMatchedForConnection_) {
    return true;
  }

  if (!bondedGattSchemaStateLoaded_) {
    bondedGattSchemaFingerprint_ = fingerprint;
    bondedGattSchemaStateLoaded_ = true;
    return persistBondedCccdState();
  }
  if (bondedGattSchemaFingerprint_ == 0U) {
    // Version-1 records have no fingerprint. Establish a baseline once rather
    // than forcing rediscovery when the database may be unchanged.
    bondedGattSchemaFingerprint_ = fingerprint;
    return persistBondedCccdState();
  }
  if (bondedGattSchemaFingerprint_ != fingerprint) {
    bondedGattSchemaFingerprint_ = fingerprint;
    return markGattServiceChanged(0x0001U, 0xFFFFU);
  }
  scheduleBondedServiceChangedIfReady();
  return true;
}

bool BleRadio::markGattServiceChanged(uint16_t startHandle,
                                      uint16_t endHandle) {
  if (!gattServiceChangedEnabled_ || !bondRecordValid_ ||
      !bondedGattSchemaStateLoaded_ || !connected_ ||
      !bondKeyPrimedForConnection_ || !bondIdentityMatchedForConnection_ ||
      startHandle == 0U ||
      startHandle > endHandle) {
    return false;
  }
  if (bondedServiceChangedStartHandle_ == 0U) {
    bondedServiceChangedEndHandle_ = endHandle;
    bondedServiceChangedStartHandle_ = startHandle;
  } else {
    if (startHandle < bondedServiceChangedStartHandle_) {
      bondedServiceChangedStartHandle_ = startHandle;
    }
    if (endHandle > bondedServiceChangedEndHandle_) {
      bondedServiceChangedEndHandle_ = endHandle;
    }
  }
  ++bondedServiceChangedGeneration_;
  if (bondedServiceChangedGeneration_ == 0U) {
    bondedServiceChangedGeneration_ = 1U;
  }
  const bool persisted = persistBondedCccdState();
  scheduleBondedServiceChangedIfReady();
  return persisted;
}

bool BleRadio::getGattServiceChangedPending(uint16_t* outStartHandle,
                                            uint16_t* outEndHandle) const {
  if (!bondedGattSchemaStateLoaded_ ||
      bondedServiceChangedStartHandle_ == 0U) {
    return false;
  }
  if (outStartHandle != nullptr) {
    *outStartHandle = bondedServiceChangedStartHandle_;
  }
  if (outEndHandle != nullptr) {
    *outEndHandle = bondedServiceChangedEndHandle_;
  }
  return true;
}

uint32_t BleRadio::gattSchemaFingerprint() const {
  return gattSchemaFingerprint_;
}

void BleRadio::scheduleBondedServiceChangedIfReady() {
  if (!gattServiceChangedEnabled_ || !connected_ ||
      connectionRole_ != BleConnectionRole::kPeripheral ||
      !bondKeyPrimedForConnection_ || !bondIdentityMatchedForConnection_ ||
      !connectionServiceChangedCccdRestored_ ||
      !connectionServiceChangedIndicationsEnabled_ ||
      bondedServiceChangedStartHandle_ == 0U) {
    return;
  }
  connectionServiceChangedIndicationPending_ = true;
}

bool BleRadio::confirmBondedServiceChanged(uint32_t confirmedGeneration) {
  if (confirmedGeneration == 0U ||
      confirmedGeneration != bondedServiceChangedGeneration_) {
    scheduleBondedServiceChangedIfReady();
    return false;
  }
  bondedServiceChangedStartHandle_ = 0U;
  bondedServiceChangedEndHandle_ = 0U;
  ++bondedServiceChangedGeneration_;
  if (bondedServiceChangedGeneration_ == 0U) {
    bondedServiceChangedGeneration_ = 1U;
  }
  const bool persisted = persistBondedCccdState();
  emitBleTrace("GATT_SERVICE_CHANGED_CONFIRMED");
  return persisted;
}

}  // namespace xiao_nrf54l15

namespace {

constexpr uint8_t kAesSbox[256] = {
    0x63, 0x7C, 0x77, 0x7B, 0xF2, 0x6B, 0x6F, 0xC5, 0x30, 0x01, 0x67, 0x2B,
    0xFE, 0xD7, 0xAB, 0x76, 0xCA, 0x82, 0xC9, 0x7D, 0xFA, 0x59, 0x47, 0xF0,
    0xAD, 0xD4, 0xA2, 0xAF, 0x9C, 0xA4, 0x72, 0xC0, 0xB7, 0xFD, 0x93, 0x26,
    0x36, 0x3F, 0xF7, 0xCC, 0x34, 0xA5, 0xE5, 0xF1, 0x71, 0xD8, 0x31, 0x15,
    0x04, 0xC7, 0x23, 0xC3, 0x18, 0x96, 0x05, 0x9A, 0x07, 0x12, 0x80, 0xE2,
    0xEB, 0x27, 0xB2, 0x75, 0x09, 0x83, 0x2C, 0x1A, 0x1B, 0x6E, 0x5A, 0xA0,
    0x52, 0x3B, 0xD6, 0xB3, 0x29, 0xE3, 0x2F, 0x84, 0x53, 0xD1, 0x00, 0xED,
    0x20, 0xFC, 0xB1, 0x5B, 0x6A, 0xCB, 0xBE, 0x39, 0x4A, 0x4C, 0x58, 0xCF,
    0xD0, 0xEF, 0xAA, 0xFB, 0x43, 0x4D, 0x33, 0x85, 0x45, 0xF9, 0x02, 0x7F,
    0x50, 0x3C, 0x9F, 0xA8, 0x51, 0xA3, 0x40, 0x8F, 0x92, 0x9D, 0x38, 0xF5,
    0xBC, 0xB6, 0xDA, 0x21, 0x10, 0xFF, 0xF3, 0xD2, 0xCD, 0x0C, 0x13, 0xEC,
    0x5F, 0x97, 0x44, 0x17, 0xC4, 0xA7, 0x7E, 0x3D, 0x64, 0x5D, 0x19, 0x73,
    0x60, 0x81, 0x4F, 0xDC, 0x22, 0x2A, 0x90, 0x88, 0x46, 0xEE, 0xB8, 0x14,
    0xDE, 0x5E, 0x0B, 0xDB, 0xE0, 0x32, 0x3A, 0x0A, 0x49, 0x06, 0x24, 0x5C,
    0xC2, 0xD3, 0xAC, 0x62, 0x91, 0x95, 0xE4, 0x79, 0xE7, 0xC8, 0x37, 0x6D,
    0x8D, 0xD5, 0x4E, 0xA9, 0x6C, 0x56, 0xF4, 0xEA, 0x65, 0x7A, 0xAE, 0x08,
    0xBA, 0x78, 0x25, 0x2E, 0x1C, 0xA6, 0xB4, 0xC6, 0xE8, 0xDD, 0x74, 0x1F,
    0x4B, 0xBD, 0x8B, 0x8A, 0x70, 0x3E, 0xB5, 0x66, 0x48, 0x03, 0xF6, 0x0E,
    0x61, 0x35, 0x57, 0xB9, 0x86, 0xC1, 0x1D, 0x9E, 0xE1, 0xF8, 0x98, 0x11,
    0x69, 0xD9, 0x8E, 0x94, 0x9B, 0x1E, 0x87, 0xE9, 0xCE, 0x55, 0x28, 0xDF,
    0x8C, 0xA1, 0x89, 0x0D, 0xBF, 0xE6, 0x42, 0x68, 0x41, 0x99, 0x2D, 0x0F,
    0xB0, 0x54, 0xBB, 0x16};

constexpr uint8_t kAesRcon[11] = {0x00, 0x01, 0x02, 0x04, 0x08, 0x10,
                                   0x20, 0x40, 0x80, 0x1B, 0x36};

using AesState = uint8_t[4][4];

uint8_t aesXtime(uint8_t x) {
  return static_cast<uint8_t>((x << 1U) ^ (((x >> 7U) & 0x01U) * 0x1BU));
}

void aesAddRoundKey(uint8_t round, AesState* state, const uint8_t* roundKey) {
  for (uint8_t i = 0U; i < 4U; ++i) {
    for (uint8_t j = 0U; j < 4U; ++j) {
      (*state)[i][j] ^= roundKey[(round * 16U) + (i * 4U) + j];
    }
  }
}

void aesSubBytes(AesState* state) {
  for (uint8_t i = 0U; i < 4U; ++i) {
    for (uint8_t j = 0U; j < 4U; ++j) {
      (*state)[j][i] = kAesSbox[(*state)[j][i]];
    }
  }
}

void aesShiftRows(AesState* state) {
  uint8_t temp = (*state)[0][1];
  (*state)[0][1] = (*state)[1][1];
  (*state)[1][1] = (*state)[2][1];
  (*state)[2][1] = (*state)[3][1];
  (*state)[3][1] = temp;

  temp = (*state)[0][2];
  (*state)[0][2] = (*state)[2][2];
  (*state)[2][2] = temp;
  temp = (*state)[1][2];
  (*state)[1][2] = (*state)[3][2];
  (*state)[3][2] = temp;

  temp = (*state)[0][3];
  (*state)[0][3] = (*state)[3][3];
  (*state)[3][3] = (*state)[2][3];
  (*state)[2][3] = (*state)[1][3];
  (*state)[1][3] = temp;
}

void aesMixColumns(AesState* state) {
  for (uint8_t i = 0U; i < 4U; ++i) {
    uint8_t t = (*state)[i][0];
    uint8_t tmp = static_cast<uint8_t>((*state)[i][0] ^ (*state)[i][1] ^
                                       (*state)[i][2] ^ (*state)[i][3]);
    uint8_t tm = static_cast<uint8_t>((*state)[i][0] ^ (*state)[i][1]);
    tm = aesXtime(tm);
    (*state)[i][0] ^= static_cast<uint8_t>(tm ^ tmp);
    tm = static_cast<uint8_t>((*state)[i][1] ^ (*state)[i][2]);
    tm = aesXtime(tm);
    (*state)[i][1] ^= static_cast<uint8_t>(tm ^ tmp);
    tm = static_cast<uint8_t>((*state)[i][2] ^ (*state)[i][3]);
    tm = aesXtime(tm);
    (*state)[i][2] ^= static_cast<uint8_t>(tm ^ tmp);
    tm = static_cast<uint8_t>((*state)[i][3] ^ t);
    tm = aesXtime(tm);
    (*state)[i][3] ^= static_cast<uint8_t>(tm ^ tmp);
  }
}

void aesKeyExpansion128(const uint8_t key[16], uint8_t roundKey[176]) {
  for (uint8_t i = 0U; i < 16U; ++i) {
    roundKey[i] = key[i];
  }

  uint8_t temp[4] = {0U, 0U, 0U, 0U};
  uint16_t bytesGenerated = 16U;
  uint8_t rconIndex = 1U;
  while (bytesGenerated < 176U) {
    for (uint8_t i = 0U; i < 4U; ++i) {
      temp[i] = roundKey[static_cast<uint16_t>(bytesGenerated - 4U + i)];
    }

    if ((bytesGenerated % 16U) == 0U) {
      const uint8_t rot = temp[0];
      temp[0] = temp[1];
      temp[1] = temp[2];
      temp[2] = temp[3];
      temp[3] = rot;
      temp[0] = kAesSbox[temp[0]];
      temp[1] = kAesSbox[temp[1]];
      temp[2] = kAesSbox[temp[2]];
      temp[3] = kAesSbox[temp[3]];
      temp[0] ^= kAesRcon[rconIndex];
      ++rconIndex;
    }

    for (uint8_t i = 0U; i < 4U; ++i) {
      roundKey[bytesGenerated] =
          static_cast<uint8_t>(roundKey[bytesGenerated - 16U] ^ temp[i]);
      ++bytesGenerated;
    }
  }
}

// AES-128 encryption path adapted from tiny-AES-c (Unlicense), reduced to ECB block encrypt.
void aesEncryptBlockBe(const uint8_t key[16], const uint8_t plaintext[16], uint8_t out[16]) {
  uint8_t roundKey[176] = {0};
  uint8_t stateBuffer[16] = {0};
  aesKeyExpansion128(key, roundKey);
  memcpy(stateBuffer, plaintext, 16U);

  auto* state = reinterpret_cast<AesState*>(&stateBuffer[0]);
  aesAddRoundKey(0U, state, roundKey);
  for (uint8_t round = 1U; round < 10U; ++round) {
    aesSubBytes(state);
    aesShiftRows(state);
    aesMixColumns(state);
    aesAddRoundKey(round, state, roundKey);
  }
  aesSubBytes(state);
  aesShiftRows(state);
  aesAddRoundKey(10U, state, roundKey);
  memcpy(out, stateBuffer, 16U);
}

void reverseBytes(const uint8_t* in, uint8_t* out, uint8_t len) {
  if (in == nullptr || out == nullptr) {
    return;
  }
  for (uint8_t i = 0U; i < len; ++i) {
    out[i] = in[static_cast<uint8_t>(len - 1U - i)];
  }
}

void xor16(const uint8_t* a, const uint8_t* b, uint8_t* out) {
  if (a == nullptr || b == nullptr || out == nullptr) {
    return;
  }
  for (uint8_t i = 0U; i < 16U; ++i) {
    out[i] = static_cast<uint8_t>(a[i] ^ b[i]);
  }
}

bool aesEncryptLeSoftware(const uint8_t keyLe[16], const uint8_t plaintextLe[16],
                         uint8_t outLe[16]) {
  if (keyLe == nullptr || plaintextLe == nullptr || outLe == nullptr) {
    return false;
  }

  uint8_t keyBe[16] = {0};
  uint8_t plainBe[16] = {0};
  uint8_t encBe[16] = {0};
  reverseBytes(keyLe, keyBe, 16U);
  reverseBytes(plaintextLe, plainBe, 16U);
  aesEncryptBlockBe(keyBe, plainBe, encBe);
  reverseBytes(encBe, outLe, 16U);
  return true;
}

bool aesEncryptCcmBlockSoftware(const uint8_t keyLe[16], const uint8_t plaintext[16],
                                uint8_t out[16]) {
  if (keyLe == nullptr || plaintext == nullptr || out == nullptr) {
    return false;
  }

  // Link-layer CCM operates on byte strings as-is (AES big-endian block
  // convention). Session keys are held in Bluetooth little-endian order, so
  // only the key bytes are swapped before the block encrypt.
  uint8_t keyBe[16] = {0};
  reverseBytes(keyLe, keyBe, 16U);
  aesEncryptBlockBe(keyBe, plaintext, out);
  return true;
}

bool aesEncryptLe(const uint8_t keyLe[16], const uint8_t plaintextLe[16],
                  uint8_t outLe[16]);
bool bleCcmEncryptPayload(const uint8_t key[16], const uint8_t iv[8],
                          uint64_t counter, uint8_t direction, uint8_t header,
                          const uint8_t* plaintext, uint8_t plaintextLen,
                          uint8_t* outCipherWithMic, uint8_t* outCipherWithMicLen);
bool bleCcmDecryptPayload(const uint8_t key[16], const uint8_t iv[8],
                          uint64_t counter, uint8_t direction, uint8_t header,
                          const uint8_t* cipherWithMic, uint8_t cipherWithMicLen,
                          uint8_t* outPlaintext, uint8_t* outPlaintextLen);
bool bleCcmEncryptPayloadSoftware(const uint8_t key[16], const uint8_t iv[8],
                                  uint64_t counter, uint8_t direction,
                                  uint8_t header, const uint8_t* plaintext,
                                  uint8_t plaintextLen, uint8_t* outCipherWithMic,
                                  uint8_t* outCipherWithMicLen);
bool bleCcmDecryptPayloadSoftware(const uint8_t key[16], const uint8_t iv[8],
                                  uint64_t counter, uint8_t direction,
                                  uint8_t header, const uint8_t* cipherWithMic,
                                  uint8_t cipherWithMicLen, uint8_t* outPlaintext,
                                  uint8_t* outPlaintextLen);

#if !defined(NRF54L15_CLEAN_BLE_HARDWARE_CRYPTO)
// Default the live BLE path to the silicon ECB/CCM blocks. The runtime still
// self-tests the hardware backend once and falls back to the software path if
// the peripheral bring-up or packet formatting does not match the software
// reference implementation on this board/package.
#define NRF54L15_CLEAN_BLE_HARDWARE_CRYPTO 1
#endif

constexpr uint32_t kBleHardwareCryptoEcbSpinLimit = 200000UL;
constexpr uint32_t kBleHardwareCryptoCcmSpinLimit = 200000UL;
constexpr uint32_t kBleSecurityRandomSpinLimit = 400000UL;
