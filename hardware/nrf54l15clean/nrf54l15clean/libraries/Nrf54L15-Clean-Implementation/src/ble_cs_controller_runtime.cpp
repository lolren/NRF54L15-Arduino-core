#include "ble_cs_controller_runtime.h"

#include <string.h>

#include "Arduino.h"
#include "cmsis.h"
#include "nrf54l15_hal.h"

namespace {

using xiao_nrf54l15::BleCsControllerRuntime;

constexpr IRQn_Type kMpslTimerIrq = TIMER10_IRQn;
constexpr IRQn_Type kMpslRtcIrq = GRTC_3_IRQn;
constexpr IRQn_Type kMpslRadioIrq = RADIO_0_IRQn;
constexpr IRQn_Type kMpslLowPriorityIrq = SWI00_IRQn;
constexpr IRQn_Type kMpslClockIrq = CLOCK_POWER_IRQn;
constexpr uint32_t kMpslHighPriority = 0U;
constexpr uint32_t kMpslClockPriority = 2U;
constexpr uint32_t kMpslLowPriority = 4U;
constexpr uint32_t kRequiredCpuFrequencyHz = 128000000UL;
constexpr uint16_t kHfClockWorstCaseStartUs = 1400U;
constexpr uint8_t kHciEventMessageType = 0x04U;
constexpr uint8_t kHciLeMetaEvent = 0x3EU;
constexpr uint8_t kHciLeCsSubeventResult = 0x31U;
constexpr uint8_t kHciLeCsSubeventResultContinue = 0x32U;
constexpr uint8_t kHciLeCsTestEndComplete = 0x33U;
constexpr uint8_t kCsProcedureComplete = 0x00U;
constexpr uint8_t kCsProcedureAborted = 0x0FU;
constexpr int32_t kNrfEagain = -35;
constexpr size_t kControllerMemorySize = 16384U;
constexpr uint8_t kSdcDefaultResourceTag = 0U;
constexpr uint8_t kSdcConfigNone = 0U;
constexpr uint8_t kSdcConfigCentralCount = 1U;
constexpr uint8_t kSdcConfigPeripheralCount = 2U;
constexpr uint8_t kSdcConfigCsCount = 21U;
constexpr uint8_t kSdcConfigCsCapabilities = 22U;
constexpr uint8_t kCsToneA1B1 = 0U;
constexpr uint8_t kCsSnrNotUsed = 0xFFU;
constexpr uint16_t kCsOverrideConfigMainModeSteps = (1U << 2U);
constexpr uint16_t kCsOverrideConfigAccessAddresses = (1U << 5U);
constexpr uint8_t kCsOverrideParametersLength = 22U;
constexpr uint32_t kRadioQuiesceSpinLimit = 2000000UL;

bool releaseMpslRadioOwnershipIfQuiesced(uint32_t* token) {
  if (token == nullptr || *token == 0U) {
    return true;
  }
  if (!nrf54l15_quiesce_owned_radio(
          Nrf54ExclusiveRadioOwner::kMpslChannelSounding, *token,
          kRadioQuiesceSpinLimit) ||
      !nrf54l15_release_exclusive_radio(
          Nrf54ExclusiveRadioOwner::kMpslChannelSounding, *token)) {
    return false;
  }
  *token = 0U;
  return true;
}

struct MpslClockConfig {
  uint8_t source;
  uint8_t rcCalibrationInterval;
  uint8_t rcTemperatureInterval;
  uint16_t accuracyPpm;
  bool skipWaitForStart;
};

struct SdcRandomSource {
  void (*poll)(uint8_t* destination, uint8_t length);
};

struct SdcHciEventMask {
  uint8_t raw[8];
};

union SdcConfig {
  uint8_t bytes[16];
  struct {
    uint8_t count;
  } roleCount;
  struct {
    uint8_t maxAntennaPaths;
    uint8_t antennaCount;
  } csCapabilities;
};

struct __attribute__((packed)) SdcCsTestCommand {
  uint8_t mainModeType;
  uint8_t subModeType;
  uint8_t mainModeRepetition;
  uint8_t mode0Steps;
  uint8_t role;
  uint8_t rttType;
  uint8_t syncPhy;
  uint8_t syncAntenna;
  uint8_t subeventLength[3];
  uint16_t subeventInterval;
  uint8_t maxSubevents;
  uint8_t transmitPower;
  uint8_t tIp1;
  uint8_t tIp2;
  uint8_t tFcs;
  uint8_t tPm;
  uint8_t tSw;
  uint8_t toneAntennaConfig;
  uint8_t enhancements;
  uint8_t initiatorSnr;
  uint8_t reflectorSnr;
  uint16_t drbgNonce;
  uint8_t channelMapRepetition;
  uint16_t overrideConfig;
  uint8_t overrideParametersLength;
};

static_assert(sizeof(MpslClockConfig) == 8U, "Unexpected MPSL clock ABI");
static_assert(sizeof(SdcCsTestCommand) == 30U, "Unexpected SDC CS test ABI");

extern "C" {
typedef void (*MpslAssertHandler)(const char*, uint32_t);
typedef void (*SdcEventCallback)(void);

int32_t mpsl_init(const MpslClockConfig* config, IRQn_Type lowPriorityIrq,
                  MpslAssertHandler assertHandler);
void mpsl_uninit(void);
bool mpsl_is_initialized(void);
void mpsl_low_priority_process(void);
void MPSL_IRQ_TIMER0_Handler(void);
void MPSL_IRQ_RTC0_Handler(void);
void MPSL_IRQ_RADIO_Handler(void);
void MPSL_IRQ_CLOCK_Handler(void);
int32_t mpsl_clock_hfclk_latency_set(uint16_t rampUpTimeUs);

int32_t sdc_init(MpslAssertHandler faultHandler);
void sdc_support_channel_sounding_test(void);
int32_t sdc_cfg_set(uint8_t configTag, uint8_t configType,
                    const SdcConfig* config);
int32_t sdc_rand_source_register(const SdcRandomSource* randomSource);
int32_t sdc_enable(SdcEventCallback callback, uint8_t* memory);
int32_t sdc_disable(void);
int32_t sdc_hci_get(uint8_t* packet, uint8_t* messageType);
uint8_t sdc_hci_cmd_cb_reset(void);
uint8_t sdc_hci_cmd_cb_set_event_mask(const SdcHciEventMask* command);
uint8_t sdc_hci_cmd_le_set_event_mask(const SdcHciEventMask* command);
uint8_t sdc_hci_cmd_le_cs_test(const SdcCsTestCommand* command);
uint8_t sdc_hci_cmd_le_cs_test_end(void);

void nrf54l15_ble_clock_irq_service(void) __attribute__((weak));
}

alignas(8) uint8_t gControllerMemory[kControllerMemorySize] = {};
BleCsControllerRuntime* gRuntimeOwner = nullptr;
volatile bool gControllerActive = false;
volatile bool gLowPriorityPending = false;
volatile bool gHciPending = false;
volatile bool gControllerFault = false;
bool gSdcInitialized = false;
uint16_t gControllerMemoryRequired = 0U;
uint32_t gSavedRramLowPowerConfig = 0U;
uint16_t gLowLatencyDepth = 0U;
bool gLowLatencyOverflowed = false;

uint32_t enterCriticalSection() {
  const uint32_t previous = __get_PRIMASK();
  __disable_irq();
  return previous;
}

void leaveCriticalSection(uint32_t previous) {
  if ((previous & 1U) == 0U) {
    __enable_irq();
  }
}

void clearPendingIrq(IRQn_Type irq) {
  const uint32_t number = static_cast<uint32_t>(irq);
  NVIC->ICPR[number >> 5U] = (1UL << (number & 0x1FU));
}

void configureMpslInterrupts() {
  NVIC_DisableIRQ(kMpslTimerIrq);
  NVIC_DisableIRQ(kMpslRtcIrq);
  NVIC_DisableIRQ(kMpslRadioIrq);
  NVIC_DisableIRQ(kMpslLowPriorityIrq);
  NVIC_DisableIRQ(kMpslClockIrq);

  clearPendingIrq(kMpslTimerIrq);
  clearPendingIrq(kMpslRtcIrq);
  clearPendingIrq(kMpslRadioIrq);
  clearPendingIrq(kMpslLowPriorityIrq);
  clearPendingIrq(kMpslClockIrq);

  NVIC_SetPriority(kMpslTimerIrq, kMpslHighPriority);
  NVIC_SetPriority(kMpslRtcIrq, kMpslHighPriority);
  NVIC_SetPriority(kMpslRadioIrq, kMpslHighPriority);
  NVIC_SetPriority(kMpslClockIrq, kMpslClockPriority);
  NVIC_SetPriority(kMpslLowPriorityIrq, kMpslLowPriority);
}

void enableMpslInterrupts() {
  NVIC_EnableIRQ(kMpslTimerIrq);
  NVIC_EnableIRQ(kMpslRtcIrq);
  NVIC_EnableIRQ(kMpslRadioIrq);
  NVIC_EnableIRQ(kMpslClockIrq);
  NVIC_EnableIRQ(kMpslLowPriorityIrq);
}

void disableMpslInterrupts() {
  NVIC_DisableIRQ(kMpslTimerIrq);
  NVIC_DisableIRQ(kMpslRtcIrq);
  NVIC_DisableIRQ(kMpslRadioIrq);
  NVIC_DisableIRQ(kMpslClockIrq);
  NVIC_DisableIRQ(kMpslLowPriorityIrq);
  clearPendingIrq(kMpslTimerIrq);
  clearPendingIrq(kMpslRtcIrq);
  clearPendingIrq(kMpslRadioIrq);
  clearPendingIrq(kMpslClockIrq);
  clearPendingIrq(kMpslLowPriorityIrq);
}

void ensureGrtcRunning() {
  uint32_t mode = NRF_GRTC->MODE;
  mode &= ~GRTC_MODE_SYSCOUNTEREN_Msk;
  mode |= (GRTC_MODE_SYSCOUNTEREN_Enabled << GRTC_MODE_SYSCOUNTEREN_Pos);
  NRF_GRTC->MODE = mode;
  NRF_GRTC->TASKS_START = GRTC_TASKS_START_TASKS_START_Trigger;
  __DSB();
  __ISB();
}

void controllerFaultHandler(const char*, uint32_t) {
  gControllerFault = true;
}

void controllerEventCallback() {
  gHciPending = true;
}

void controllerRandomPoll(uint8_t* destination, uint8_t length) {
  xiao_nrf54l15::CracenRng rng;
  while (!rng.fill(destination, length, 800000UL)) {
    rng.end();
  }
}

bool procedureFinished(uint8_t status) {
  const uint8_t doneStatus = status & 0x0FU;
  return doneStatus == kCsProcedureComplete ||
         doneStatus == kCsProcedureAborted;
}

uint16_t hciPacketLength(uint8_t messageType, const uint8_t* packet) {
  if (messageType == kHciEventMessageType) {
    return static_cast<uint16_t>(packet[1]) + 2U;
  }
  return 0U;
}

}  // namespace

