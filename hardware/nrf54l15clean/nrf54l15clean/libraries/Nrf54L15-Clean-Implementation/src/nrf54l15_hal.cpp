#include "nrf54l15_hal.h"
#include "ble_controller_state.h"
#include "nrf54l15_hal_board_policy_internal.h"
#include "nrf54l15_hal_support_internal.h"
#include "nrf54l15_hal_timebase_internal.h"
#include "matter_secp256r1.h"

#include <Arduino.h>
#include <string.h>
#include <cmsis.h>
#include "variant.h"

extern "C" uint8_t nrf54l15_constlat_users_active(void) __attribute__((weak));
extern "C" void nrf54l15_grtc_irq_observer(void) __attribute__((weak));
extern "C" bool nrf54_cs_controller_radio_irq_service(void)
    __attribute__((weak));
extern "C" void nrf54l15_grtc_irq_observer(void) {}
namespace xiao_nrf54l15 {
class I2sTx;
class I2sRx;
class I2sDuplex;
class Pwm;

static bool decodeSecureConnectionsPublicKeyToInternalLe(
    const uint8_t wirePublicKey[65], uint8_t outInternalLe[65],
    bool* outWireBigEndian, Secp256r1Point* outPoint);
static void buildSecureConnectionsWirePublicKeyFromInternalLe(
    const uint8_t internalLe[65], bool wireBigEndian, uint8_t outWireKey[65]);
}

namespace {
xiao_nrf54l15::Pwm* g_activePwm20 = nullptr;
xiao_nrf54l15::Pwm* g_activePwm21 = nullptr;
xiao_nrf54l15::Pwm* g_activePwm22 = nullptr;
xiao_nrf54l15::ZigbeeRadio* g_activeZigbeeRadioIrq = nullptr;
volatile uint8_t g_exclusiveRadioOwner =
    static_cast<uint8_t>(Nrf54ExclusiveRadioOwner::kNone);
volatile uint8_t g_exclusiveRadioQuarantined = 0U;
volatile uint32_t g_exclusiveRadioGeneration = 0U;
volatile uint32_t g_exclusiveRadioToken = 0U;

// LM20A RADIO EasyDMA pointer registers canonicalize a zero write to SRAM
// base. Accept only the documented zero and that observed cleared readback;
// the ENABLE and MAXCNT postconditions below remain independently required.
constexpr uint32_t kCanonicalClearedEasyDmaPointer = 0x20000000UL;

bool radioDmaPointerIsCleared(uint32_t pointer) {
  return pointer == 0U ||
         pointer == kCanonicalClearedEasyDmaPointer;
}

bool validExclusiveRadioOwner(Nrf54ExclusiveRadioOwner owner) {
  const uint8_t value = static_cast<uint8_t>(owner);
  return value >= static_cast<uint8_t>(Nrf54ExclusiveRadioOwner::kBle) &&
         value <=
             static_cast<uint8_t>(
                 Nrf54ExclusiveRadioOwner::kMpslChannelSounding);
}

bool scrubRadioDmaPointersIfDisabled(NRF_RADIO_Type* radio) {
  if (radio == nullptr) {
    return true;
  }
  const uint32_t state =
      (radio->STATE & RADIO_STATE_STATE_Msk) >> RADIO_STATE_STATE_Pos;
  if (state != RADIO_STATE_STATE_Disabled) {
    return false;
  }

  const bool auxDmaEnabled = radio->AUXDATADMA[0].ENABLE != 0U ||
                             radio->AUXDATADMA[1].ENABLE != 0U;
  if (auxDmaEnabled && radio->EVENTS_AUXDATADMAEND == 0U) {
    // AUX acquisition has its own EasyDMA completion event; RADIO Disabled is
    // not documented as proof that this transaction has stopped. Do not clear
    // owner-backed pointers until AUXDATADMAEND acknowledges STOP.
    radio->TASKS_AUXDATADMASTOP =
        RADIO_TASKS_AUXDATADMASTOP_TASKS_AUXDATADMASTOP_Trigger;
    uint32_t spins = 200000UL;
    while (radio->EVENTS_AUXDATADMAEND == 0U && spins-- != 0U) {
      __NOP();
    }
    if (radio->EVENTS_AUXDATADMAEND == 0U) {
      return false;
    }
  }
  __DSB();
  for (uint8_t index = 0U; index < 2U; ++index) {
    radio->AUXDATADMA[index].ENABLE = 0U;
    radio->AUXDATADMA[index].PTR = 0U;
    radio->AUXDATADMA[index].MAXCNT = 0U;
  }
  radio->DFEPACKET.PTR = 0U;
  radio->DFEPACKET.MAXCNT = 0U;
  radio->PACKETPTR = 0U;
  radio->EVENTS_AUXDATADMAEND = 0U;
  __DSB();

  return radioDmaPointerIsCleared(radio->PACKETPTR) &&
         radioDmaPointerIsCleared(radio->DFEPACKET.PTR) &&
         radio->DFEPACKET.MAXCNT == 0U &&
         radio->AUXDATADMA[0].ENABLE == 0U &&
         radioDmaPointerIsCleared(radio->AUXDATADMA[0].PTR) &&
         radio->AUXDATADMA[0].MAXCNT == 0U &&
         radio->AUXDATADMA[1].ENABLE == 0U &&
         radioDmaPointerIsCleared(radio->AUXDATADMA[1].PTR) &&
         radio->AUXDATADMA[1].MAXCNT == 0U;
}

xiao_nrf54l15::Pwm** pwmActiveSlotForBase(uint32_t base) {
  switch (base) {
    case nrf54l15::PWM20_BASE:
      return &g_activePwm20;
    case nrf54l15::PWM21_BASE:
      return &g_activePwm21;
    case nrf54l15::PWM22_BASE:
      return &g_activePwm22;
    default:
      return nullptr;
  }
}

int32_t pwmIrqNumberForBase(uint32_t base) {
  switch (base) {
    case nrf54l15::PWM20_BASE:
      return static_cast<int32_t>(PWM20_IRQn);
    case nrf54l15::PWM21_BASE:
      return static_cast<int32_t>(PWM21_IRQn);
    case nrf54l15::PWM22_BASE:
      return static_cast<int32_t>(PWM22_IRQn);
    default:
      return -1;
  }
}
}

