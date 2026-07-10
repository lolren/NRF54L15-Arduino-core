/*
 * System startup for nRF54LM20B.
 * PLL frequency is selected once during startup.
 * Applies trim, cache, and debug signal setup.
 */
#include <stdbool.h>
#include <stdint.h>
#include "cmsis.h"
#include "nrf54lm20b.h"

uint32_t SystemCoreClock = 64000000UL;
static uint32_t g_resetReasonAtBoot = 0U;

#if defined(NRF54L15_CLEAN_POWER_LOW)
void nrf54lm20b_core_bootstrap_low_power_timebase(void);
#endif

#if !defined(NRF_TRUSTZONE_NONSECURE)
static const NRF_FICR_Type *const kFicr = (const NRF_FICR_Type *)0x00FFC000UL;
static const uintptr_t kRramcLowPowerConfigReg = 0x5004E518UL;
static const uintptr_t kGlitchDetConfigReg  = 0x5004E5A0UL;
static const uintptr_t kCacheEnableReg      = 0xE0082404UL;
static const uintptr_t kDeviceConfigReg     = 0x50120440UL;
static const uintptr_t kTampcDbgenCtrlReg   = 0x500EF500UL;
static const uintptr_t kTampcNidenCtrlReg   = 0x500EF508UL;
static const uintptr_t kTampcSpidenCtrlReg  = 0x500EF510UL;
static const uintptr_t kTampcSpnidenCtrlReg = 0x500EF518UL;
static const uint32_t kTampcWriteKey =
    (TAMPC_PROTECT_DOMAIN_DBGEN_CTRL_KEY_KEY << TAMPC_PROTECT_DOMAIN_DBGEN_CTRL_KEY_Pos);
static const uint32_t kTampcWriteProtectionClear =
    (TAMPC_PROTECT_DOMAIN_DBGEN_CTRL_WRITEPROTECTION_Clear << TAMPC_PROTECT_DOMAIN_DBGEN_CTRL_WRITEPROTECTION_Pos);
static const uint32_t kTampcDebugSignalEnable =
    (TAMPC_PROTECT_DOMAIN_DBGEN_CTRL_VALUE_High << TAMPC_PROTECT_DOMAIN_DBGEN_CTRL_VALUE_Pos);
#endif

static inline volatile uint32_t *reg32(uintptr_t a) { return (volatile uint32_t *)a; }

#if !defined(NRF_TRUSTZONE_NONSECURE)
static void enableTampcDebugSignal(uintptr_t ctrlReg) {
    *reg32(ctrlReg) = kTampcWriteKey | kTampcWriteProtectionClear;
    *reg32(ctrlReg) = kTampcWriteKey | kTampcDebugSignalEnable;
}
#endif

static uint32_t currentPllFrequencyRaw(void) {
    return (NRF_OSCILLATORS->PLL.CURRENTFREQ & OSCILLATORS_PLL_CURRENTFREQ_CURRENTFREQ_Msk)
           >> OSCILLATORS_PLL_CURRENTFREQ_CURRENTFREQ_Pos;
}

static uint32_t cpuFrequencyHzFromRaw(uint32_t raw) {
    if (raw == OSCILLATORS_PLL_CURRENTFREQ_CURRENTFREQ_CK128M) return 128000000UL;
    if (raw == OSCILLATORS_PLL_CURRENTFREQ_CURRENTFREQ_CK64M)  return  64000000UL;
    return 0UL;
}

static void setPllFrequency(uint32_t target) {
    NRF_OSCILLATORS->PLL.FREQ = target;
    uint32_t guard = 0U;
    while ((((NRF_OSCILLATORS->PLL.CURRENTFREQ & OSCILLATORS_PLL_CURRENTFREQ_CURRENTFREQ_Msk)
             >> OSCILLATORS_PLL_CURRENTFREQ_CURRENTFREQ_Pos) != target) && (guard++ < 1000000UL))
        __NOP();
}

void SystemCoreClockUpdate(void) {
    uint32_t hz = cpuFrequencyHzFromRaw(currentPllFrequencyRaw());
    SystemCoreClock = (hz == 0UL) ? 64000000UL : hz;
}

#if !defined(NRF_TRUSTZONE_NONSECURE)
static void zephyrCopyTrimConfig(void) {
    for (uint32_t i = 0U; i < FICR_TRIMCNF_MaxCount; ++i) {
        uint32_t addr = kFicr->TRIMCNF[i].ADDR;
        if (addr == 0xFFFFFFFFUL || addr == 0x00000000UL) break;
        *reg32(addr) = kFicr->TRIMCNF[i].DATA;
    }
}