extern "C" void mpsl_low_latency_acquire_callback(void) {
  const uint32_t previous = enterCriticalSection();
  if (gLowLatencyOverflowed) {
    gControllerFault = true;
    leaveCriticalSection(previous);
    return;
  }
  if (gLowLatencyDepth == 0U) {
    gSavedRramLowPowerConfig = NRF_RRAMC->POWER.LOWPOWERCONFIG;
    if (nrf54l15_constlat_acquire() == 0U) {
      gControllerFault = true;
      leaveCriticalSection(previous);
      return;
    }
    NRF_RRAMC->POWER.LOWPOWERCONFIG =
        (RRAMC_POWER_LOWPOWERCONFIG_MODE_Standby
         << RRAMC_POWER_LOWPOWERCONFIG_MODE_Pos);
  }
  if (gLowLatencyDepth != UINT16_MAX) {
    ++gLowLatencyDepth;
  } else {
    gLowLatencyOverflowed = true;
    gControllerFault = true;
  }
  leaveCriticalSection(previous);
}

extern "C" void mpsl_low_latency_release_callback(void) {
  const uint32_t previous = enterCriticalSection();
  if (!gLowLatencyOverflowed && gLowLatencyDepth > 0U) {
    --gLowLatencyDepth;
    if (gLowLatencyDepth == 0U) {
      NRF_RRAMC->POWER.LOWPOWERCONFIG = gSavedRramLowPowerConfig;
      nrf54l15_constlat_release();
    }
  }
  leaveCriticalSection(previous);
}