uint32_t nrf54l15_acquire_exclusive_radio(Nrf54ExclusiveRadioOwner owner) {
  if (!validExclusiveRadioOwner(owner)) {
    return 0U;
  }
  const uint32_t primask = __get_PRIMASK();
  __disable_irq();
  uint32_t token = 0U;
  if (g_exclusiveRadioOwner ==
          static_cast<uint8_t>(Nrf54ExclusiveRadioOwner::kNone) &&
      g_exclusiveRadioQuarantined == 0U) {
    // Never recycle a generation token. An ancient stale owner must not
    // become valid again after UINT32 wrap; reset is the only recovery.
    if (g_exclusiveRadioGeneration == UINT32_MAX) {
      g_exclusiveRadioQuarantined = 1U;
    } else {
      ++g_exclusiveRadioGeneration;
      token = g_exclusiveRadioGeneration;
      g_exclusiveRadioToken = token;
      g_exclusiveRadioOwner = static_cast<uint8_t>(owner);
    }
  }
  __set_PRIMASK(primask);
  return token;
}

bool nrf54l15_release_exclusive_radio(Nrf54ExclusiveRadioOwner owner,
                                      uint32_t token) {
  if (owner == Nrf54ExclusiveRadioOwner::kNone || token == 0U) {
    return false;
  }
  const uint32_t primask = __get_PRIMASK();
  __disable_irq();
  const bool matches =
      g_exclusiveRadioQuarantined == 0U &&
      g_exclusiveRadioOwner == static_cast<uint8_t>(owner) &&
      g_exclusiveRadioToken == token;
  if (matches) {
    g_exclusiveRadioToken = 0U;
    g_exclusiveRadioOwner =
        static_cast<uint8_t>(Nrf54ExclusiveRadioOwner::kNone);
  }
  __set_PRIMASK(primask);
  return matches;
}

bool nrf54l15_quarantine_exclusive_radio(Nrf54ExclusiveRadioOwner owner,
                                         uint32_t token) {
  if (owner == Nrf54ExclusiveRadioOwner::kNone || token == 0U) {
    return false;
  }
  const uint32_t primask = __get_PRIMASK();
  __disable_irq();
  const bool matches =
      g_exclusiveRadioOwner == static_cast<uint8_t>(owner) &&
      g_exclusiveRadioToken == token;
  if (matches) {
    g_exclusiveRadioQuarantined = 1U;
  }
  __set_PRIMASK(primask);
  return matches;
}

bool nrf54l15_exclusive_radio_is_owned_by(Nrf54ExclusiveRadioOwner owner,
                                          uint32_t token) {
  const uint32_t primask = __get_PRIMASK();
  __disable_irq();
  const bool matches = token != 0U &&
                       g_exclusiveRadioOwner == static_cast<uint8_t>(owner) &&
                       g_exclusiveRadioToken == token;
  __set_PRIMASK(primask);
  return matches;
}