static void zephyrApplySystemInitParity(void) {
    zephyrCopyTrimConfig();

    /* Device configuration for ES silicon */
    if (*reg32(kDeviceConfigReg) == 0U)
        *reg32(kDeviceConfigReg) = 0xC8U;

    /* Allow RRAMC low power mode */
    *reg32(kRramcLowPowerConfigReg) = 3U;
    *reg32(kGlitchDetConfigReg) = 0U;

    /* Re-enable debug signals after reset */
    enableTampcDebugSignal(kTampcDbgenCtrlReg);
    enableTampcDebugSignal(kTampcNidenCtrlReg);
    enableTampcDebugSignal(kTampcSpidenCtrlReg);
    enableTampcDebugSignal(kTampcSpnidenCtrlReg);
}

static void zephyrApplyClockTrimParity(void) {
    const uint32_t xosc32ktrim = kFicr->XOSC32KTRIM;
    const uint32_t slopeFieldK = (xosc32ktrim & FICR_XOSC32KTRIM_SLOPE_Msk) >> FICR_XOSC32KTRIM_SLOPE_Pos;
    const uint32_t slopeMaskK = FICR_XOSC32KTRIM_SLOPE_Msk >> FICR_XOSC32KTRIM_SLOPE_Pos;
    const uint32_t slopeSignK = slopeMaskK - (slopeMaskK >> 1U);
    const int32_t slopeK = (int32_t)(slopeFieldK ^ slopeSignK) - (int32_t)slopeSignK;
    const uint32_t offsetK = (xosc32ktrim & FICR_XOSC32KTRIM_OFFSET_Msk) >> FICR_XOSC32KTRIM_OFFSET_Pos;
    const uint32_t lfxoIntcapFemtoF = 17000UL;
    const uint32_t lfxoMidValue = (2UL * lfxoIntcapFemtoF - 12000UL) * (uint32_t)(slopeK + 392) + ((offsetK << 3U) * 1000UL);
    uint32_t lfxoIntcap = lfxoMidValue / 512000UL;
    if ((lfxoMidValue % 512000UL) >= 256000UL) ++lfxoIntcap;
    NRF_OSCILLATORS->XOSC32KI.INTCAP = (lfxoIntcap << OSCILLATORS_XOSC32KI_INTCAP_VAL_Pos) & OSCILLATORS_XOSC32KI_INTCAP_VAL_Msk;

    const uint32_t xosc32mtrim = kFicr->XOSC32MTRIM;
    const uint32_t slopeFieldM = (xosc32mtrim & FICR_XOSC32MTRIM_SLOPE_Msk) >> FICR_XOSC32MTRIM_SLOPE_Pos;
    const uint32_t slopeMaskM = FICR_XOSC32MTRIM_SLOPE_Msk >> FICR_XOSC32MTRIM_SLOPE_Pos;
    const uint32_t slopeSignM = slopeMaskM - (slopeMaskM >> 1U);
    const int32_t slopeM = (int32_t)(slopeFieldM ^ slopeSignM) - (int32_t)slopeSignM;
    const uint32_t offsetM = (xosc32mtrim & FICR_XOSC32MTRIM_OFFSET_Msk) >> FICR_XOSC32MTRIM_OFFSET_Pos;
    const uint32_t hfxoIntcapFemtoF = 15000UL;
    const uint32_t hfxoMidValue = (((hfxoIntcapFemtoF - 5500UL) * (uint32_t)(slopeM + 791)) + ((offsetM << 2U) * 1000UL)) >> 8U;
    uint32_t hfxoIntcap = hfxoMidValue / 1000UL;
    if ((hfxoMidValue % 1000UL) >= 500UL) ++hfxoIntcap;
    NRF_OSCILLATORS->XOSC32M.CONFIG.INTCAP = (hfxoIntcap << OSCILLATORS_XOSC32M_CONFIG_INTCAP_VAL_Pos) & OSCILLATORS_XOSC32M_CONFIG_INTCAP_VAL_Msk;

    NRF_REGULATORS->VREGMAIN.DCDCEN = REGULATORS_VREGMAIN_DCDCEN_VAL_Enabled;
    *reg32(kCacheEnableReg) = 1U;
}
#endif

