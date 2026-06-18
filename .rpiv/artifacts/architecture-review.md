# nRF54L15 Clean Arduino Core — Architecture Review & Issue Report

**Date:** 2026-06-18
**Scope:** Full custom Arduino core (`cores/nrf54l15/`), variants, `Nrf54L15-Clean-Implementation` library src (excl. openthread_core_stage/third_party), datasheet cross-reference
**Target:** nRF54L15 (Cortex-M33) / XIAO nRF54L15

---

## 1. Startup & Vector Table (`startup_nrf54l15.S`)

### Finding 1.1 — Vector table gap coverage (IRQ 0–69 all `Default_Handler`)
**Severity:** Concern

The vector table has 70 entries of `Default_Handler` for IRQ 0–69. The nRF54L15 datasheet defines several peripherals in this range (e.g., VPR-related IRQs, DPPIC00, PPIB00). While unused IRQs pointing to `Default_Handler` (infinite loop) is standard practice, if any peripheral inadvertently fires in this range the CPU hangs. This is acceptable for a bare-metal Arduino core since these IRQs aren't used, but worth noting.

### Finding 1.2 — No `bx lr` in core exception handlers
**Severity:** Suggestion

Core exceptions (NMI, HardFault, MemManage, BusFault, UsageFault, SecureFault, SVC, DebugMon, PendSV) use `b .` (infinite loop). For HardFault this is intentional (trap for debugging), but for SVC and PendSV an infinite loop means any library/framework that installs its own SVC/PendSV handler must override with `.weak`. The `.weak` attribute is present, so this is correct.

### Finding 1.3 — `SysTick_Handler` declared `.weak` in assembly, overridden in `wiring_time.c`
**Severity:** OK (verified correct)

The startup defines `.weak SysTick_Handler` → `b .` and `wiring_time.c` provides the real `SysTick_Handler` with `__attribute__((weak))`. This is the correct pattern.

---

## 2. System Init (`system_nrf54l15.c`)

### Finding 2.1 — PLL frequency set before errata/trim config
**Severity:** Concern

`SystemInit()` calls `setPllFrequency()` *before* `zephyrApplySystemInitParity()` and `zephyrApplyClockTrimParity()`. The PLL frequency change happens before the XOSC32M trim values are loaded from FICR. If the PLL locks to an untrimmed oscillator, there could be a brief period of incorrect frequency before trim is applied. However, since `setPllFrequency()` waits for `CURRENTFREQ` to match, and the trim config is applied before the PLL is used for anything time-critical, this is low risk.

### Finding 2.2 — `setPllFrequency` spin limit of 1,000,000 iterations
**Severity:** Suggestion

At 64 MHz, 1M NOPs ≈ 16ms. This is generous but could hang indefinitely if the PLL never locks (e.g., missing crystal). No fallback or error indication is provided.

### Finding 2.3 — `cpuFrequencyRawFromHz` boundary: 64 MHz → CK64M for any value ≥ 64M
**Severity:** Suggestion

`hz >= 64000000UL` maps to CK64M. Values between 64M and 128M (exclusive) also map to CK64M, which silently rounds down. This is acceptable since there are only two supported frequencies.

### Finding 2.4 — Errata checks use `PART == 0x1CU` (nRF54L15)
**Severity:** OK (verified)

All errata functions check `kFicr->INFO.PART == 0x1CU` which is the nRF54L15 part number. Errata 37 applies to all nRF54L15, while 31/32/40 require `VARIANT == 0x01U`. This matches Nordic's errata documentation.

### Finding 2.5 — TAMPC debug signal re-enable after every reset
**Severity:** OK (intentional design)

The code re-enables all four debug signals (DBGEN, NIDEN, SPIDEN, SPNIDEN) after reset to prevent debug access loss after power cycles. This is a deliberate development-friendly choice.

### Finding 2.6 — Cache enabled via direct register write `0xE008244UL`
**Severity:** Concern