extern "C" void TIMER10_IRQHandler(void) {
  if (gControllerActive &&
      nrf54l15_exclusive_radio_owner() ==
          Nrf54ExclusiveRadioOwner::kMpslChannelSounding) {
    MPSL_IRQ_TIMER0_Handler();
  }
}

extern "C" void GRTC_3_IRQHandler(void) {
  if (gControllerActive &&
      nrf54l15_exclusive_radio_owner() ==
          Nrf54ExclusiveRadioOwner::kMpslChannelSounding) {
    MPSL_IRQ_RTC0_Handler();
  }
}

extern "C" void SWI00_IRQHandler(void) {
  if (gControllerActive &&
      nrf54l15_exclusive_radio_owner() ==
          Nrf54ExclusiveRadioOwner::kMpslChannelSounding) {
    gLowPriorityPending = true;
  }
}

extern "C" void CLOCK_POWER_IRQHandler(void) {
  const Nrf54ExclusiveRadioOwner owner = nrf54l15_exclusive_radio_owner();
  if (gControllerActive &&
      owner == Nrf54ExclusiveRadioOwner::kMpslChannelSounding) {
    MPSL_IRQ_CLOCK_Handler();
    return;
  }
  NVIC_DisableIRQ(kMpslClockIrq);
  if (owner == Nrf54ExclusiveRadioOwner::kBle &&
      nrf54l15_ble_clock_irq_service != nullptr) {
    nrf54l15_ble_clock_irq_service();
  }
}

