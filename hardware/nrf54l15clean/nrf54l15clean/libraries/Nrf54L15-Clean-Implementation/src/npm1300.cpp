/*
 * nPM1300 PMIC Driver implementation.
 * Uses TWIM22 via exact Wire-library register sequence for nRF54L.
 * PMIC I2C: Address 0x6B, SDA=P1.18, SCL=P1.17
 */
#include "npm1300.h"

// TWIM22 base — same as Wire (header I2C), but we retarget to PMIC pins
static volatile uint32_t* const twi = (volatile uint32_t*)0x500C8000UL;

static void twi_begin_pmic(void) {
    twi[0x500/4] = 0;
    twi[0x600/4] = (1UL << 5) | 17;  // SCL = P1.17
    twi[0x604/4] = (1UL << 5) | 18;  // SDA = P1.18
    twi[0x524/4] = 0x01980000;       // 100 kHz
    twi[0x200/4] = 0;                 // SHORTS = 0
    twi[0x500/4] = 6;                 // TWIM enable
}

static void twi_end(void) {
    twi[0x500/4] = 0;
}

// Write to 16-bit register address (nPM1300 uses 8-bit reg + 8-bit data)
static bool reg_write(uint16_t reg, uint8_t value) {
    volatile uint32_t* dma = (volatile uint32_t*)0x20000000UL;
    
    twi_begin_pmic();
    for (volatile int i = 0; i < 100; i++) __asm__ volatile("nop");
    
    // Clear events and errors (matching Wire.cpp sequence)
    twi[0x104/4] = 0;  // STOPPED
    twi[0x114/4] = 0;   // ERROR
    twi[0x138/4] = 0;   // LASTTX
    twi[0x168/4] = 0;   // DMA_TX_END
    twi[0x4C4/4] = 0x7;  // ERRORSRC all
    
    // Pack: [reg_lo, value]
    dma[0] = (reg & 0xFF) | ((uint32_t)value << 8);
    
    twi[0x588/4] = NPM1300_ADDR;          // ADDRESS
    twi[0x73C/4] = 0x20000000;             // DMA_TX_PTR
    twi[0x740/4] = 2;                       // MAXCNT
    twi[0x050/4] = 1;                        // DMA_TX_START
    
    // Wait for LASTTX (0x138) or ERROR (0x114)
    bool ok = false;
    for (volatile int i = 0; i < 500000; i++) {
        if (twi[0x138/4] || twi[0x114/4]) { ok = true; break; }
    }
    
    // STOP
    twi[0x004/4] = 1;
    for (volatile int i = 0; i < 500000; i++) {
        if (twi[0x104/4]) break;
    }
    
    uint32_t esrc = twi[0x4C4/4] & 0x7;
    bool noerror = (twi[0x114/4] == 0) && esrc == 0;
    twi_end();
    return ok && noerror;
}

// ─── Public API ──────────────────────────────────────────────

void npm1300_begin(void) {
    // PMIC is configured at boot by OTP. 
    // LDO1 powers IMU/MIC at 1.8V — OTP default.
}

bool npm1300_ldo1_enable(bool enable) {
    return reg_write(enable ? NPM1300_TASK_LDSW1_SET : NPM1300_TASK_LDSW1_CLR, 0x01);
}

bool npm1300_ldo1_set_voltage(uint16_t mv) {
    uint8_t code;
    if (mv <= 1000) code = 0;
    else if (mv <= 1100) code = 1;
    else if (mv <= 1200) code = 2;
    else if (mv <= 1300) code = 3;
    else if (mv <= 1400) code = 4;
    else if (mv <= 1500) code = 5;
    else if (mv <= 1600) code = 6;
    else if (mv <= 1700) code = 7;
    else if (mv <= 1800) code = 8;
    else if (mv <= 2000) code = 9;
    else if (mv <= 2100) code = 10;
    else if (mv <= 2500) code = 11;
    else if (mv <= 2700) code = 12;
    else if (mv <= 3000) code = 13;
    else code = 14;
    return reg_write(NPM1300_LDSW1_VOUTSEL, code);
}

bool npm1300_ldo1_is_enabled(void) {
    // Reading status requires register read which needs 
    // write-then-read (repeated start) — not yet implemented in raw mode.
    return true;
}

bool npm1300_ldo2_enable(bool enable) {
    return reg_write(enable ? NPM1300_TASK_LDSW2_SET : NPM1300_TASK_LDSW2_CLR, 0x01);
}

bool npm1300_ldo2_set_voltage(uint16_t mv) {
    return npm1300_ldo1_set_voltage(mv);
}

bool npm1300_charger_enable(bool enable) {
    return reg_write(enable ? NPM1300_CHARGER_ENABLE : NPM1300_CHARGER_DISABLE, 0x01);
}

bool npm1300_charger_set_current(uint16_t ma) {
    uint8_t code;
    if (ma <= 20) code = 1; else if (ma <= 50) code = 2;
    else if (ma <= 100) code = 3; else if (ma <= 150) code = 4;
    else if (ma <= 200) code = 5; else if (ma <= 300) code = 6;
    else if (ma <= 400) code = 7; else if (ma <= 500) code = 8;
    else code = 9;
    return reg_write(NPM1300_CHARGER_ICHARGE, code);
}

bool npm1300_charger_set_term_voltage(uint16_t mv) {
    uint8_t code = (mv > 3500) ? ((mv - 3500) / 50) : 0;
    if (code > 19) code = 19;
    return reg_write(NPM1300_CHARGER_VTERM, code);
}

bool npm1300_charger_is_charging(void) { return false; }

int32_t npm1300_read_vbat_mv(void) { return -1; }
int32_t npm1300_read_temp_mc(void) { return -1; }
int32_t npm1300_read_ibat_ma(void) { return -1; }
int32_t npm1300_read_vsys_mv(void) { return -1; }
int32_t npm1300_read_vbus_mv(void) { return -1; }

bool npm1300_enter_ship_mode(void) {
    return reg_write(NPM1300_TASK_ENTER_SHIP, 0x01);
}
bool npm1300_enter_hibernate(void) {
    return reg_write(NPM1300_TASK_ENTER_HIBERNATE, 0x01);
}
bool npm1300_led_set(uint8_t led, uint8_t brightness) {
    return reg_write(0x0700 + led * 4, brightness);
}
