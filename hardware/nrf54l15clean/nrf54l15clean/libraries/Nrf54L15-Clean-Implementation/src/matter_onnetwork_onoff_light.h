#pragma once

#include <Preferences.h>
#include <stddef.h>
#include <stdint.h>

#include "matter_foundation_target.h"
#include "matter_onoff_light.h"
#include "matter_onoff_light_endpoint.h"
#include "matter_credentials.h"
#include "matter_device_attestation.h"
#include "matter_access_control.h"

#if defined(OPENTHREAD_CONFIG_SRP_CLIENT_ENABLE) && \
    (OPENTHREAD_CONFIG_SRP_CLIENT_ENABLE != 0)
#include <openthread/srp_client.h>
#endif

namespace xiao_nrf54l15 {

enum class MatterOnNetworkDatasetSource : uint8_t {
  kNone = 0U,
  kDemo = 1U,
  kPassphrase = 2U,
  kExplicit = 3U,
  kPersistent = 4U,
};

enum class MatterCommissioningWindowState : uint8_t {
  kClosed = 0U,
  kPendingReadiness = 1U,
  kOpen = 2U,
  kExpired = 3U,
};

enum class MatterDiscoveryCommissioningMode : uint8_t {
  kNotCommissioning = 0U,
  kBasicCommissioning = 1U,
  kEnhancedCommissioning = 2U,
};

enum class MatterOnNetworkDiscoveryRecordKind : uint8_t {
  kCommissionable = 0U,
  kOperational = 1U,
};

struct MatterOnNetworkDiscoveryTextEntry {
  char value[32] = {0};
};

struct MatterOnNetworkDiscoverySubtype {
  char value[32] = {0};
};

struct MatterOnNetworkDiscoveryRecord {
  static constexpr size_t kMaxTextEntries = 10U;
  static constexpr size_t kMaxSubtypes = 4U;