void SystemInit(void)
{
    /* RESETREAS is cumulative and W1C. Preserve every cause for the sketch;
     * System OFF entry clears it only after all shutdown prerequisites pass. */
    g_resetReasonAtBoot = NRF_RESET->RESETREAS;

#if defined(ARDUINO_NRF54_CPU_128M)
    setPllFrequency(OSCILLATORS_PLL_CURRENTFREQ_CURRENTFREQ_CK128M);
#else
    setPllFrequency(OSCILLATORS_PLL_CURRENTFREQ_CURRENTFREQ_CK64M);
#endif

#if !defined(NRF_TRUSTZONE_NONSECURE)
    /* Force green LED (P1.24) HIGH before debug signal init.
     * Debug signal enabling can cause the probe to drive P1.24 as SWO,
     * lighting the active-low LED. Pre-initialize it to OUTPUT HIGH. */
    NRF_P1->DIRSET = (1UL << 24);
    NRF_P1->OUTSET = (1UL << 24);

    zephyrApplySystemInitParity();
    zephyrApplyClockTrimParity();

    /* Re-assert P1.24 after debug signal enabling in case probe stole it */
    NRF_P1->DIRSET = (1UL << 24);
    NRF_P1->OUTSET = (1UL << 24);
    /* Also set all RGB pins through normal GPIO path */
    NRF_P1->DIRSET = (1UL << 22) | (1UL << 23);
    NRF_P1->OUTSET = (1UL << 22) | (1UL << 23) | (1UL << 24);
#endif
    SystemCoreClockUpdate();

#if defined(NRF54L15_CLEAN_POWER_LOW)
    /* Restore reset GRTC registers before C++ constructors run. */
    if ((g_resetReasonAtBoot &
         (RESET_RESETREAS_OFF_Msk | RESET_RESETREAS_GRTC_Msk)) != 0U) {
        nrf54lm20b_core_bootstrap_low_power_timebase();
    }
#endif
}

bool nrf54lm20b_core_set_cpu_frequency_hz(uint32_t hz) {
    SystemCoreClockUpdate();
    return hz == SystemCoreClock;
}

uint32_t nrf54lm20b_core_get_cpu_frequency_hz(void) {
    SystemCoreClockUpdate();
    return SystemCoreClock;
}

bool nrf54lm20b_core_set_idle_cpu_scaling_hz(uint32_t hz, bool enable) {
    (void)hz;
    return !enable;
}

bool nrf54lm20b_core_get_idle_cpu_scaling_enabled(void) { return false; }
uint32_t nrf54lm20b_core_get_idle_cpu_frequency_hz(void) {
    return nrf54lm20b_core_get_cpu_frequency_hz();
}

uint32_t nrf54lm20b_core_enter_idle_cpu_scaling(void) {
    return 0UL;
}

void nrf54lm20b_core_exit_idle_cpu_scaling(uint32_t prev) {
    (void)prev;
}

/* nrf54l15_core_* aliases for library compatibility (Arduino.h declares nrf54l15_core_*) */
__attribute__((alias("nrf54lm20b_core_set_cpu_frequency_hz")))
bool nrf54l15_core_set_cpu_frequency_hz(uint32_t);

__attribute__((alias("nrf54lm20b_core_get_cpu_frequency_hz")))
uint32_t nrf54l15_core_get_cpu_frequency_hz(void);

__attribute__((alias("nrf54lm20b_core_set_idle_cpu_scaling_hz")))
bool nrf54l15_core_set_idle_cpu_scaling_hz(uint32_t, bool);

__attribute__((alias("nrf54lm20b_core_get_idle_cpu_scaling_enabled")))
bool nrf54l15_core_get_idle_cpu_scaling_enabled(void);

__attribute__((alias("nrf54lm20b_core_get_idle_cpu_frequency_hz")))
uint32_t nrf54l15_core_get_idle_cpu_frequency_hz(void);

__attribute__((alias("nrf54lm20b_core_enter_idle_cpu_scaling")))
uint32_t nrf54l15_core_enter_idle_cpu_scaling(void);

__attribute__((alias("nrf54lm20b_core_exit_idle_cpu_scaling")))
void nrf54l15_core_exit_idle_cpu_scaling(uint32_t);

uint32_t nrf54_core_reset_reason(void) {
    return g_resetReasonAtBoot;
}

void nrf54_core_clear_reset_reason(uint32_t mask) {
    NRF_RESET->RESETREAS = mask;
    g_resetReasonAtBoot &= ~mask;
}
