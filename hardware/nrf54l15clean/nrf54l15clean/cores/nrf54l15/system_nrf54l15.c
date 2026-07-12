/*
 * Copyright (c) 2009-2026 ARM Limited. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 *
 * NOTICE: The upstream system template was modified by Nordic Semiconductor
 * ASA. Modified again for the nRF54 Arduino Core in 2026: the nrfx startup,
 * trim, errata, oscillator, cache, and SystemCoreClock behavior was adapted to
 * this core's secure/non-secure and board-specific startup paths.
 *
 * Portions derived from the nrfx oscillator HAL:
 * Copyright (c) 2019-2026 Nordic Semiconductor ASA. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * Apache-2.0 terms are in ../../LICENSES/Apache-2.0.txt.
 *
 * System startup for the nRF54L15 clean Arduino core.
 *
 * For secure builds, this mirrors the Zephyr/nrfx startup writes that were
 * required to reach the same low-power SYSTEM OFF behavior on XIAO nRF54L15.
 */

#include <stdbool.h>
#include <stdint.h>

#include "cmsis.h"
#include "nrf54l15.h"

uint32_t SystemCoreClock = 64000000UL;
static uint32_t g_resetReasonAtBoot = 0U;

#if defined(NRF54L15_CLEAN_POWER_LOW)
void nrf54l15_core_bootstrap_low_power_timebase(void);
#endif

#if !defined(NRF_TRUSTZONE_NONSECURE)
static const NRF_FICR_Type *const kFicr =
    (const NRF_FICR_Type *)0x00FFC000UL;
static const uintptr_t kErrata37Reg = 0x5005340CUL;
static const uintptr_t kDeviceConfigReg = 0x50120440UL;
static const uintptr_t kErrata32CheckReg = 0x00FFC334UL;
static const uintptr_t kErrata32Reg = 0x50120640UL;
static const uintptr_t kErrata31Reg0 = 0x50120624UL;
static const uintptr_t kErrata31Reg1 = 0x5012063CUL;
static const uintptr_t kErrata40Reg = 0x5008A7ACUL;
static const uintptr_t kRramcLowPowerConfigReg = 0x5004B518UL;
static const uintptr_t kGlitchDetConfigReg = 0x5004B5A0UL;
static const uintptr_t kCacheEnableReg = 0xE0082404UL;
/* TAMPC (Tamper Controller) base: 0x500DC000 (secure).
 * PROTECT is at +0x500, DOMAIN[0] at +0x000. Each protected debug signal defaults to
 * VALUE=0 with write protection enabled after a power cycle and must be updated through
 * the documented two-stage write sequence: clear write protection, then write VALUE. */
static const uintptr_t kTampcDbgenCtrlReg = 0x500DC500UL;
static const uintptr_t kTampcNidenCtrlReg = 0x500DC508UL;
static const uintptr_t kTampcSpidenCtrlReg = 0x500DC510UL;
static const uintptr_t kTampcSpnidenCtrlReg = 0x500DC518UL;
static const uint32_t kTampcWriteKey =
    (TAMPC_PROTECT_DOMAIN_DBGEN_CTRL_KEY_KEY
     << TAMPC_PROTECT_DOMAIN_DBGEN_CTRL_KEY_Pos);
static const uint32_t kTampcWriteProtectionClear =
    (TAMPC_PROTECT_DOMAIN_DBGEN_CTRL_WRITEPROTECTION_Clear
     << TAMPC_PROTECT_DOMAIN_DBGEN_CTRL_WRITEPROTECTION_Pos);
static const uint32_t kTampcDebugSignalEnable =
    (TAMPC_PROTECT_DOMAIN_DBGEN_CTRL_VALUE_High
     << TAMPC_PROTECT_DOMAIN_DBGEN_CTRL_VALUE_Pos);
#endif

static inline volatile uint32_t *reg32(uintptr_t address)
{
    return (volatile uint32_t *)address;
}

#if !defined(NRF_TRUSTZONE_NONSECURE)
static void enableTampcDebugSignal(uintptr_t ctrlReg)
{
    *reg32(ctrlReg) = kTampcWriteKey | kTampcWriteProtectionClear;
    *reg32(ctrlReg) = kTampcWriteKey | kTampcDebugSignalEnable;
}
#endif

static uint32_t currentPllFrequencyRaw(void)
{
    return (NRF_OSCILLATORS->PLL.CURRENTFREQ &
            OSCILLATORS_PLL_CURRENTFREQ_CURRENTFREQ_Msk) >>
           OSCILLATORS_PLL_CURRENTFREQ_CURRENTFREQ_Pos;
}