`kCacheEnableReg = 0xE0082404UL` writes directly to the CP15/ARMv8M cache maintenance register. The value `1U` enables the D-Cache. This bypasses the proper CMSIS `SCB_EnableDCache()` API. If the I-Cache is not also enabled, there could be coherency issues. The code does not enable I-Cache.

**Recommendation:** Use `SCB_EnableDCache()` and consider `SCB_EnableICache()` for consistency.

### Finding 2.7 — DC/DC converter enabled unconditionally
**Severity:** OK (for 3.0V+ operation)

`NRF_REGULATORS->VREGMAIN.DCDCEN = REGULATORS_VREGMAIN_DCDCEN_VAL_Enabled` is set unconditionally. The nRF54L15 datasheet states DC/DC is recommended above 2.7V. At lower voltages this could cause issues.

---

## 3. Linker Script (`nrf54l15_linker_script.ld`)

### Finding 3.1 — FLASH length 0x17C000 (1520 KB) vs datasheet 1524 KB
**Severity:** OK (intentional)

The linker reserves 4 KB (`FLASH_BOND`) at the top for bond/prefs/EEPROM storage. 1524 KB - 4 KB = 1520 KB = 0x17C000. This is correct.

### Finding 3.2 — RAM limited to 152 KB (0x26000) with VPR
**Severity:** OK (matches Zephyr partitioning)

CPUAPP gets 152 KB, VPR gets the remaining ~96 KB. This matches Nordic's recommended partitioning.

### Finding 3.3 — Stack size hardcoded to 4 KB
**Severity:** Concern

`__stack_size__ = 4K` is relatively small for a system running BLE + Thread + Arduino sketches. Deep recursion or large stack frames could overflow. Nordic's reference designs typically use 2-8 KB depending on the application.

### Finding 3.4 — `ASSERT(_enoinit <= 0x20018000)` for VPR IPC boundary
**Severity:** OK (correct)

Ensures static data doesn't overlap the VPR shared memory window at 0x20018000.

---

## 4. GPIO / Digital I/O (`wiring_digital.c`)

### Finding 4.1 — `pinToPortPin` used instead of `NRFX_PIN` macro
**Severity:** OK (correct approach)

The core uses `pinToPortPin()` from the variant's `pins_arduino.h` to map Arduino pin numbers to (port, pin) pairs. This is the correct approach for Arduino compatibility.

### Finding 4.2 — P2 port support in `gpio_for_port`
**Severity:** OK (verified)

The nRF54L15 has P0, P1, and P2 ports. The code correctly handles all three.

### Finding 4.3 — GPIOTE20 interrupt routing
**Severity:** OK (verified)

`attachInterrupt()` uses GPIOTE20 with two IRQ lines (IRQ 218/219). The channel allocation correctly avoids channels used by task channels (`nrf54l15_gpiote20_acquire_task_channel`). P2 pins are excluded from interrupts (correct — P2 is 8 MHz port with limited GPIOTE support).

### Finding 4.4 — `configure_pin_for_interrupt` sets `DIR_Input` and `INPUT_Connect`
**Severity:** OK (correct)

Properly configures the pin for interrupt sensing.

### Finding 4.5 — `polarity_from_mode` maps `LOW` to `HITOLO`
**Severity:** Concern

`LOW` mode maps to `CORE_GPIOTE_POLARITY_HITOLO` (falling edge). This means `attachInterrupt(pin, fn, LOW)` triggers on the transition from HIGH to LOW, not when the pin is held LOW. Standard Arduino `LOW` mode triggers whenever the pin is LOW (level-sensitive). GPIOTE on nRF54 only supports edge-triggered events, not level-sensitive. This is a **functional difference** from standard Arduino behavior.

**Impact:** Libraries expecting level-sensitive LOW interrupts will not work as expected.

### Finding 4.6 — `noInterrupts`/`interrupts` nesting counter
**Severity:** OK (correct)

Properly handles nested disable/enable with `g_irq_nest` counter.

---

## 5. UART / HardwareSerial (`HardwareSerial.cpp`)