  bool valid = false;
  bool stagedOnly = true;
  bool readyToRegister = false;
  MatterOnNetworkDiscoveryRecordKind kind =
      MatterOnNetworkDiscoveryRecordKind::kCommissionable;
  const char* serviceType = nullptr;
  uint16_t port = 0U;
  char instanceName[33] = {0};
  char hostName[32] = {0};
  char blockerName[48] = {0};
  size_t textEntryCount = 0U;
  MatterOnNetworkDiscoveryTextEntry textEntries[kMaxTextEntries] = {};
  size_t subtypeCount = 0U;
  MatterOnNetworkDiscoverySubtype subtypes[kMaxSubtypes] = {};
};

struct MatterOnNetworkIdentity {
  uint32_t setupPinCode = kDefaultSetupPinCode;
  uint16_t discriminator = kDefaultDiscriminator;
  uint16_t vendorId = kDefaultVendorId;
  uint16_t productId = kDefaultProductId;
  MatterCommissioningFlow commissioningFlow =
      MatterCommissioningFlow::kStandard;
};

struct MatterOnNetworkPersistentState {
  uint32_t magic = 0U;
  uint16_t version = 0U;
  uint32_t setupPinCode = 0U;
  uint16_t discriminator = 0U;
  uint16_t vendorId = 0U;
  uint16_t productId = 0U;
  uint8_t commissioningFlow = 0U;
  uint8_t reserved = 0U;
};

struct MatterOnNetworkPersistentThreadDataset {
  uint32_t magic = 0U;
  uint16_t version = 0U;
  uint16_t length = 0U;
  uint8_t tlvs[OT_OPERATIONAL_DATASET_MAX_LENGTH] = {0};
};

struct MatterOnNetworkReadinessSummary {
  bool ready = false;
  bool storageOpen = false;
  bool lightReady = false;
  bool foundationReady = false;
  bool threadStarted = false;
  bool threadAttached = false;
  bool manualCodeReady = false;
  bool qrCodeReady = false;
  bool threadDatasetExportable = false;
  Nrf54ThreadExperimental::AttachSummary threadAttachSummary = {};
  Nrf54ThreadExperimental::DatasetRestoreDiagnostics
      threadRestoreDiagnostics = {};
  char beginFailureName[48] = {0};
  char phaseName[32] = {0};
  char blockerName[48] = {0};
};

struct MatterOnNetworkDiscoverySummary {
  bool valid = false;
  bool stagedOnly = true;
  bool readyToRegister = false;
  bool commissioningWindowOpen = false;
  bool threadAttached = false;
  MatterDiscoveryCommissioningMode commissioningMode =
      MatterDiscoveryCommissioningMode::kNotCommissioning;
  MatterFoundationDiscoveryCapabilities capabilities = {};
  const char* serviceType = nullptr;
  uint16_t port = 0U;
  uint16_t discriminator = 0U;
  uint16_t vendorId = 0U;
  uint16_t productId = 0U;
  MatterDeviceTypeId deviceTypeId = 0U;
  char instanceName[17] = {0};
  char deviceName[24] = {0};
  char txtDiscriminator[8] = {0};
  char txtVendorProduct[18] = {0};
  char txtCommissioningMode[8] = {0};
  char txtDeviceType[16] = {0};
  char txtDeviceName[28] = {0};
  char blockerName[48] = {0};
};

struct MatterOnNetworkDiscoveryPublicationState {
  bool attempted = false;
  bool active = false;
  bool stagedOnly = true;
  bool backendAvailable = false;
  bool srpClientEnabled = false;
  bool srpServiceQueued = false;
  bool srpAutoStartEnabled = false;
  bool srpRemovePending = false;
  bool srpHostRegistered = false;
  bool srpServiceRegistered = false;
  bool commissioningWindowOpen = false;
  bool threadAttached = false;
  MatterCommissioningWindowState windowState =
      MatterCommissioningWindowState::kClosed;
  uint16_t recordsTotal = 0U;
  uint16_t recordsReady = 0U;
  uint16_t recordsActive = 0U;
  uint32_t publishAttempts = 0U;
  uint32_t unpublishCount = 0U;
  int srpLastError = 0;
  char blockerName[48] = {0};
};

struct MatterOnNetworkOnOffLightConfig {
  const char* storageNamespace = "matter_node";
  const char* lightStorageNamespace = "matter_onoff";
  bool restorePersistentState = true;
  bool wipeThreadSettings = false;
  bool autoStartThread = true;
  bool autoRequestRouterRole = false;
  bool autoOpenCommissioningWindow = false;
  uint16_t commissioningWindowSeconds = 900U;
  bool deriveDefaultIdentityFromHardware = true;
  bool useDemoDataset = false;
  MatterOnNetworkIdentity identity = {};
  const otOperationalDataset* explicitThreadDataset = nullptr;
  const char* threadPassPhrase = nullptr;
  const char* threadNetworkName = nullptr;
  const uint8_t* threadExtPanId = nullptr;
};

struct MatterOnNetworkOnOffLightStatus {
  bool storageOpen = false;
  bool lightReady = false;
  bool threadStarted = false;
  bool threadAttached = false;
  bool threadDatasetConfigured = false;
  bool threadDatasetExportable = false;
  otChangedFlags threadLastChangedFlags = 0U;
  Nrf54ThreadExperimental::AttachDebugState threadAttachDebugState = {};
  Nrf54ThreadExperimental::AttachSummary threadAttachSummary = {};
  Nrf54ThreadExperimental::DatasetRestoreDiagnostics
      threadRestoreDiagnostics = {};
  MatterOnNetworkReadinessSummary readinessSummary = {};
  MatterOnNetworkDiscoverySummary discoverySummary = {};
  MatterOnNetworkDiscoveryPublicationState discoveryPublication = {};
  bool manualCodeReady = false;
  bool qrCodeReady = false;
  bool readyForOnNetworkCommissioning = false;
  bool buildSeamsAligned = false;
  bool commissioningWindowPending = false;
  char beginFailureName[48] = {0};
  MatterOnNetworkDatasetSource datasetSource =
      MatterOnNetworkDatasetSource::kNone;
  MatterCommissioningWindowState commissioningWindowState =
      MatterCommissioningWindowState::kClosed;
  uint16_t commissioningWindowSecondsRemaining = 0U;
  Nrf54ThreadExperimental::Role threadRole =
      Nrf54ThreadExperimental::Role::kUnknown;
  Nrf54ThreadExperimental::AttachDiagnostics threadAttachDiagnostics = {};
  uint16_t rloc16 = 0xFFFFU;
  MatterOnNetworkIdentity identity = {};
  MatterOnOffLightDeviceState light = {};
};

struct MatterOnNetworkCommissioningBundle {
  static constexpr size_t kOpenThreadDatasetHexCapacity =
      (OT_OPERATIONAL_DATASET_MAX_LENGTH * 2U) + 1U;