Nrf54ExclusiveRadioOwner nrf54l15_exclusive_radio_owner() {
  const uint32_t primask = __get_PRIMASK();
  __disable_irq();
  const Nrf54ExclusiveRadioOwner owner =
      static_cast<Nrf54ExclusiveRadioOwner>(g_exclusiveRadioOwner);
  __set_PRIMASK(primask);
  return owner;
}

bool nrf54l15_scrub_owned_radio_dma(Nrf54ExclusiveRadioOwner owner,
                                    uint32_t token, NRF_RADIO_Type* radio) {
  return nrf54l15_exclusive_radio_is_owned_by(owner, token) &&
         scrubRadioDmaPointersIfDisabled(radio);
}

[[noreturn]] void nrf54l15_exclusive_radio_fail_stop() {
  __disable_irq();
  NVIC_SystemReset();
  while (true) {
    __WFE();
  }
}


// This file is intentionally an ordered amalgamation of smaller HAL fragments.
// Keep fragments in this order unless the cross-fragment helper dependencies are also refactored.
#include "nrf54l15_hal_parts/nrf54l15_hal_internal_ble_timing.inc"
#include "nrf54l15_hal_parts/nrf54l15_hal_internal_gatt_bond.inc"
#include "nrf54l15_hal_parts/nrf54l15_hal_internal_crypto_service.inc"
#include "nrf54l15_hal_parts/nrf54l15_hal_ble_hooks.inc"
#include "nrf54l15_hal_parts/nrf54l15_hal_peripherals.inc"
#include "nrf54l15_hal_parts/nrf54l15_hal_crypto_analog.inc"
#include "nrf54l15_hal_parts/nrf54l15_hal_i2s.inc"
#include "nrf54l15_hal_parts/nrf54l15_hal_802154_rawradio.inc"
#include "nrf54l15_hal_parts/nrf54l15_hal_ble_core_setup.inc"
#include "nrf54l15_hal_parts/nrf54l15_hal_ble_custom_gatt.inc"
#include "nrf54l15_hal_parts/nrf54l15_hal_ble_advertising.inc"
#include "nrf54l15_hal_parts/nrf54l15_hal_ble_connection_api.inc"
#include "nrf54l15_hal_parts/nrf54l15_hal_ble_central_event.inc"
#include "nrf54l15_hal_parts/nrf54l15_hal_ble_peripheral_event_rx.inc"
#include "nrf54l15_hal_parts/nrf54l15_hal_ble_peripheral_event_tx.inc"
#include "nrf54l15_hal_parts/nrf54l15_hal_ble_peripheral_event_tail.inc"
#include "nrf54l15_hal_parts/nrf54l15_hal_ble_scanning_connections.inc"
#include "nrf54l15_hal_parts/nrf54l15_hal_ble_att_l2cap.inc"
#include "nrf54l15_hal_parts/nrf54l15_hal_ble_ll_security.inc"
#include "nrf54l15_hal_parts/nrf54l15_hal_ble_radio_tail.inc"