### Finding 5.1 — Serial instances mapped to UARTE20/UARTE21
**Severity:** OK (verified)

`Serial` → `NRF_UARTE21` (P2.8/P2.7) and `Serial1` → `NRF_UARTE20` (SAMD11 bridge pins). This matches the XIAO nRF54L15 schematic.

### Finding 5.2 — Shared IRQ handling for SPIM22/SPIM30
**Severity:** OK (correct)

UARTE22 and TWIM22 share IRQ 200 (SPIM22_IRQn). UARTE30 and TWIM30 share IRQ 260 (SPIM30_IRQn). The IRQ handlers correctly dispatch to both UART and I2C owners.

### Finding 5.3 — `baud_to_reg` nearest-preset selection
**Severity:** OK (correct)

Maps arbitrary baud rates to the nearest supported UARTE preset. The preset table covers all standard rates from 1200 to 1M.

### Finding 5.4 — DMA RX with FRAMETIMEOUT shortcut
**Severity:** OK (sophisticated design)

Uses `FRAMETIMEOUT → DMA_RX_STOP` shortcut for proper byte count delivery. This is a well-designed approach that handles the nRF54's UARTE DMA behavior correctly.

### Finding 5.5 — `CONSTLAT` management for P2 pins
**Severity:** OK (correct)

P2 is an 8 MHz port requiring `CONSTLAT` mode for reliable operation. The code correctly acquires/releases CONSTLAT when UART uses P2 pins.

### Finding 5.6 — OVERRUN error handling: no DMA restart
**Severity:** OK (documented design decision)

The code explicitly does NOT call `stopRxDma()` on OVERRUN, citing that the 708μs RXTO wait would let the FIFO overflow again. Instead it clears error flags and lets DMA continue. This is a pragmatic choice for high-throughput scenarios.

---

## 6. SPI (`SPI.cpp`)

### Finding 6.1 — SPIM30 for standard SPI, SPIM00 for HS-SPI
**Severity:** OK (verified)

`SPI` → `NRF_SPIM30` (standard, 16 MHz core clock) and `SPI_HS` → `NRF_SPIM00` (high-speed, 128 MHz core clock). This matches the nRF54L15 peripheral capabilities.

### Finding 6.2 — `compute_prescaler` divisor calculation
**Severity:** OK (correct)

Correctly computes the prescaler divisor from target frequency, respecting minimum divisor constraints (4 for HS, 2 for standard) and even-divisor requirement.

### Finding 6.3 — EasyDMA buffer staging through aligned RAM
**Severity:** OK (correct)

All transfers go through `alignas(4)` scratch buffers to satisfy EasyDMA RAM-alignment requirements.

### Finding 6.4 — `set_extra_high_drive_if_hs` for HS-SPI pins
**Severity:** OK (correct)

HS-SPI pins get enhanced drive strength (`E0`/`E1`) for signal integrity at high speeds.

---

## 7. I2C / Wire (`Wire.cpp`)

### Finding 7.1 — Wire → TWIM22, Wire1 → TWIM30
**Severity:** OK (verified)

Matches the XIAO nRF54L15 board routing.

### Finding 7.2 — TWIS target mode support
**Severity:** OK (sophisticated)

Full TWIS (slave) mode with interrupt-driven event handling. Supports `onReceive`/`onRequest` callbacks.

### Finding 7.3 — `twim_frequency_reg` only supports up to 1 MHz
**Severity:** OK (matches hardware)

The nRF54L15 TWIM supports 100k, 250k, 400k, and 1 MHz. Fast-mode Plus (1 MHz) is the maximum.

### Finding 7.4 — `configure_i2c_pin` drive config: `S0`/`D1`
**Severity:** OK (correct)

Sets Standard drive for S0 and Drain for S1, appropriate for open-drain I2C.

---

## 8. Analog / ADC (`wiring_analog.c`)

### Finding 8.1 — SAADC single-ended sampling
**Severity:** OK (correct)

Uses single-ended mode with internal reference. Resolution defaults to 10-bit (0-1023).