static uint32_t cpuFrequencyHzFromRaw(uint32_t raw)
{
    if (raw == OSCILLATORS_PLL_CURRENTFREQ_CURRENTFREQ_CK128M) {
        return 128000000UL;
    }
    if (raw == OSCILLATORS_PLL_CURRENTFREQ_CURRENTFREQ_CK64M) {
        return 64000000UL;
    }
    return 0UL;
}

static void setPllFrequency(uint32_t targetFrequency)
{
    NRF_OSCILLATORS->PLL.FREQ = targetFrequency;

    uint32_t guard = 0U;
    while ((((NRF_OSCILLATORS->PLL.CURRENTFREQ &
              OSCILLATORS_PLL_CURRENTFREQ_CURRENTFREQ_Msk) >>
             OSCILLATORS_PLL_CURRENTFREQ_CURRENTFREQ_Pos) != targetFrequency) &&
           (guard++ < 1000000UL)) {
        __NOP();
    }
}

void SystemCoreClockUpdate(void)
{
    const uint32_t hz = cpuFrequencyHzFromRaw(currentPllFrequencyRaw());
    SystemCoreClock = (hz == 0UL) ? 64000000UL : hz;
}

#if !defined(NRF_TRUSTZONE_NONSECURE)
static bool zephyrErrata31(void)
{
    return (kFicr->INFO.PART == 0x1CU) && (kFicr->INFO.VARIANT == 0x01U);
}

static bool zephyrErrata32(void)
{
    return (kFicr->INFO.PART == 0x1CU) && (kFicr->INFO.VARIANT == 0x01U);
}

static bool zephyrErrata37(void)
{
    return kFicr->INFO.PART == 0x1CU;
}

static bool zephyrErrata40(void)
{
    return (kFicr->INFO.PART == 0x1CU) && (kFicr->INFO.VARIANT == 0x01U);
}

static void zephyrCopyTrimConfig(void)
{
    for (uint32_t index = 0U; index < FICR_TRIMCNF_MaxCount; ++index) {
        const uint32_t address = kFicr->TRIMCNF[index].ADDR;
        if ((address == 0xFFFFFFFFUL) || (address == 0x00000000UL)) {
            break;
        }

        *reg32(address) = kFicr->TRIMCNF[index].DATA;
    }
}

static void zephyrApplySystemInitParity(void)
{
    if (zephyrErrata37()) {
        *reg32(kErrata37Reg) = 1U;
    }

    zephyrCopyTrimConfig();

    if (*reg32(kDeviceConfigReg) == 0U) {
        *reg32(kDeviceConfigReg) = 0xC8U;
    }

    if (zephyrErrata32() && (*reg32(kErrata32CheckReg) <= 0x180A1D00UL)) {
        *reg32(kErrata32Reg) = 0x1EA9E040UL;
    }

    if (zephyrErrata40()) {
        *reg32(kErrata40Reg) = 0x040A0078UL;
    }

    if (zephyrErrata31()) {
        *reg32(kErrata31Reg0) = 20U | (1U << 5);
        *reg32(kErrata31Reg1) &= ~(1UL << 19);
    }

    *reg32(kRramcLowPowerConfigReg) = 3U;
    *reg32(kGlitchDetConfigReg) = 0U;

    /* UICR.APPROTECT and UICR.SECUREAPPROTECT default to unprotected on erased parts,
     * which leaves the TAMPC debug signals under CPU control. Re-enable both non-secure
     * and secure debug signals after reset so a power cycle does not strand the board
     * behind disabled debug access. */
    enableTampcDebugSignal(kTampcDbgenCtrlReg);
    enableTampcDebugSignal(kTampcNidenCtrlReg);
    enableTampcDebugSignal(kTampcSpidenCtrlReg);
    enableTampcDebugSignal(kTampcSpnidenCtrlReg);
}