extern "C" bool nrf54_cs_controller_radio_irq_service(void) {
  if (!gControllerActive ||
      nrf54l15_exclusive_radio_owner() !=
          Nrf54ExclusiveRadioOwner::kMpslChannelSounding) {
    return false;
  }
  MPSL_IRQ_RADIO_Handler();
  return true;
}

namespace xiao_nrf54l15 {

BleCsControllerRuntime::BleCsControllerRuntime() = default;

BleCsControllerRuntime::~BleCsControllerRuntime() {
  if (!end() && radioOwnershipToken_ != 0U) {
    (void)nrf54l15_quarantine_exclusive_radio(
        Nrf54ExclusiveRadioOwner::kMpslChannelSounding,
        radioOwnershipToken_);
    if (gRuntimeOwner == this) {
      gRuntimeOwner = nullptr;
    }
    nrf54l15_exclusive_radio_fail_stop();
  }
}

bool BleCsControllerRuntime::begin() {
  if (active_) {
    return true;
  }
  if (gRuntimeOwner != nullptr && gRuntimeOwner != this) {
    setError(kErrorInvalidState);
    return false;
  }
  if (nrf54l15_core_get_cpu_frequency_hz() != kRequiredCpuFrequencyHz) {
    setError(kErrorRequires128MHz);
    return false;
  }
  if (radioOwnershipToken_ != 0U) {
    setError(kErrorInvalidState);
    return false;
  }
  radioOwnershipToken_ = nrf54l15_acquire_exclusive_radio(
      Nrf54ExclusiveRadioOwner::kMpslChannelSounding);
  if (radioOwnershipToken_ == 0U) {
    setError(kErrorRadioBusy);
    return false;
  }
  if (!nrf54l15_quiesce_owned_radio(
          Nrf54ExclusiveRadioOwner::kMpslChannelSounding,
          radioOwnershipToken_, kRadioQuiesceSpinLimit)) {
    setError(kErrorRadioBusy);
    return false;
  }
  if (gLowLatencyOverflowed) {
    setError(kErrorInvalidState);
    (void)releaseMpslRadioOwnershipIfQuiesced(&radioOwnershipToken_);
    return false;
  }

  resetPackets();
  testRunning_ = false;
  testComplete_ = false;
  lastError_ = 0;
  gControllerFault = false;
  gLowPriorityPending = false;
  gHciPending = false;
  gRuntimeOwner = this;
  gControllerActive = true;

  ensureGrtcRunning();
  configureMpslInterrupts();

  const MpslClockConfig clockConfig = {
      1U,
      0U,
      0U,
      50U,
      false,
  };
  int32_t result = mpsl_init(&clockConfig, kMpslLowPriorityIrq,
                             controllerFaultHandler);
  if (result != 0) {
    setError(result);
    gControllerActive = false;
    gRuntimeOwner = nullptr;
    disableMpslInterrupts();
    (void)releaseMpslRadioOwnershipIfQuiesced(&radioOwnershipToken_);
    return false;
  }

  result = mpsl_clock_hfclk_latency_set(kHfClockWorstCaseStartUs);
  if (result != 0) {
    setError(result);
    gControllerActive = false;
    disableMpslInterrupts();
    mpsl_uninit();
    gRuntimeOwner = nullptr;
    (void)releaseMpslRadioOwnershipIfQuiesced(&radioOwnershipToken_);
    return false;
  }

  if (!gSdcInitialized) {
    result = sdc_init(controllerFaultHandler);
    if (result != 0) {
      setError(result);
      gControllerActive = false;
      disableMpslInterrupts();
      mpsl_uninit();
      gRuntimeOwner = nullptr;
      (void)releaseMpslRadioOwnershipIfQuiesced(&radioOwnershipToken_);
      return false;
    }

    sdc_support_channel_sounding_test();

    SdcConfig config = {};
    config.roleCount.count = 0U;
    result = sdc_cfg_set(kSdcDefaultResourceTag, kSdcConfigCentralCount,
                         &config);
    config.roleCount.count = 1U;
    if (result >= 0) {
      result = sdc_cfg_set(kSdcDefaultResourceTag,
                           kSdcConfigPeripheralCount, &config);
    }
    config.roleCount.count = 1U;
    if (result >= 0) {
      result = sdc_cfg_set(kSdcDefaultResourceTag, kSdcConfigCsCount,
                           &config);
    }
    config.csCapabilities.maxAntennaPaths = 1U;
    config.csCapabilities.antennaCount = 1U;
    if (result >= 0) {
      result = sdc_cfg_set(kSdcDefaultResourceTag,
                           kSdcConfigCsCapabilities, &config);
    }
    if (result >= 0) {
      result = sdc_cfg_set(kSdcDefaultResourceTag, kSdcConfigNone, &config);
    }
    if (result < 0 || static_cast<size_t>(result) > kControllerMemorySize) {
      setError(result < 0 ? result : kErrorControllerMemory);
      gControllerActive = false;
      disableMpslInterrupts();
      mpsl_uninit();
      gRuntimeOwner = nullptr;
      (void)releaseMpslRadioOwnershipIfQuiesced(&radioOwnershipToken_);
      return false;
    }
    controllerMemoryRequired_ = static_cast<uint16_t>(result);
    gControllerMemoryRequired = controllerMemoryRequired_;
    gSdcInitialized = true;
  }
  controllerMemoryRequired_ = gControllerMemoryRequired;

  const SdcRandomSource randomSource = {controllerRandomPoll};
  result = sdc_rand_source_register(&randomSource);
  if (result == 0) {
    result = sdc_enable(controllerEventCallback, gControllerMemory);
  }
  if (result != 0) {
    setError(result);
    gControllerActive = false;
    disableMpslInterrupts();
    mpsl_uninit();
    gRuntimeOwner = nullptr;
    (void)releaseMpslRadioOwnershipIfQuiesced(&radioOwnershipToken_);
    return false;
  }

  const SdcHciEventMask baseEventMask = {
      {0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x20U}};
  const SdcHciEventMask leCsEventMask = {
      {0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x07U, 0x00U}};
  uint8_t hciStatus = sdc_hci_cmd_cb_reset();
  if (hciStatus == 0U) {
    hciStatus = sdc_hci_cmd_cb_set_event_mask(&baseEventMask);
  }
  if (hciStatus == 0U) {
    hciStatus = sdc_hci_cmd_le_set_event_mask(&leCsEventMask);
  }
  if (hciStatus != 0U) {
    setError(static_cast<int32_t>(hciStatus));
    const int32_t disableResult = sdc_disable();
    if (disableResult != 0) {
      setError(disableResult);
      enableMpslInterrupts();
      active_ = true;
      return false;
    }
    gControllerActive = false;
    disableMpslInterrupts();
    mpsl_uninit();
    gRuntimeOwner = nullptr;
    (void)releaseMpslRadioOwnershipIfQuiesced(&radioOwnershipToken_);
    return false;
  }

  enableMpslInterrupts();
  active_ = true;
  return true;
}

bool BleCsControllerRuntime::startTest(
    BleCsControllerRole role, const BleCsControllerTestConfig& config) {
  if (!active_ || testRunning_ || config.subeventLengthUs > 0xFFFFFFUL ||
      config.channelMapRepetition == 0U) {
    setError(kErrorInvalidState);
    return false;
  }
  if (config.mainModeType != 2U || config.subModeType != 1U ||
      config.mainModeRepetition != 1U || config.mode0Steps != 3U ||
      config.mainModeSteps != 8U || config.rttType != 0U ||
      config.syncPhy != 1U || config.syncAntenna != 1U ||
      config.channelMapRepetition != 1U) {
    setError(kErrorUnsupportedTestConfig);
    return false;
  }

  resetPackets();

  alignas(4) uint8_t commandBytes[sizeof(SdcCsTestCommand) +
                                  kCsOverrideParametersLength] = {};
  SdcCsTestCommand* command =
      reinterpret_cast<SdcCsTestCommand*>(commandBytes);
  command->mainModeType = config.mainModeType;
  command->subModeType = config.subModeType;
  command->mainModeRepetition = config.mainModeRepetition;
  command->mode0Steps = config.mode0Steps;
  command->role = static_cast<uint8_t>(role);
  command->rttType = config.rttType;
  command->syncPhy = config.syncPhy;
  command->syncAntenna = config.syncAntenna;
  command->subeventLength[0] =
      static_cast<uint8_t>(config.subeventLengthUs & 0xFFU);
  command->subeventLength[1] =
      static_cast<uint8_t>((config.subeventLengthUs >> 8U) & 0xFFU);
  command->subeventLength[2] =
      static_cast<uint8_t>((config.subeventLengthUs >> 16U) & 0xFFU);
  command->subeventInterval = 0U;
  command->maxSubevents = 1U;
  command->transmitPower = config.transmitPower;
  command->tIp1 = config.tIp1Us;
  command->tIp2 = config.tIp2Us;
  command->tFcs = config.tFcsUs;
  command->tPm = config.tPmUs;
  command->tSw = 0U;
  command->toneAntennaConfig = kCsToneA1B1;
  command->enhancements = 0U;
  command->initiatorSnr = kCsSnrNotUsed;
  command->reflectorSnr = kCsSnrNotUsed;
  command->drbgNonce = config.drbgNonce;
  command->channelMapRepetition = config.channelMapRepetition;
  command->overrideConfig = kCsOverrideConfigMainModeSteps |
                            kCsOverrideConfigAccessAddresses;
  command->overrideParametersLength = kCsOverrideParametersLength;

  uint8_t* overrideData = commandBytes + sizeof(SdcCsTestCommand);
  memcpy(overrideData, config.channelMap, sizeof(config.channelMap));
  overrideData[10] = config.channelSelectionType;
  overrideData[11] = config.channelSelectionShape;
  overrideData[12] = config.channelSelectionJump;
  overrideData[13] = config.mainModeSteps;
  for (uint8_t byte = 0U; byte < 4U; ++byte) {
    overrideData[14U + byte] = static_cast<uint8_t>(
        (config.initiatorAccessAddress >> (byte * 8U)) & 0xFFU);
    overrideData[18U + byte] = static_cast<uint8_t>(
        (config.reflectorAccessAddress >> (byte * 8U)) & 0xFFU);
  }

  const uint8_t status = sdc_hci_cmd_le_cs_test(command);
  if (status != 0U) {
    setError(static_cast<int32_t>(status));
    return false;
  }

  testRunning_ = true;
  testComplete_ = false;
  lastError_ = 0;
  return true;
}

void BleCsControllerRuntime::poll() {
  if (!active_) {
    return;
  }

  bool processLowPriority = false;
  const uint32_t previous = enterCriticalSection();
  if (gLowPriorityPending) {
    gLowPriorityPending = false;
    processLowPriority = true;
  }
  leaveCriticalSection(previous);

  if (processLowPriority) {
    mpsl_low_priority_process();
  }
  drainHciPackets();
  if (gControllerFault) {
    setError(kErrorInvalidState);
  }
}

bool BleCsControllerRuntime::readPacket(
    BleCsControllerHciPacket* outPacket) {
  if (outPacket == nullptr || packetCount_ == 0U) {
    return false;
  }
  *outPacket = packetQueue_[packetTail_];
  packetTail_ = static_cast<uint8_t>((packetTail_ + 1U) % kPacketQueueDepth);
  --packetCount_;
  return true;
}

bool BleCsControllerRuntime::stopTest() {
  if (!active_ || !testRunning_) {
    setError(kErrorInvalidState);
    return false;
  }
  const uint8_t status = sdc_hci_cmd_le_cs_test_end();
  if (status != 0U) {
    setError(static_cast<int32_t>(status));
    return false;
  }
  return true;
}

bool BleCsControllerRuntime::end() {
  if (!active_) {
    if (!releaseMpslRadioOwnershipIfQuiesced(&radioOwnershipToken_)) {
      return false;
    }
    if (gRuntimeOwner == this) {
      gRuntimeOwner = nullptr;
    }
    return true;
  }

  const int32_t result = sdc_disable();
  if (result != 0) {
    setError(result);
    return false;
  }
  gControllerActive = false;
  disableMpslInterrupts();
  mpsl_uninit();

  const uint32_t previous = enterCriticalSection();
  if (gLowLatencyOverflowed) {
    // The callback nesting count is no longer representable. Keep CONSTLAT
    // and the RRAM override latched until reset rather than releasing early.
    gLowLatencyDepth = 0U;
  } else if (gLowLatencyDepth != 0U) {
    gLowLatencyDepth = 0U;
    NRF_RRAMC->POWER.LOWPOWERCONFIG = gSavedRramLowPowerConfig;
    nrf54l15_constlat_release();
  }
  gLowPriorityPending = false;
  gHciPending = false;
  leaveCriticalSection(previous);

  active_ = false;
  testRunning_ = false;
  if (!releaseMpslRadioOwnershipIfQuiesced(&radioOwnershipToken_)) {
    return false;
  }
  if (gRuntimeOwner == this) {
    gRuntimeOwner = nullptr;
  }
  return true;
}

bool BleCsControllerRuntime::active() const {
  return active_;
}

bool BleCsControllerRuntime::testRunning() const {
  return testRunning_;
}

bool BleCsControllerRuntime::testComplete() const {
  return testComplete_;
}

int32_t BleCsControllerRuntime::lastError() const {
  return lastError_;
}

uint32_t BleCsControllerRuntime::droppedPacketCount() const {
  return droppedPacketCount_;
}

uint16_t BleCsControllerRuntime::controllerMemoryRequired() const {
  return controllerMemoryRequired_;
}

void BleCsControllerRuntime::resetPackets() {
  packetHead_ = 0U;
  packetTail_ = 0U;
  packetCount_ = 0U;
  droppedPacketCount_ = 0U;
  memset(packetQueue_, 0, sizeof(packetQueue_));
}

void BleCsControllerRuntime::drainHciPackets() {
  gHciPending = false;
  for (uint8_t packetIndex = 0U; packetIndex < 32U; ++packetIndex) {
    uint8_t data[BleCsControllerHciPacket::kMaxDataLength] = {};
    uint8_t messageType = 0U;
    const int32_t result = sdc_hci_get(data, &messageType);
    if (result == kNrfEagain) {
      break;
    }
    if (result != 0) {
      setError(result);
      break;
    }
    const uint16_t length = hciPacketLength(messageType, data);
    if (length != 0U &&
        length <= BleCsControllerHciPacket::kMaxDataLength) {
      recordPacket(messageType, data, length);
    }
  }
}

void BleCsControllerRuntime::recordPacket(uint8_t messageType,
                                          const uint8_t* data,
                                          uint16_t dataLength) {
  if (packetCount_ == kPacketQueueDepth) {
    ++droppedPacketCount_;
    setError(kErrorEventOverflow);
    return;
  }

  BleCsControllerHciPacket& packet = packetQueue_[packetHead_];
  packet.messageType = messageType;
  packet.dataLength = dataLength;
  memcpy(packet.data, data, dataLength);
  packetHead_ = static_cast<uint8_t>((packetHead_ + 1U) % kPacketQueueDepth);
  ++packetCount_;

  if (messageType != kHciEventMessageType || dataLength < 4U ||
      data[0] != kHciLeMetaEvent) {
    return;
  }

  const uint8_t subevent = data[2];
  if (subevent == kHciLeCsSubeventResult && dataLength > 14U &&
      procedureFinished(data[13]) && procedureFinished(data[14])) {
    testRunning_ = false;
    testComplete_ = true;
  } else if (subevent == kHciLeCsSubeventResultContinue && dataLength > 7U &&
             procedureFinished(data[6]) && procedureFinished(data[7])) {
    testRunning_ = false;
    testComplete_ = true;
  } else if (subevent == kHciLeCsTestEndComplete) {
    testRunning_ = false;
    testComplete_ = true;
  }
}

void BleCsControllerRuntime::setError(int32_t error) {
  lastError_ = error;
}

}  // namespace xiao_nrf54l15