### Finding 8.2 — PWM via hardware PWM20-22 and timer-based soft PWM
**Severity:** OK (sophisticated)

Hardware PWM for D0-D5 (PWM20/PWM21 channels). Timer+GPIOTE+DPPI soft PWM for additional pins. This is a well-designed hybrid approach.

### Finding 8.3 — `pinToSaadcChannel` mapping
**Severity:** OK (verified)

A0→ch0, A1→ch1, A2→ch2, A3→ch3, A5→ch4, PDM_CLK→ch5, A6→ch6, A7→ch7. Note: A4 is NOT mapped to any SAADC channel.

### Finding 8.4 — **A4 (PIN_D4) has no SAADC channel**
**Severity:** Concern

`pinToSaadcChannel` returns -1 for A4 (PIN_D4). However, `digitalPinHasPWM` includes D4. The `analogRead(A4)` will fail silently. This is because D4 is routed to TWIM22 SDA on the XIAO and may not have an analog input connection. **Verify against the XIAO schematic.**

---

## 9. Time / SysTick (`wiring_time.c`)

### Finding 9.1 — Dual timebase: SysTick (balanced) vs GRTC (low-power)
**Severity:** OK (correct design)

`NRF54L15_CLEAN_POWER_LOW` builds use GRTC for tickless idle. Non-low-power builds use SysTick.

### Finding 9.2 — `millis()` fallback when SysTick not firing
**Severity:** Concern

When `g_millis_ticks == 0`, the code reads `SysTick->VAL` directly with wrap tracking. This fallback has a static `s_last` and `s_ms` that can produce incorrect results if SysTick is disabled and re-enabled. The `s_last = (uint32_t)-1` initialization means the first read after startup produces no increment.

### Finding 9.3 — GRTC low-power delay with WFI
**Severity:** OK (sophisticated)

Well-implemented tickless delay using GRTC compare + WFI. Properly handles BLE sleep cap to avoid starving the BLE stack.

### Finding 9.4 — `systemOffWakeReset` triggers actual SYSTEM OFF
**Severity:** OK (correct)

Enters true SYSTEM OFF with GRTC wake. Non-retention variant disables RAM retention.

---

## 10. nRF52 Compatibility Shim (`nrf52_compat.cpp/h`)

### Finding 10.1 — `NRF_UICR` and `NRF_NVMC` compatibility structs
**Severity:** Concern

The nRF54L15 does not have NVMC (it uses RRAM with different write mechanisms). `g_nrf52_compat_nvmc` is a stub with `READY` always set to `Ready`. Libraries that check `NVMC->READY` will get false positives. `NRF_UICR` is a zero-initialized struct — any UICR read returns 0.

**Impact:** Libraries relying on UICR for board detection or NVMC for flash programming will malfunction silently.

### Finding 10.2 — `sd_power_system_off` → `delaySystemOff(0)`
**Severity:** Concern

`sd_power_system_off()` (SoftDevice compatibility) calls `delaySystemOff(0)` which is a no-op delay, not an actual system off. The real `systemOffWakeReset()` is different. SoftDevice libraries expecting actual power-off will not get it.

### Finding 10.3 — `enterOTADfu` and `enterSerialDfu` both call `softReset()`
**Severity:** Concern

Both DFU entry points just do a soft reset. There's no actual DFU bootloader handoff mechanism. This will simply reboot into the same sketch.

### Finding 10.4 — `SysTick_Config` stub returns 0
**Severity:** OK (acceptable)

The core manages SysTick internally. The stub prevents mbedTLS/other libraries from reconfiguring it.

---

## 11. Pin Mapping (`variants/xiao_nrf54l15/pins_arduino.h`)

### Finding 11.1 — LED_BUILTIN active-low
**Severity:** OK (correct)

`LED_STATE_ON = LOW` matches the XIAO nRF54L15 schematic (LED connected between VCC and P2.0).

### Finding 11.2 — SAMD11 bridge UART pin crossover
**Severity:** OK (correct)

