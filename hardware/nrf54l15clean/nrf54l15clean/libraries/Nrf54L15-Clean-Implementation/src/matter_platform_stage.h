#pragma once

#include <stddef.h>
#include <stdint.h>

#include "matter_foundation_target.h"
#include "matter_pase_transport.h"
#include "nrf54_thread_experimental.h"

#if defined(NRF54L15_CLEAN_MATTER_CORE_ENABLE) && \
    (NRF54L15_CLEAN_MATTER_CORE_ENABLE != 0)
#include <lib/core/CHIPError.h>
#endif

namespace xiao_nrf54l15 {

// Matter platform initialization and runtime ownership for nRF54L15.
// This is the bridge between the Arduino core and the staged CHIP runtime.

enum class MatterPlatformThreadDatasetSource : uint8_t {
  kNone = 0U,
  kConfiguredDataset = 1U,
  kConfiguredTlvs = 2U,
  kDemoDataset = 3U,
  kRestoredSettings = 4U,
  kActiveOpenThread = 5U,
};

struct MatterPlatformConfig {
  const char* storageNamespace = "matter_plat";
  bool autoStartThread = true;
  bool wipeSettings = false;
  Nrf54ThreadExperimental::AttachPolicy threadAttachPolicy =
      Nrf54ThreadExperimental::AttachPolicy::kChildFirst;
  const otOperationalDataset* threadDataset = nullptr;
  const otOperationalDatasetTlvs* threadDatasetTlvs = nullptr;
  bool useDemoThreadDataset = false;
  uint16_t udpPort = Nrf54MatterOnOffLightFoundation::kMatterUdpPort;
};

struct MatterPlatformState {
  bool initialized = false;
  bool storageOpen = false;
  bool threadStarted = false;
  bool threadAttached = false;
  bool udpBound = false;
  bool transportReady = false;
  bool threadDatasetConfigured = false;
  bool threadDatasetExportable = false;
  uint32_t uptimeMs = 0U;
  uint32_t rxCount = 0U;
  uint32_t txCount = 0U;
  uint32_t dropCount = 0U;
  uint32_t lastError = 0U;
  uint16_t rloc16 = OT_RADIO_INVALID_SHORT_ADDR;
  MatterPlatformThreadDatasetSource threadDatasetSource =
      MatterPlatformThreadDatasetSource::kNone;
  Nrf54ThreadExperimental::Role threadRole =
      Nrf54ThreadExperimental::Role::kUnknown;
  Nrf54ThreadExperimental::AttachSummary threadAttachSummary = {};
  Nrf54ThreadExperimental::DatasetRestoreDiagnostics
      threadRestoreDiagnostics = {};
  char readinessBlocker[48] = {0};
  char threadDatasetSourceName[20] = {0};
};

class MatterPlatform : public MatterPaseTransport {
 public:
  MatterPlatform() = default;

  bool begin(const MatterPlatformConfig& config = MatterPlatformConfig());
  void end();
  void process();

  bool ready() const;
  bool snapshot(MatterPlatformState* outState) const;
  bool transportReady() const;
  bool setThreadDataset(const otOperationalDataset& dataset);
  bool setThreadDatasetTlvs(const otOperationalDatasetTlvs& datasetTlvs);
  bool useDemoThreadDataset();
  bool exportOpenThreadDatasetTlvs(otOperationalDatasetTlvs* outTlvs) const;
  bool exportOpenThreadDatasetHex(char* outBuffer, size_t outBufferSize,
                                  size_t* outHexLength = nullptr) const;
  bool sendUdp(const uint8_t* payload, uint16_t length,
               const otIp6Address& destAddr,
               uint16_t destPort) override;
  bool setReceiveCallback(void (*callback)(void* context,
                                           const uint8_t* payload,
                                           uint16_t length,
                                           const otIp6Address& source,
                                           uint16_t sourcePort),
                          void* context = nullptr) override;
  bool setFactoryData(const uint8_t* data, size_t length);
  bool getFactoryData(uint8_t* outData, size_t maxLength,
                      size_t* outLength = nullptr) const;
  size_t factoryDataLength() const;

  Nrf54ThreadExperimental& thread();
  const Nrf54ThreadExperimental& thread() const;

  uint32_t uptimeMs() const;
  uint32_t getMonotonicMilliseconds() const;

  static bool getUniqueId(uint8_t outId[16]);
  static uint64_t getHardwareUniqueId();
  static void secureZero(void* ptr, size_t length);
  static const char* threadDatasetSourceName(
      MatterPlatformThreadDatasetSource source);

 private:
  static void handleUdpReceiveStatic(void* context, const uint8_t* payload,
                                     uint16_t length,
                                     const otMessageInfo& messageInfo);
  bool applyConfiguredThreadDataset();
  bool startThreadFromConfig();
  bool openStorageFromConfig();
  bool loadFactoryDataFromStorage();
  bool persistFactoryDataToStorage() const;
  bool clearFactoryDataFromStorage() const;
  MatterPlatformThreadDatasetSource currentThreadDatasetSource() const;
  void copyReadinessBlocker(MatterPlatformState* outState) const;

  MatterPlatformConfig config_ = {};
  Nrf54ThreadExperimental thread_;
  bool storageOpen_ = false;
  bool udpBound_ = false;
  MatterPlatformThreadDatasetSource configuredThreadDatasetSource_ =
      MatterPlatformThreadDatasetSource::kNone;
  uint32_t rxCount_ = 0U;
  uint32_t txCount_ = 0U;
  uint32_t dropCount_ = 0U;
  uint32_t lastError_ = 0U;
  void (*receiveCallback_)(void* context, const uint8_t* payload,
                           uint16_t length, const otIp6Address& source,
                           uint16_t sourcePort) = nullptr;
  void* receiveContext_ = nullptr;
  uint8_t factoryData_[128] = {0};
  size_t factoryDataLength_ = 0U;
};

inline const char* matterPlatformBuildMode() {
  return MatterRuntimeOwnership::kMatterBuildSeamCurrentEnabled
             ? "staged-platform"
             : "disabled";
}

}  // namespace xiao_nrf54l15
