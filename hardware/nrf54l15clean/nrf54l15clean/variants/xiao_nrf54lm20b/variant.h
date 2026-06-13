/*
 * XIAO nRF54LM20A variant header.
 *
 * LM20A has no software-controlled RF switch; the antenna path is fixed by
 * board hardware.
 */

#ifndef VARIANT_H
#define VARIANT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ─── Board Capabilities ───────────────────────────────────────
#define NRF54LM20A_BOARD_HAS_RGB_LED     1
#define NRF54LM20A_BOARD_LED_ACTIVE_LOW  1
#define NRF54LM20A_BOARD_HAS_BUTTON      1
#define NRF54LM20A_BOARD_HAS_EXTERNAL_FLASH 1
#define NRF54LM20A_BOARD_HAS_RF_SWITCH   0

#define NRF54LM20B_BOARD_HAS_RGB_LED     NRF54LM20A_BOARD_HAS_RGB_LED
#define NRF54LM20B_BOARD_LED_ACTIVE_LOW  NRF54LM20A_BOARD_LED_ACTIVE_LOW
#define NRF54LM20B_BOARD_HAS_BUTTON      NRF54LM20A_BOARD_HAS_BUTTON
#define NRF54LM20B_BOARD_HAS_EXTERNAL_FLASH NRF54LM20A_BOARD_HAS_EXTERNAL_FLASH
#define NRF54LM20B_BOARD_HAS_RF_SWITCH   NRF54LM20A_BOARD_HAS_RF_SWITCH

// ─── Antenna type stub (required by HAL board policy) ─────────
// LM20A has no antenna switch — always uses the fixed board antenna path.
typedef enum {
    XIAO_NRF54L15_ANTENNA_CERAMIC = 0,
    XIAO_NRF54L15_ANTENNA_EXTERNAL = 1,
    XIAO_NRF54L15_ANTENNA_CONTROL_HIZ = 2,
    XIAO_NRF54L15_ANTENNA_COUNT = 3
} xiao_nrf54l15_antenna_t;

// Compat alias for the early LM20B-named API.
#define XIAO_LM20B_ANTENNA_BUILTIN XIAO_NRF54L15_ANTENNA_CERAMIC

static inline const char* boardAntennaSelectionFromPath(int path) {
    (void)path;
    return "builtin";
}

// ─── RF path select stub (no-op on LM20A) ────────────────────
static inline void xiaoNrf54l15SetAntenna(xiao_nrf54l15_antenna_t sel) {
    (void)sel;
}
static inline xiao_nrf54l15_antenna_t xiaoNrf54l15GetAntenna(void) {
    return XIAO_NRF54L15_ANTENNA_CERAMIC;
}

// ─── IMU/MIC stubs (no Sense variant yet) ───────────────────
static inline int arduinoXiaoNrf54l15SetImuMicEnable(uint8_t enable) {
    (void)enable; return 0;
}
static inline int arduinoXiaoNrf54l15GetImuMicEnable(void) {
    return 0;
}

// ─── RF switch stub (no RF switch on LM20A) ──────────────────
static inline int arduinoXiaoNrf54l15SetRfSwitchPower(uint8_t enable) {
    (void)enable; return 0;
}
static inline int arduinoXiaoNrf54l15GetRfSwitchPower(void) {
    return 0;
}

// ─── Low power stub ──────────────────────────────────────────
int xiaoNrf54lm20QspiFlashPrepareForSleep(void);

static inline void xiaoNrf54l15EnterLowestPowerBoardState(void) {
    (void)xiaoNrf54lm20QspiFlashPrepareForSleep();
}

// ─── Bootloader / Reset ───────────────────────────────────────
// LM20A supports CMSIS-DAP / pyOCD / probe-rs.

#ifdef __cplusplus
}
#endif

#endif /* VARIANT_H */