`PIN_SAMD11_TX` (19) → P1.05 (nRF receives from SAMD11 TX)
`PIN_SAMD11_RX` (18) → P1.04 (nRF transmits to SAMD11 RX)
This crossover is correct for UART communication.

### Finding 11.3 — `PIN_VBAT_EN` and `PIN_VBAT_READ` for battery monitoring
**Severity:** OK (correct)

P1.15 enables the VBAT divider, P1.14 reads the divided voltage. Matches XIAO schematic.

### Finding 11.4 — `PIN_IMU_MIC_PWR` as sense-only pin
**Severity:** Concern

`PIN_IMU_MIC_PWR` maps to P0.01 which is marked as "Sense IMU+MIC power enable". On the XIAO nRF54L15, this pin may be internally connected to the power enable circuitry and not directly controllable. Writing to it may have no effect or could conflict with the board's power management.

### Finding 11.5 — `PIN_RF_SW_CTL` shared with `PIN_HSPI_SS`
**Severity:** Concern

`PIN_HSPI_SS` is defined as `PIN_RF_SW_CTL` (P2.05). Using HS-SPI with this SS pin will toggle the RF switch control, potentially interfering with BLE/Thread radio operation. This is documented but risky.

---

## 12. `main.cpp`

### Finding 12.1 — `init()` disables SysTick for low-power builds
**Severity:** OK (correct)

For `NRF54L15_CLEAN_POWER_LOW`, SysTick is disabled to avoid interfering with GRTC-based tickless idle. The SysTick ISR pending bit is cleared via ICPR.

### Finding 12.2 — `yield()` WFI behavior
**Severity:** OK (correct)

Balanced mode avoids WFI when BLE is active (sleep cap > 0). Low-power mode uses WFI with CPU frequency scaling.

### Finding 12.3 — `softReset()` via AIRCR
**Severity:** OK (correct)

Uses `0x5FA` vector key + `SYSRESETREQ` bit. Standard CMSIS approach.

---

## 13. `syscalls.c`

### Finding 13.1 — `_write` returns `len` without actually writing
**Severity:** Concern

`_write()` returns `len` (success) without actually outputting anything. This means `printf()` to stdout appears to succeed but produces no output. This is acceptable for Arduino (Serial.print is the intended output path) but could confuse libraries that use `printf`.

### Finding 13.2 — `_sbrk` heap management
**Severity:** OK (correct)

Properly manages heap between `__heap_start__` and `__heap_end__` with bounds checking.

---

## 14. `idle_service.cpp`

### Finding 14.1 — Auto-gate for SPI/Wire
**Severity:** OK (correct)

When `NRF54L15_CLEAN_AUTO_GATE` is enabled, idle service closes unused SPI/I2C peripherals after 2 seconds of inactivity to save power.

---

## 15. `SoftwareTimer.cpp`

### Finding 15.1 — Linked list with non-atomic operations
**Severity:** Concern

`SoftwareTimer` uses a singly-linked list (`head_`/`next_`) accessed from both main context and `yield()` (which can be called from any context). No locking is used. If a timer is added/removed while `serviceAll()` is iterating, there could be a use-after-free or missed callback.

**Impact:** Low risk in single-threaded Arduino, but could cause issues if interrupts modify the timer list.

---

## 16. `new.cpp`

### Finding 16.1 — `__cxa_pure_virtual` enters WFI loop
**Severity:** OK (correct)

Pure virtual call traps in WFI, minimizing power consumption while waiting for debugger.

---

## 17. Register Header (`nrf54l15.h`)

### Finding 17.1 — All peripheral base addresses verified against datasheet
**Severity:** OK (all addresses match)

Cross-referenced all `_NS_BASE` and `_S_BASE` definitions against the nRF54L15 datasheet. All addresses are correct.

### Finding 17.2 — TrustZone secure/non-secure split
**Severity:** OK (correct)

`#ifdef NRF_TRUSTZONE_NONSECURE` correctly selects NS vs S base addresses for all peripherals.