  bool ready = false;
  bool manualCodeReady = false;
  bool qrCodeReady = false;
  bool openThreadDatasetReady = false;
#if defined(NRF54L15_CLEAN_MATTER_CORE_ENABLE) && \
    (NRF54L15_CLEAN_MATTER_CORE_ENABLE != 0)
  static constexpr size_t kMatterThreadDatasetHexCapacity =
      (chip::Thread::kSizeOperationalDataset * 2U) + 1U;
  bool matterThreadDatasetReady = false;
  size_t matterThreadDatasetHexLength = 0U;
  char matterThreadDatasetHex[kMatterThreadDatasetHexCapacity] = {0};
#endif
  MatterOnNetworkDatasetSource datasetSource =
      MatterOnNetworkDatasetSource::kNone;
  MatterCommissioningWindowState commissioningWindowState =
      MatterCommissioningWindowState::kClosed;
  uint16_t commissioningWindowSecondsRemaining = 0U;
  size_t openThreadDatasetHexLength = 0U;
  char manualCode[kMatterManualPairingLongCodeLength + 1U] = {0};
  char qrCode[kMatterQrCodeTextLength + 1U] = {0};
  char openThreadDatasetHex[kOpenThreadDatasetHexCapacity] = {0};
};

class Nrf54MatterOnNetworkOnOffLightNode {
 public:
  Nrf54MatterOnNetworkOnOffLightNode() = default;

  bool begin(const MatterOnNetworkOnOffLightConfig* config = nullptr);
  void end();
  void process();

  bool snapshot(MatterOnNetworkOnOffLightStatus* outStatus) const;

  bool setIdentity(const MatterOnNetworkIdentity& identity, bool persist = true);
  const MatterOnNetworkIdentity& identity() const;
  bool restoreDefaultIdentity(bool persist = true);
  bool savePersistentIdentity();
  bool clearPersistentIdentity();

  bool useDemoThreadDataset();
  bool useThreadDatasetFromPassphrase(
      const char* passPhrase,
      const char* networkName,
      const uint8_t extPanId[OT_EXT_PAN_ID_SIZE]);
  bool useThreadDataset(const otOperationalDataset& dataset,
                        bool persist = true);
  bool useThreadDatasetTlvs(const otOperationalDatasetTlvs& datasetTlvs,
                            bool persist = true);
  bool useThreadDatasetHex(const char* datasetHex, bool persist = true);
  bool savePersistentThreadDataset();
  bool clearPersistentThreadDataset();
  bool factoryReset();

  bool openCommissioningWindow(uint16_t seconds);
  void closeCommissioningWindow();
  MatterCommissioningWindowState commissioningWindowState() const;
  bool commissioningWindowOpen() const;
  uint16_t commissioningWindowSecondsRemaining() const;
  bool readinessSummary(MatterOnNetworkReadinessSummary* outSummary) const;
  bool discoverySummary(MatterOnNetworkDiscoverySummary* outSummary) const;
  bool discoveryPublicationState(
      MatterOnNetworkDiscoveryPublicationState* outState) const;
  bool buildCommissionableDiscoveryRecord(
      MatterOnNetworkDiscoveryRecord* outRecord) const;
  bool buildOperationalDiscoveryRecord(
      MatterOnNetworkDiscoveryRecord* outRecord) const;
  bool buildDiscoveryRecords(MatterOnNetworkDiscoveryRecord* outRecords,
                             size_t recordCapacity,
                             size_t* outRecordCount = nullptr) const;
  bool buildCommissioningBundle(
      MatterOnNetworkCommissioningBundle* outBundle) const;
  bool exportOpenThreadDatasetTlvs(otOperationalDatasetTlvs* outTlvs) const;
  bool exportOpenThreadDatasetHex(char* outBuffer, size_t outBufferSize,
                                  size_t* outHexLength = nullptr) const;