namespace {

constexpr uint32_t kBleSecp256r1BackgroundCooperateSpinLimit = 0UL;
constexpr uint32_t kBleSecp256r1ForegroundCooperateSpinLimit = 2500UL;

bool quiesceSharedRadioForSystemOff(uint32_t spinLimit) {
  NRF_RADIO_Type* const radio = NRF_RADIO;
  if (radio == nullptr) return true;

  // Prevent DPPI or local shortcuts from restarting the radio after DISABLE.
  radio->SUBSCRIBE_TXEN = 0U;
  radio->SUBSCRIBE_RXEN = 0U;
  radio->SUBSCRIBE_START = 0U;
  radio->SUBSCRIBE_STOP = 0U;
  radio->SUBSCRIBE_DISABLE = 0U;
  radio->SUBSCRIBE_RSSISTART = 0U;
  radio->SUBSCRIBE_BCSTART = 0U;
  radio->SUBSCRIBE_BCSTOP = 0U;
  radio->SUBSCRIBE_EDSTART = 0U;
  radio->SUBSCRIBE_EDSTOP = 0U;
  radio->SUBSCRIBE_CCASTART = 0U;
  radio->SUBSCRIBE_CCASTOP = 0U;
  radio->SUBSCRIBE_AUXDATADMASTART = 0U;
  radio->SUBSCRIBE_AUXDATADMASTOP = 0U;
  radio->SUBSCRIBE_PLLEN = 0U;
  radio->SUBSCRIBE_CSTONESSTART = 0U;
  radio->SUBSCRIBE_SOFTRESET = 0U;
  radio->SHORTS = 0U;
  radio->INTENCLR00 = 0xFFFFFFFFUL;
  radio->INTENCLR01 = 0xFFFFFFFFUL;
  radio->INTENCLR10 = 0xFFFFFFFFUL;
  radio->INTENCLR11 = 0xFFFFFFFFUL;
  radio->TASKS_AUXDATADMASTOP =
      RADIO_TASKS_AUXDATADMASTOP_TASKS_AUXDATADMASTOP_Trigger;

  const uint32_t state =
      (radio->STATE & RADIO_STATE_STATE_Msk) >> RADIO_STATE_STATE_Pos;
  bool disabled = (state == RADIO_STATE_STATE_Disabled);
  if (!disabled) {
    radio->EVENTS_DISABLED = 0U;
    radio->TASKS_DISABLE = RADIO_TASKS_DISABLE_TASKS_DISABLE_Trigger;
    // EVENTS_DISABLED is sticky. Only STATE proves that no EasyDMA transfer or
    // shortcut-triggered RADIO operation can still be using the pointers below.
    disabled = waitRadioStateDisabledBudgeted(radio, 0U, spinLimit);
  }
  if (!disabled) return false;

  if (!scrubRadioDmaPointersIfDisabled(radio)) return false;
  clearRadioCoreEvents(radio);
  return true;
}

#if !defined(NRF54L15_CLEAN_BLE_DISABLED) && \
    (!defined(NRF54L15_CLEAN_BLE_ENABLED) || (NRF54L15_CLEAN_BLE_ENABLED != 0))
void clearUnownedBleBackgroundGrtcCompares() {
  static constexpr uint8_t kChannels[] = {
      kBleBackgroundAdvPrewarmCompareChannel,
      kBleBackgroundAdvTxCompareChannel,
      kBleBackgroundConnPrewarmCompareChannel,
      kBleBackgroundAdvCleanupCompareChannel,
      kBleBackgroundAdvStage1ServiceCompareChannel,
      kBleBackgroundAdvStage1TxCompareChannel,
      kBleBackgroundAdvStage2ServiceCompareChannel,
      kBleBackgroundAdvStage2TxCompareChannel,
      kBleBackgroundAdvFinalCleanupCompareChannel,
      kBleBackgroundConnRxCompareChannel,
  };

  for (uint8_t i = 0U; i < sizeof(kChannels); ++i) {
    const uint8_t channel = kChannels[i];
    if (bleCompareEventPending(channel) || bleCompareEnabled(channel)) {
      bleDisableCompare(channel, true);
    }
  }

  // Only clear the idle wake channel if no wake is armed.
  if (g_bleIdleWakeArmed == 0U) {
    if (bleCompareEventPending(kBleBackgroundCompareChannel) ||
        bleCompareEnabled(kBleBackgroundCompareChannel)) {
      bleDisableCompare(kBleBackgroundCompareChannel, true);
    }
  }
}
#endif

}  // namespace

bool nrf54l15_quiesce_owned_radio(Nrf54ExclusiveRadioOwner owner,
                                  uint32_t token, uint32_t spinLimit) {
  return nrf54l15_exclusive_radio_is_owned_by(owner, token) &&
         quiesceSharedRadioForSystemOff(spinLimit);
}

extern "C" void nrf54l15_secp256r1_cooperate_hook(void) {
  if (g_activeBleRadio != nullptr) {
    ++g_activeBleRadio->smpSecureConnectionsCooperateHookCount_;
    g_activeBleRadio->serviceBackgroundConnection(
        g_activeBleRadio->isBackgroundConnectionServiceEnabled()
            ? kBleSecp256r1BackgroundCooperateSpinLimit
            : kBleSecp256r1ForegroundCooperateSpinLimit);
  }
  if (g_bleBackgroundRadio != nullptr &&
      g_bleBackgroundRadio != g_activeBleRadio) {
    ++g_bleBackgroundRadio->smpSecureConnectionsCooperateHookCount_;
    g_bleBackgroundRadio->serviceBackgroundConnection(
        g_bleBackgroundRadio->isBackgroundConnectionServiceEnabled()
            ? kBleSecp256r1BackgroundCooperateSpinLimit
            : kBleSecp256r1ForegroundCooperateSpinLimit);
  }
}

