#if defined(NRF54L15_CLEAN_MATTER_CORE_ENABLE) && NRF54L15_CLEAN_MATTER_CORE_ENABLE
#include "matter_platform_stage.h"

#include <Arduino.h>
#include <Preferences.h>
#include <string.h>

namespace xiao_nrf54l15 {
namespace {

constexpr char kFactoryDataKey[] = "factory_data";

void copyText(char* destination, size_t length, const char* source) {
  if (destination == nullptr || length == 0U) {
    return;
  }

  destination[0] = '\0';
  if (source == nullptr) {
    return;
  }

  strncpy(destination, source, length - 1U);
  destination[length - 1U] = '\0';
}

}  // namespace

bool MatterPlatform::begin(const MatterPlatformConfig& config) {
  if (storageOpen_) {
    lastError_ = static_cast<uint32_t>(OT_ERROR_ALREADY);
    return false;
  }

  config_ = config;
  rxCount_ = 0U;
  txCount_ = 0U;
  dropCount_ = 0U;
  lastError_ = static_cast<uint32_t>(OT_ERROR_NONE);
  udpBound_ = false;
  configuredThreadDatasetSource_ = MatterPlatformThreadDatasetSource::kNone;

  if (!openStorageFromConfig()) {
    return false;
  }

  if (config_.autoStartThread && !startThreadFromConfig()) {
    storageOpen_ = false;
    return false;
  }

  return true;
}

void MatterPlatform::end() {
  if (thread_.started()) {
    thread_.stop();
  }
  storageOpen_ = false;
  udpBound_ = false;
  configuredThreadDatasetSource_ = MatterPlatformThreadDatasetSource::kNone;
  receiveCallback_ = nullptr;
  receiveContext_ = nullptr;
}

void MatterPlatform::process() {
  thread_.process();
  if (udpBound_ && !thread_.udpOpened(config_.udpPort)) {
    (void)thread_.openUdp(config_.udpPort, handleUdpReceiveStatic, this);
  }
}

bool MatterPlatform::ready() const {
  return storageOpen_ && thread_.started() && thread_.attached();
}

bool MatterPlatform::transportReady() const {
  return ready() && udpBound_ && thread_.udpOpened(config_.udpPort);
}

bool MatterPlatform::snapshot(MatterPlatformState* outState) const {
  if (outState == nullptr) {
    return false;
  }

  memset(outState, 0, sizeof(*outState));
  outState->initialized = storageOpen_;
  outState->storageOpen = storageOpen_;
  outState->threadStarted = thread_.started();
  outState->threadAttached = thread_.attached();
  outState->udpBound = udpBound_;
  outState->transportReady = transportReady();
  outState->threadDatasetConfigured = thread_.datasetConfigured();
  outState->uptimeMs = millis();
  outState->rxCount = rxCount_;
  outState->txCount = txCount_;
  outState->dropCount = dropCount_;
  outState->lastError = lastError_;
  outState->rloc16 = thread_.rloc16();
  outState->threadDatasetSource = currentThreadDatasetSource();
  outState->threadRole = thread_.role();
  (void)thread_.getAttachSummary(&outState->threadAttachSummary);
  (void)thread_.getDatasetRestoreDiagnostics(
      &outState->threadRestoreDiagnostics);
  otOperationalDatasetTlvs tlvs = {};
  outState->threadDatasetExportable =
      thread_.getConfiguredOrActiveDatasetTlvs(&tlvs);
  copyText(outState->threadDatasetSourceName,
           sizeof(outState->threadDatasetSourceName),
           threadDatasetSourceName(outState->threadDatasetSource));
  copyReadinessBlocker(outState);
  return true;
}

bool MatterPlatform::setThreadDataset(const otOperationalDataset& dataset) {
  const bool ok = thread_.setActiveDataset(dataset);
  lastError_ = static_cast<uint32_t>(ok ? OT_ERROR_NONE : thread_.lastError());
  if (ok) {
    configuredThreadDatasetSource_ =
        MatterPlatformThreadDatasetSource::kConfiguredDataset;
  }
  return ok;
}

bool MatterPlatform::setThreadDatasetTlvs(
    const otOperationalDatasetTlvs& datasetTlvs) {
  const bool ok = thread_.setActiveDatasetTlvs(datasetTlvs);
  lastError_ = static_cast<uint32_t>(ok ? OT_ERROR_NONE : thread_.lastError());
  if (ok) {
    configuredThreadDatasetSource_ =
        MatterPlatformThreadDatasetSource::kConfiguredTlvs;
  }
  return ok;
}

bool MatterPlatform::useDemoThreadDataset() {
  otOperationalDataset dataset = {};
  Nrf54ThreadExperimental::buildDemoDataset(&dataset);
  const bool ok = thread_.setActiveDataset(dataset);
  lastError_ = static_cast<uint32_t>(ok ? OT_ERROR_NONE : thread_.lastError());
  if (ok) {
    configuredThreadDatasetSource_ =
        MatterPlatformThreadDatasetSource::kDemoDataset;
  }
  return ok;
}

bool MatterPlatform::exportOpenThreadDatasetTlvs(
    otOperationalDatasetTlvs* outTlvs) const {
  if (outTlvs == nullptr) {
    return false;
  }

  return thread_.getConfiguredOrActiveDatasetTlvs(outTlvs);
}

bool MatterPlatform::exportOpenThreadDatasetHex(char* outBuffer,
                                                size_t outBufferSize,
                                                size_t* outHexLength) const {
  return thread_.exportConfiguredOrActiveDatasetHex(outBuffer, outBufferSize,
                                                   outHexLength);
}

bool MatterPlatform::sendUdp(const uint8_t* payload, uint16_t length,
                             const otIp6Address& destAddr, uint16_t destPort) {
  if (payload == nullptr || length == 0U) {
    lastError_ = static_cast<uint32_t>(OT_ERROR_INVALID_ARGS);
    return false;
  }

  if (!udpBound_ || !thread_.udpOpened()) {
    lastError_ = static_cast<uint32_t>(OT_ERROR_INVALID_STATE);
    return false;
  }

  const bool ok = thread_.sendUdp(destAddr, destPort, payload, length);
  if (ok) {
    txCount_++;
  }
  lastError_ = static_cast<uint32_t>(ok ? OT_ERROR_NONE : thread_.lastUdpError());
  return ok;
}

bool MatterPlatform::setReceiveCallback(
    void (*callback)(void* context, const uint8_t* payload, uint16_t length,
                     const otIp6Address& source, uint16_t sourcePort),
    void* context) {
  receiveCallback_ = callback;
  receiveContext_ = context;

  if (callback == nullptr) {
    return true;
  }

  if (callback != nullptr && !udpBound_) {
    const bool opened =
        thread_.openUdp(config_.udpPort, handleUdpReceiveStatic, this);
    if (opened) {
      udpBound_ = true;
    }
    lastError_ =
        static_cast<uint32_t>(opened ? OT_ERROR_NONE : thread_.lastUdpError());
    return opened;
  }

  return true;
}

bool MatterPlatform::setFactoryData(const uint8_t* data, size_t length) {
  if (data == nullptr && length != 0U) {
    return false;
  }

  if (length > sizeof(factoryData_)) {
    return false;
  }

  if (length == 0U) {
    factoryDataLength_ = 0U;
    memset(factoryData_, 0, sizeof(factoryData_));
    const bool ok = !storageOpen_ || clearFactoryDataFromStorage();
    lastError_ = static_cast<uint32_t>(ok ? OT_ERROR_NONE : OT_ERROR_FAILED);
    return ok;
  }

  memcpy(factoryData_, data, length);
  factoryDataLength_ = length;
  const bool ok = !storageOpen_ || persistFactoryDataToStorage();
  lastError_ = static_cast<uint32_t>(ok ? OT_ERROR_NONE : OT_ERROR_FAILED);
  return ok;
}

bool MatterPlatform::getFactoryData(uint8_t* outData, size_t maxLength,
                                    size_t* outLength) const {
  if (outLength != nullptr) {
    *outLength = 0U;
  }

  if (outData == nullptr) {
    return factoryDataLength_ > 0U;
  }

  if (maxLength < factoryDataLength_) {
    return false;
  }

  if (factoryDataLength_ > 0U) {
    memcpy(outData, factoryData_, factoryDataLength_);
  }

  if (outLength != nullptr) {
    *outLength = factoryDataLength_;
  }
  return true;
}

size_t MatterPlatform::factoryDataLength() const {
  return factoryDataLength_;
}

Nrf54ThreadExperimental& MatterPlatform::thread() {
  return thread_;
}

const Nrf54ThreadExperimental& MatterPlatform::thread() const {
  return thread_;
}

uint32_t MatterPlatform::uptimeMs() const {
  return millis();
}

uint32_t MatterPlatform::getMonotonicMilliseconds() const {
  return millis();
}

bool MatterPlatform::getUniqueId(uint8_t outId[16]) {
  if (outId == nullptr) {
    return false;
  }

  // Use FICR INFO device UUID + derivation
  const uint64_t deviceId =
      *reinterpret_cast<const volatile uint32_t*>(0xFFC000A0UL) |
      (static_cast<uint64_t>(
           *reinterpret_cast<const volatile uint32_t*>(0xFFC000A4UL))
       << 32U);

  for (size_t i = 0; i < 8; ++i) {
    outId[i] = static_cast<uint8_t>(deviceId >> (i * 8U));
    outId[i + 8] = static_cast<uint8_t>(
        (deviceId ^ 0x5A3C9E27F4B18D06ULL) >> (i * 8U));
  }
  return true;
}

uint64_t MatterPlatform::getHardwareUniqueId() {
  return *reinterpret_cast<const volatile uint32_t*>(0xFFC000A0UL) |
         (static_cast<uint64_t>(
              *reinterpret_cast<const volatile uint32_t*>(0xFFC000A4UL))
          << 32U);
}

void MatterPlatform::secureZero(void* ptr, size_t length) {
  if (ptr == nullptr) {
    return;
  }
  volatile uint8_t* bytes = static_cast<volatile uint8_t*>(ptr);
  while (length-- > 0U) {
    *bytes++ = 0U;
  }
}

const char* MatterPlatform::threadDatasetSourceName(
    MatterPlatformThreadDatasetSource source) {
  switch (source) {
    case MatterPlatformThreadDatasetSource::kNone:
      return "none";
    case MatterPlatformThreadDatasetSource::kConfiguredDataset:
      return "configured";
    case MatterPlatformThreadDatasetSource::kConfiguredTlvs:
      return "configured_tlvs";
    case MatterPlatformThreadDatasetSource::kDemoDataset:
      return "demo";
    case MatterPlatformThreadDatasetSource::kRestoredSettings:
      return "settings";
    case MatterPlatformThreadDatasetSource::kActiveOpenThread:
      return "active";
    default:
      return "unknown";
  }
}

void MatterPlatform::handleUdpReceiveStatic(
    void* context, const uint8_t* payload, uint16_t length,
    const otMessageInfo& messageInfo) {
  if (context == nullptr) {
    return;
  }

  MatterPlatform* platform = static_cast<MatterPlatform*>(context);
  platform->rxCount_++;

  if (platform->receiveCallback_ != nullptr) {
    platform->receiveCallback_(platform->receiveContext_, payload, length,
                               messageInfo.mPeerAddr,
                               messageInfo.mPeerPort);
  } else {
    platform->dropCount_++;
  }
}

bool MatterPlatform::applyConfiguredThreadDataset() {
  if (config_.threadDatasetTlvs != nullptr) {
    return setThreadDatasetTlvs(*config_.threadDatasetTlvs);
  }

  if (config_.threadDataset != nullptr) {
    return setThreadDataset(*config_.threadDataset);
  }

  if (config_.useDemoThreadDataset) {
    return useDemoThreadDataset();
  }

  return true;
}

bool MatterPlatform::startThreadFromConfig() {
  if (!applyConfiguredThreadDataset()) {
    return false;
  }

  bool ok = false;
  switch (config_.threadAttachPolicy) {
    case Nrf54ThreadExperimental::AttachPolicy::kChildOnly:
      ok = thread_.beginAsChild(config_.wipeSettings);
      break;
    case Nrf54ThreadExperimental::AttachPolicy::kRouterEligible:
      ok = thread_.beginAsRouter(config_.wipeSettings);
      break;
    case Nrf54ThreadExperimental::AttachPolicy::kJoinerOnly:
      ok = thread_.beginJoinerOnly(config_.wipeSettings);
      break;
    case Nrf54ThreadExperimental::AttachPolicy::kChildFirst:
    default:
      ok = thread_.beginChildFirst(config_.wipeSettings);
      break;
  }

  lastError_ = static_cast<uint32_t>(ok ? OT_ERROR_NONE : thread_.lastError());
  return ok;
}

bool MatterPlatform::openStorageFromConfig() {
  const char* storageNamespace = config_.storageNamespace;
  if (storageNamespace == nullptr || storageNamespace[0] == '\0') {
    storageNamespace = "matter_plat";
  }

  Preferences storage;
  if (!storage.begin(storageNamespace, false)) {
    lastError_ = static_cast<uint32_t>(OT_ERROR_FAILED);
    storageOpen_ = false;
    return false;
  }

  if (config_.wipeSettings) {
    (void)storage.remove(kFactoryDataKey);
  }

  storage.end();
  storageOpen_ = true;

  if (factoryDataLength_ > 0U) {
    return persistFactoryDataToStorage();
  }

  return loadFactoryDataFromStorage();
}

bool MatterPlatform::loadFactoryDataFromStorage() {
  const char* storageNamespace = config_.storageNamespace;
  if (storageNamespace == nullptr || storageNamespace[0] == '\0') {
    storageNamespace = "matter_plat";
  }

  Preferences storage;
  if (!storage.begin(storageNamespace, true)) {
    lastError_ = static_cast<uint32_t>(OT_ERROR_FAILED);
    return false;
  }

  const size_t storedLength = storage.getBytesLength(kFactoryDataKey);
  if (storedLength == 0U) {
    storage.end();
    factoryDataLength_ = 0U;
    memset(factoryData_, 0, sizeof(factoryData_));
    lastError_ = static_cast<uint32_t>(OT_ERROR_NONE);
    return true;
  }

  if (storedLength > sizeof(factoryData_)) {
    storage.end();
    (void)clearFactoryDataFromStorage();
    factoryDataLength_ = 0U;
    memset(factoryData_, 0, sizeof(factoryData_));
    lastError_ = static_cast<uint32_t>(OT_ERROR_NO_BUFS);
    return true;
  }

  const size_t copied = storage.getBytes(kFactoryDataKey, factoryData_,
                                         sizeof(factoryData_));
  storage.end();
  if (copied != storedLength) {
    factoryDataLength_ = 0U;
    memset(factoryData_, 0, sizeof(factoryData_));
    lastError_ = static_cast<uint32_t>(OT_ERROR_FAILED);
    return false;
  }

  factoryDataLength_ = copied;
  lastError_ = static_cast<uint32_t>(OT_ERROR_NONE);
  return true;
}

bool MatterPlatform::persistFactoryDataToStorage() const {
  const char* storageNamespace = config_.storageNamespace;
  if (storageNamespace == nullptr || storageNamespace[0] == '\0') {
    storageNamespace = "matter_plat";
  }

  Preferences storage;
  if (!storage.begin(storageNamespace, false)) {
    return false;
  }

  bool ok = true;
  if (factoryDataLength_ == 0U) {
    ok = storage.remove(kFactoryDataKey);
  } else {
    ok = storage.putBytes(kFactoryDataKey, factoryData_, factoryDataLength_) ==
         factoryDataLength_;
  }
  storage.end();
  return ok;
}

bool MatterPlatform::clearFactoryDataFromStorage() const {
  const char* storageNamespace = config_.storageNamespace;
  if (storageNamespace == nullptr || storageNamespace[0] == '\0') {
    storageNamespace = "matter_plat";
  }

  Preferences storage;
  if (!storage.begin(storageNamespace, false)) {
    return false;
  }

  const bool ok = !storage.isKey(kFactoryDataKey) ||
                  storage.remove(kFactoryDataKey);
  storage.end();
  return ok;
}

MatterPlatformThreadDatasetSource MatterPlatform::currentThreadDatasetSource()
    const {
  if (configuredThreadDatasetSource_ != MatterPlatformThreadDatasetSource::kNone) {
    return configuredThreadDatasetSource_;
  }

  Nrf54ThreadExperimental::DatasetRestoreDiagnostics restore = {};
  if (thread_.getDatasetRestoreDiagnostics(&restore)) {
    if (restore.restored) {
      return MatterPlatformThreadDatasetSource::kRestoredSettings;
    }
    if (restore.datasetConfigured) {
      return MatterPlatformThreadDatasetSource::kConfiguredDataset;
    }
  }

  otOperationalDatasetTlvs tlvs = {};
  if (thread_.getActiveDatasetTlvs(&tlvs)) {
    return MatterPlatformThreadDatasetSource::kActiveOpenThread;
  }

  return MatterPlatformThreadDatasetSource::kNone;
}

void MatterPlatform::copyReadinessBlocker(MatterPlatformState* outState) const {
  if (outState == nullptr) {
    return;
  }

  if (!storageOpen_) {
    copyText(outState->readinessBlocker, sizeof(outState->readinessBlocker),
             "storage_closed");
    return;
  }

  if (!thread_.started()) {
    copyText(outState->readinessBlocker, sizeof(outState->readinessBlocker),
             "thread_not_started");
    return;
  }

  if (!outState->threadDatasetConfigured &&
      !outState->threadDatasetExportable) {
    const char* restoreBlocker =
        outState->threadRestoreDiagnostics.blockerName[0] != '\0'
            ? outState->threadRestoreDiagnostics.blockerName
            : "thread_dataset_missing";
    copyText(outState->readinessBlocker, sizeof(outState->readinessBlocker),
             restoreBlocker);
    return;
  }

  if (!thread_.attached()) {
    const char* attachBlocker =
        outState->threadAttachSummary.blockerName[0] != '\0'
            ? outState->threadAttachSummary.blockerName
            : "thread_not_attached";
    copyText(outState->readinessBlocker, sizeof(outState->readinessBlocker),
             attachBlocker);
    return;
  }

  if (udpBound_ && !thread_.udpOpened(config_.udpPort)) {
    copyText(outState->readinessBlocker, sizeof(outState->readinessBlocker),
             "udp_bind_pending");
    return;
  }

  copyText(outState->readinessBlocker, sizeof(outState->readinessBlocker),
           "none");
}

}  // namespace xiao_nrf54l15
#endif // NRF54L15_CLEAN_MATTER_CORE_ENABLE