  bool manualPairingCode(char* outBuffer, size_t outBufferSize) const;
  bool qrCode(char* outBuffer, size_t outBufferSize) const;
  bool readyForOnNetworkCommissioning() const;

#if defined(NRF54L15_CLEAN_MATTER_CORE_ENABLE) && \
    (NRF54L15_CLEAN_MATTER_CORE_ENABLE != 0)
  bool exportThreadDataset(chip::Thread::OperationalDataset* outDataset,
                           CHIP_ERROR* outError = nullptr) const;
  bool exportMatterThreadDatasetHex(char* outBuffer, size_t outBufferSize,
                                    size_t* outHexLength = nullptr,
                                    CHIP_ERROR* outError = nullptr) const;
#endif

  Nrf54MatterOnOffLightDevice& light();
  const Nrf54MatterOnOffLightDevice& light() const;
  Nrf54MatterOnOffLightEndpoint& endpoint();
  const Nrf54MatterOnOffLightEndpoint& endpoint() const;
  const Nrf54MatterOnOffLightFoundation& foundation() const;
  Nrf54ThreadExperimental& thread();
  const Nrf54ThreadExperimental& thread() const;

  static void buildDefaultIdentity(MatterOnNetworkIdentity* outIdentity);
  static bool identityValid(const MatterOnNetworkIdentity& identity);
  static const char* datasetSourceName(MatterOnNetworkDatasetSource source);
  static const char* commissioningWindowStateName(
      MatterCommissioningWindowState state);
  static const char* discoveryRecordKindName(
      MatterOnNetworkDiscoveryRecordKind kind);

 private:
  static constexpr uint32_t kPersistentStateMagic = 0x4D4E4554UL;
  static constexpr uint16_t kPersistentStateVersion = 1U;
  static constexpr char kPersistentStateKey[] = "setup";
  static constexpr uint32_t kPersistentThreadDatasetMagic = 0x54445354UL;
  static constexpr uint16_t kPersistentThreadDatasetVersion = 1U;
  static constexpr char kPersistentThreadDatasetKey[] = "thread_ds";

  static uint16_t remainingWindowSeconds(uint32_t endMs);
  static bool bytesToUpperHex(const uint8_t* data, size_t length,
                              char* outBuffer, size_t outBufferSize,
                              size_t* outHexLength = nullptr);
  static int hexNibble(char value);
  static bool hexToBytes(const char* text, uint8_t* outData, size_t outCapacity,
                         size_t* outLength);
  static bool addDiscoveryText(MatterOnNetworkDiscoveryRecord* record,
                               const char* text);
  static bool addDiscoverySubtype(MatterOnNetworkDiscoveryRecord* record,
                                  const char* subtype);
  static void buildDiscoveryHostName(const MatterOnNetworkIdentity& identity,
                                     char* outHostName,
                                     size_t outHostNameSize);