extern "C" bool nrf54_hal_quiesce_for_system_off(uint32_t spinLimit) {
  bool ok = true;

  const Nrf54ExclusiveRadioOwner owner = nrf54l15_exclusive_radio_owner();
  xiao_nrf54l15::BleRadio* const activeBle = g_activeBleRadio;
  xiao_nrf54l15::BleRadio* const backgroundBle = g_bleBackgroundRadio;
  if (owner == Nrf54ExclusiveRadioOwner::kBle) {
    if (activeBle != nullptr) {
      activeBle->end();
    }
    if (backgroundBle != nullptr && backgroundBle != activeBle) {
      backgroundBle->end();
    }
  } else if (owner == Nrf54ExclusiveRadioOwner::kZigbee802154 &&
             g_activeZigbeeRadioIrq != nullptr) {
    g_activeZigbeeRadioIrq->end();
  }
  if (nrf54l15_exclusive_radio_owner() !=
          Nrf54ExclusiveRadioOwner::kNone ||
      !quiesceSharedRadioForSystemOff(spinLimit)) {
    ok = false;
  }

  xiao_nrf54l15::Pwm* const pwms[] = {
      g_activePwm20, g_activePwm21, g_activePwm22};
  for (xiao_nrf54l15::Pwm* pwm : pwms) {
    if (pwm != nullptr) {
      pwm->enableInterruptMask(xiao_nrf54l15::Pwm::irqSupportedMask(), false);
      if (pwm->running() && !pwm->stop(spinLimit)) {
        ok = false;
      }
    }
  }

  if (g_activeI2sTx != nullptr && !g_activeI2sTx->quiesce(spinLimit)) {
    ok = false;
  }
  if (g_activeI2sRx != nullptr && !g_activeI2sRx->quiesce(spinLimit)) {
    ok = false;
  }
  if (g_activeI2sDuplex != nullptr &&
      !g_activeI2sDuplex->quiesce(spinLimit)) {
    ok = false;
  }
  return ok;
}

extern "C" void nrf54l15_ble_grtc_irq_service(void) {
#if !defined(NRF54L15_CLEAN_BLE_DISABLED) && \
    (!defined(NRF54L15_CLEAN_BLE_ENABLED) || (NRF54L15_CLEAN_BLE_ENABLED != 0))
  bleScanSleepWaitHandleTimeoutIrq();
  if (!bleBackgroundOwnerBlocksIdleWake() &&
      g_bleIdleWakeArmed != 0U &&
      bleBackgroundCompareEventPending()) {
    g_bleIdleWakePending = 1U;
    g_bleIdleWakeArmed = 0U;
    bleBackgroundDisableCompare();
  }
  // Foreground-only work for the currently active radio does not need the
  // shared background-radio owner slot. If both owner pointers refer to the
  // same radio and neither background advertising nor background connection
  // service is enabled anymore, release the background owner here before we
  // bounce back into background cleanup paths from the IRQ.
  if (g_bleBackgroundRadio != nullptr &&
      g_bleBackgroundRadio == g_activeBleRadio &&
      !g_bleBackgroundRadio->isBackgroundAdvertisingEnabled() &&
      !g_bleBackgroundRadio->isBackgroundConnectionServiceEnabled()) {
    g_bleBackgroundRadio = nullptr;
  }
  if (g_bleBackgroundRadio != nullptr) {
    g_bleBackgroundRadio->serviceBackgroundAdvertisingFromIrq();
    g_bleBackgroundRadio->serviceBackgroundConnectionFromIrq();
  } else {
    clearUnownedBleBackgroundGrtcCompares();
  }
#endif
  nrf54l15_grtc_irq_observer();
}

extern "C" void RADIO_0_IRQHandler(void) {
  const Nrf54ExclusiveRadioOwner owner = nrf54l15_exclusive_radio_owner();
  if (owner == Nrf54ExclusiveRadioOwner::kMpslChannelSounding) {
    if (nrf54_cs_controller_radio_irq_service != nullptr) {
      (void)nrf54_cs_controller_radio_irq_service();
    }
    return;
  }
  if (owner == Nrf54ExclusiveRadioOwner::kZigbee802154) {
    if (g_activeZigbeeRadioIrq != nullptr) {
      (void)g_activeZigbeeRadioIrq->serviceBufferedReceiveIrq();
    }
    return;
  }
  if (owner == Nrf54ExclusiveRadioOwner::kBle) {
    bleScanSleepWaitHandleRadioIrq(NRF_RADIO);
  }
}

extern "C" void nrf54l15_pwm20_irq_service(void) {
  if (g_activePwm20 != nullptr) {
    g_activePwm20->onIrq();
  }
}

extern "C" void nrf54l15_pwm21_irq_service(void) {
  if (g_activePwm21 != nullptr) {
    g_activePwm21->onIrq();
  }
}

extern "C" void nrf54l15_pwm22_irq_service(void) {
  if (g_activePwm22 != nullptr) {
    g_activePwm22->onIrq();
  }
}