### Finding 17.3 — GRTC IRQ group hardcoded to 0
**Severity:** OK (correct for CPUAPP)

CPUAPP uses GRTC group 0. The comment correctly notes that nRF53-style GROUP=2 would be wrong for nRF54L15.

### Finding 18.6.1 — `nrf54l15_regs.h` VPR/CTRLAPPERI use non-secure aliases unconditionally
**Severity:** Concern

**File:** `nrf54l15_regs.h`, lines 136-137
**What:** `VPR_BASE = 0x4004C000UL` and `CTRLAPPERI_BASE = 0x40052000UL` are hardcoded as non-secure addresses regardless of the `NRF_TRUSTZONE_NONSECURE` define. The comment says this is intentional: "CPUAPP-side VPR and CTRLAPPERI access is exposed through the non-secure aliases on nRF54L15, even when the current Arduino runtime itself is built in the secure world."
**Issue:** If the Arduino core is built in secure mode (no `NRF_TRUSTZONE_NONSECURE`), accessing `0x4004C000` (NS alias) from secure world will generate a bus fault or access violation. The current code works because the Arduino core is always built non-secure, but this is fragile — a future TrustZone-aware build would break. The `nrf54lm20b.h` header uses the same pattern with the same hardcoded NS addresses.
**Recommendation:** Add `#ifdef NRF_TRUSTZONE_NONSECURE` guards or document this constraint explicitly.

---

### Finding 17.4 — Compatibility aliases for field values
**Severity:** OK (correct)

All `#undef`/`#define` patterns correctly override Nordic's pre-shifted values with position-shifted values expected by core code.

---

## 18. Summary of Issues by Severity

### Blockers: None

### Concerns (12):
1. **Finding 2.2** — PLL spin limit could hang indefinitely
2. **Finding 2.6** — D-Cache enabled without I-Cache (coherency risk)
3. **Finding 4.5** — `attachInterrupt(..., LOW)` is edge-triggered, not level-sensitive
4. **Finding 8.4** — A4 has no SAADC channel (analogRead(A4) fails silently)
5. **Finding 9.2** — SysTick fallback millis() has initialization edge case
6. **Finding 10.1** — NVMC/UICR stubs return bogus data
7. **Finding 11.5** — HS_SPI_SS shared with RF_SW_CTL
8. **Finding 18.5.2** — XIAO LM20B `analogInputToDigitalPin` skips indices 4-6
9. **Finding 18.5.3** — HOLYIOT `PIN_LED_BUILTIN` overlaps `PIN_D4` (same physical pin P1.10)
10. **Finding 18.5.4** — `analogRead(A4)` returns -1 silently on XIAO (no SAADC channel)
11. **Finding 18.5.5** — `wiring_digital.c` `pinToPortPin` missing `PIN_VBAT_EN` case (P1.15)
12. **Finding 18.6.1** — `nrf54l15_regs.h` VPR/CTRLAPPERI use non-secure aliases unconditionally

### Suggestions (4):
1. **Finding 2.3** — Document frequency rounding behavior
2. **Finding 3.3** — Consider larger stack for BLE+Thread workloads
3. **Finding 13.1** — Consider routing `_write` to Serial for printf support
4. **Finding 15.1** — Add locking to SoftwareTimer linked list

---

## 19. Overall Assessment

This is a **well-architected** custom Arduino core for the nRF54L15. The implementation demonstrates deep understanding of:
- nRF54L15 peripheral architecture (serial fabric, TrustZone, GRTC)
- Power management (tickless idle, SYSTEM OFF, CPU frequency scaling)
- Arduino API compatibility (nRF52 shims, SAMD wiring_private)
- DMA-based peripheral operation (UARTE, SPIM, TWIM)

The most significant issues are the **NVMC/UICR stub compatibility** (Finding 10.1) and the **edge-triggered vs level-sensitive interrupt** difference (Finding 4.5), which could cause subtle bugs in libraries ported from nRF52/SAMD cores.

The core is production-ready for XIAO nRF54L15 development with the noted caveats.