  bool loadPersistentIdentity(MatterOnNetworkIdentity* outIdentity) const;
  bool loadPersistentThreadDataset(otOperationalDatasetTlvs* outTlvs) const;
  void buildManualPayload(MatterManualPairingPayload* outPayload) const;
  void buildQrPayload(MatterQrCodePayload* outPayload) const;
  bool threadDatasetExportable() const;
  bool updateDiscoveryPublication();
  void resetDiscoveryPublication(const char* blockerName);
#if defined(OPENTHREAD_CONFIG_SRP_CLIENT_ENABLE) && \
    (OPENTHREAD_CONFIG_SRP_CLIENT_ENABLE != 0)
  bool publishCommissionableDiscoveryRecord(
      const MatterOnNetworkDiscoveryRecord& record);
  void clearSrpDiscoveryPublication();
  bool requestSrpDiscoveryUnpublish(bool sendUnregisterToServer);
  void resetSrpDiscoveryBuffers();
  bool prepareSrpTxtEntries(const MatterOnNetworkDiscoveryRecord& record);
  bool prepareSrpSubtypes(const MatterOnNetworkDiscoveryRecord& record);
  static void onSrpClientCallback(otError error,
                                  const otSrpClientHostInfo* hostInfo,
                                  const otSrpClientService* services,
                                  const otSrpClientService* removedServices,
                                  void* context);
  static void onSrpAutoStart(const otSockAddr* serverSockAddr,
                             void* context);
#else
  bool publishCommissionableDiscoveryRecord(
      const MatterOnNetworkDiscoveryRecord& record);
  void clearSrpDiscoveryPublication();
  bool requestSrpDiscoveryUnpublish(bool sendUnregisterToServer);
  void resetSrpDiscoveryBuffers();
#endif

  Preferences prefs_;
  bool storageOpen_ = false;
  bool lightReady_ = false;
  bool autoRequestRouterRole_ = false;
  bool routerRoleRequested_ = false;
  bool commissioningWindowPending_ = false;
  bool commissioningWindowExpired_ = false;
  uint16_t commissioningWindowDurationSeconds_ = 0U;
  uint32_t commissioningWindowEndMs_ = 0U;
  MatterOnNetworkDatasetSource datasetSource_ =
      MatterOnNetworkDatasetSource::kNone;
  char beginFailureName_[48] = "not_started";
  MatterOnNetworkIdentity identity_ = {};
  bool discoveryPublicationAttempted_ = false;
  bool discoveryPublicationActive_ = false;
  bool discoveryPublicationBackendAvailable_ = false;
  bool discoverySrpServiceQueued_ = false;
  bool discoverySrpAutoStartEnabled_ = false;
  bool discoverySrpRemovePending_ = false;
  bool discoverySrpHostRegistered_ = false;
  bool discoverySrpServiceRegistered_ = false;
  int discoverySrpLastError_ = 0;
  uint16_t discoveryPublicationRecordsTotal_ = 0U;
  uint16_t discoveryPublicationRecordsReady_ = 0U;
  uint16_t discoveryPublicationRecordsActive_ = 0U;
  uint32_t discoveryPublicationAttempts_ = 0U;
  uint32_t discoveryPublicationUnpublishCount_ = 0U;
  MatterCommissioningWindowState discoveryPublicationWindowState_ =
      MatterCommissioningWindowState::kClosed;
  char discoveryPublicationBlockerName_[48] = {0};
  Nrf54MatterOnOffLightFoundation foundation_;
  Nrf54MatterOnOffLightDevice light_;
  Nrf54MatterOnOffLightEndpoint endpoint_;
  Nrf54ThreadExperimental thread_;
  MatterDeviceAttestation attestation_;
  MatterAccessControl accessControl_;
  bool attestationReady_ = false;
  bool accessControlReady_ = false;
#if defined(OPENTHREAD_CONFIG_SRP_CLIENT_ENABLE) && \
    (OPENTHREAD_CONFIG_SRP_CLIENT_ENABLE != 0)
  otSrpClientService srpCommissionableService_ = {};
  otDnsTxtEntry srpTxtEntries_[MatterOnNetworkDiscoveryRecord::kMaxTextEntries] =
      {};
  const char* srpSubtypePointers_[MatterOnNetworkDiscoveryRecord::kMaxSubtypes +
                                  1U] = {};
  char srpHostName_[32] = {0};
  char srpInstanceName_[33] = {0};
  char srpTxtKeys_[MatterOnNetworkDiscoveryRecord::kMaxTextEntries][10] = {};
  char srpTxtValues_[MatterOnNetworkDiscoveryRecord::kMaxTextEntries][32] = {};
  char srpSubtypeLabels_[MatterOnNetworkDiscoveryRecord::kMaxSubtypes][32] = {};
#endif
};

}  // namespace xiao_nrf54l15