static void zephyrApplyClockTrimParity(void)
{
    const uint32_t xosc32ktrim = kFicr->XOSC32KTRIM;
    const uint32_t slopeFieldK =
        (xosc32ktrim & FICR_XOSC32KTRIM_SLOPE_Msk) >> FICR_XOSC32KTRIM_SLOPE_Pos;
    const uint32_t slopeMaskK =
        FICR_XOSC32KTRIM_SLOPE_Msk >> FICR_XOSC32KTRIM_SLOPE_Pos;
    const uint32_t slopeSignK = slopeMaskK - (slopeMaskK >> 1U);
    const int32_t slopeK =
        (int32_t)(slopeFieldK ^ slopeSignK) - (int32_t)slopeSignK;
    const uint32_t offsetK =
        (xosc32ktrim & FICR_XOSC32KTRIM_OFFSET_Msk) >> FICR_XOSC32KTRIM_OFFSET_Pos;
    const uint32_t lfxoIntcapFemtoF = 16000UL;
    const uint32_t lfxoMidValue =
        (2UL * lfxoIntcapFemtoF - 12000UL) *
            (uint32_t)(slopeK + 392) +
        ((offsetK << 3U) * 1000UL);
    uint32_t lfxoIntcap = lfxoMidValue / 512000UL;
    if ((lfxoMidValue % 512000UL) >= 256000UL) {
        ++lfxoIntcap;
    }
    NRF_OSCILLATORS->XOSC32KI.INTCAP =
        (lfxoIntcap << OSCILLATORS_XOSC32KI_INTCAP_VAL_Pos) &
        OSCILLATORS_XOSC32KI_INTCAP_VAL_Msk;

    const uint32_t xosc32mtrim = kFicr->XOSC32MTRIM;
    const uint32_t slopeFieldM =
        (xosc32mtrim & FICR_XOSC32MTRIM_SLOPE_Msk) >> FICR_XOSC32MTRIM_SLOPE_Pos;
    const uint32_t slopeMaskM =
        FICR_XOSC32MTRIM_SLOPE_Msk >> FICR_XOSC32MTRIM_SLOPE_Pos;
    const uint32_t slopeSignM = slopeMaskM - (slopeMaskM >> 1U);
    const int32_t slopeM =
        (int32_t)(slopeFieldM ^ slopeSignM) - (int32_t)slopeSignM;
    const uint32_t offsetM =
        (xosc32mtrim & FICR_XOSC32MTRIM_OFFSET_Msk) >> FICR_XOSC32MTRIM_OFFSET_Pos;
    const uint32_t hfxoIntcapFemtoF = 16000UL;
    const uint32_t hfxoMidValue =
        (((hfxoIntcapFemtoF - 5500UL) * (uint32_t)(slopeM + 791)) +
         ((offsetM << 2U) * 1000UL)) >>
        8U;
    uint32_t hfxoIntcap = hfxoMidValue / 1000UL;
    if ((hfxoMidValue % 1000UL) >= 500UL) {
        ++hfxoIntcap;
    }
    NRF_OSCILLATORS->XOSC32M.CONFIG.INTCAP =
        (hfxoIntcap << OSCILLATORS_XOSC32M_CONFIG_INTCAP_VAL_Pos) &
        OSCILLATORS_XOSC32M_CONFIG_INTCAP_VAL_Msk;

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
    zephyrApplySystemInitParity();
    zephyrApplyClockTrimParity();
#endif

    SystemCoreClockUpdate();

#if defined(NRF54L15_CLEAN_POWER_LOW)
    /* SystemInit runs before C++ constructors. Restore reset GRTC registers in
     * the WAKETIME window so constructors cannot observe a stale timebase. */
    if ((g_resetReasonAtBoot &
         (RESET_RESETREAS_OFF_Msk | RESET_RESETREAS_GRTC_Msk)) != 0U) {
        nrf54l15_core_bootstrap_low_power_timebase();
    }
#endif
}

bool nrf54l15_core_set_cpu_frequency_hz(uint32_t hz)
{
    SystemCoreClockUpdate();
    /* PLL selection is a startup-only operation on nRF54L. */
    return hz == SystemCoreClock;
}

uint32_t nrf54l15_core_get_cpu_frequency_hz(void)
{
    SystemCoreClockUpdate();
    return SystemCoreClock;
}

bool nrf54l15_core_set_idle_cpu_scaling_hz(uint32_t hz, bool enable)
{
    (void)hz;
    if (!enable) {
        return true;
    }
    return false;
}

bool nrf54l15_core_get_idle_cpu_scaling_enabled(void)
{
    return false;
}

uint32_t nrf54l15_core_get_idle_cpu_frequency_hz(void)
{
    return nrf54l15_core_get_cpu_frequency_hz();
}

uint32_t nrf54l15_core_enter_idle_cpu_scaling(void)
{
    return 0UL;
}

void nrf54l15_core_exit_idle_cpu_scaling(uint32_t previousRaw)
{
    (void)previousRaw;
}

uint32_t nrf54_core_reset_reason(void)
{
    return g_resetReasonAtBoot;
}

void nrf54_core_clear_reset_reason(uint32_t mask)
{
    NRF_RESET->RESETREAS = mask;
    g_resetReasonAtBoot &= ~mask;
}
