/*
 * XIAO nRF54LM20B variant header.
 *
 * ⚠️ HARDWARE NOTE: No schematic available yet.
 * Features and pinout should be verified with physical hardware.
 */

#ifndef VARIANT_H
#define VARIANT_H

#ifdef __cplusplus
extern "C" {
#endif

// ─── Board Capabilities ───────────────────────────────────────
#define NRF54LM20B_BOARD_HAS_RGB_LED     1
#define NRF54LM20B_BOARD_LED_ACTIVE_LOW  1
#define NRF54LM20B_BOARD_HAS_BUTTON      1
#define NRF54LM20B_BOARD_HAS_EXTERNAL_FLASH 1  // 8 MB external flash

// ⚠️ Unverified — awaiting schematic confirmation
// #define NRF54LM20B_BOARD_HAS_RF_SWITCH       0
// #define NRF54LM20B_BOARD_HAS_IMU             0
// #define NRF54LM20B_BOARD_HAS_PDM_MIC         0

// ─── Bootloader / Reset ───────────────────────────────────────
// LM20B uses nrfutil-mcumgr for firmware updates
// Also supports CMSIS-DAP / pyOCD / probe-rs

#ifdef __cplusplus
}
#endif

#endif /* VARIANT_H */
