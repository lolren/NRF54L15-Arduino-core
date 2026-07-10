#include "nrf54l15_hal.h"
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
    disabled = waitRadioDisabledBudgeted(radio, 0U, spinLimit);
  }
  if (!disabled) return false;

  radio->AUXDATADMA[0].ENABLE = 0U;
  radio->AUXDATADMA[0].PTR = 0U;
  radio->AUXDATADMA[0].MAXCNT = 0U;
  radio->AUXDATADMA[1].ENABLE = 0U;
  radio->AUXDATADMA[1].PTR = 0U;
  radio->AUXDATADMA[1].MAXCNT = 0U;
  radio->PACKETPTR = 0U;
  clearRadioCoreEvents(radio);
  return true;
}

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

}  // namespace

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

  xiao_nrf54l15::BleRadio* const activeBle = g_activeBleRadio;
  xiao_nrf54l15::BleRadio* const backgroundBle = g_bleBackgroundRadio;
  if (activeBle != nullptr) {
    activeBle->end();
  }
  if (backgroundBle != nullptr && backgroundBle != activeBle) {
    backgroundBle->end();
  }
  if (g_activeZigbeeRadioIrq != nullptr) {
    g_activeZigbeeRadioIrq->end();
  }
  if (!quiesceSharedRadioForSystemOff(spinLimit)) {
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
  if (g_activeZigbeeRadioIrq != nullptr &&
      g_activeZigbeeRadioIrq->serviceBufferedReceiveIrq()) {
    return;
  }
  bleScanSleepWaitHandleRadioIrq(NRF_RADIO);
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
