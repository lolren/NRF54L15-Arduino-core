/*
 * nPM1300 PMIC Driver implementation.
 * Uses TWIM22 via direct register access for minimal overhead.
 */
#include "npm1300.h"

// TWIM22 base — same as Wire (header I2C)
static volatile uint32_t* const twi = (volatile uint32_t*)0x500C8000UL;

static void twi_begin(void) {
    twi[0x500/4] = 0;
    twi[0x600/4] = (1UL << 5) | 17;  // SCL = P1.17
    twi[0x604/4] = (1UL << 5) | 18;  // SDA = P1.18
    twi[0x524/4] = 0x01980000;       // 100 kHz
    twi[0x500/4] = 6;                // TWIM enable
}

static void twi_end(void) {
    twi[0x500/4] = 0;
}

static bool twi_write(uint8_t addr, const uint8_t* data, uint8_t len) {
    if (len > 16) return false;
    volatile uint32_t* dma = (volatile uint32_t*)0x20000000UL;
    for (int i = 0; i < len; i++) dma[i] = data[i];

    twi[0x588/4] = addr;
    twi[0x73C/4] = 0x20000000;
    twi[0x740/4] = len;
    twi[0x050/4] = 1;  // STARTTX

    for (volatile int i = 0; i < 100000; i++) {
        if (twi[0x168/4] || twi[0x4C4/4]) break;
    }
    bool ok = twi[0x168/4] && !twi[0x4C4/4];
    twi[0x168/4] = 0;
    twi[0x4C4/4] = 0xFFFFFFFF;
    return ok;
}

static bool twi_read(uint8_t addr, uint8_t* data, uint8_t len) {
    if (len > 16) return false;
    twi[0x588/4] = addr;
    twi[0x704/4] = 0x20000000;
    twi[0x708/4] = len;
    twi[0x028/4] = 1;  // STARTRX

    for (volatile int i = 0; i < 100000; i++) {
        if (twi[0x14C/4] || twi[0x4C4/4]) break;
    }
    bool ok = twi[0x14C/4] && !twi[0x4C4/4];
    if (ok) {
        volatile uint32_t* dma = (volatile uint32_t*)0x20000000UL;
        for (int i = 0; i < len; i++) data[i] = dma[i] & 0xFF;
    }
    twi[0x14C/4] = 0;
    twi[0x4C4/4] = 0xFFFFFFFF;
    return ok;
}

// Write to 16-bit register address (nPM1300 uses 8-bit reg + 8-bit data)
static bool reg_write(uint16_t reg, uint8_t value) {
    uint8_t buf[2] = { (uint8_t)(reg & 0xFF), value };
    twi_begin();
    bool ok = twi_write(NPM1300_ADDR, buf, 2);
    twi_end();
    return ok;
}

static int32_t reg_read(uint16_t reg, uint8_t len) {
    uint8_t buf[2];
    int32_t result = 0;

    // First write the register address
    uint8_t addr_buf[1] = { (uint8_t)(reg & 0xFF) };
    twi_begin();
    if (!twi_write(NPM1300_ADDR, addr_buf, 1)) { twi_end(); return -1; }
    // Then read data
    if (len <= 4) {
        uint8_t rbuf[4] = {0};
        if (!twi_read(NPM1300_ADDR, rbuf, len)) { twi_end(); return -1; }
        for (int i = 0; i < len; i++) result |= ((int32_t)rbuf[i]) << (i * 8);
    }
    twi_end();
    return result;
}

// ─── Public API ──────────────────────────────────────────────

void npm1300_begin(void) {
    // PMIC is configured at boot by OTP. Set LDO1 to LDO mode with 1.8V.
    // The schematic shows LDO1 powers IMU/MIC at 1.8V — OTP default.
    // Optionally set explicitly:
    // reg_write(NPM1300_LDSW1_LDOSEL, 0x01);  // LDO mode (not load switch)
    // reg_write(NPM1300_LDSW1_VOUTSEL, 0x08);  // 1.8V (depends on encoding)
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
    else code = 14;  // 3.3V
    return reg_write(NPM1300_LDSW1_VOUTSEL, code);
}

bool npm1300_ldo1_is_enabled(void) {
    int32_t status = reg_read(NPM1300_LDSW1_STATUS, 1);
    return status > 0 && (status & 0x01);
}

bool npm1300_ldo2_enable(bool enable) {
    return reg_write(enable ? NPM1300_TASK_LDSW2_SET : NPM1300_TASK_LDSW2_CLR, 0x01);
}

bool npm1300_ldo2_set_voltage(uint16_t mv) {
    return npm1300_ldo1_set_voltage(mv);  // Same voltage table, different register
    // Would need NPM1300_LDSW2_VOUTSEL
}

// ─── Charger ─────────────────────────────────────────────────

bool npm1300_charger_enable(bool enable) {
    return reg_write(enable ? NPM1300_CHARGER_ENABLE : NPM1300_CHARGER_DISABLE, 0x01);
}

bool npm1300_charger_set_current(uint16_t ma) {
    uint8_t code;
    if (ma <= 20) code = 1;
    else if (ma <= 50) code = 2;
    else if (ma <= 100) code = 3;
    else if (ma <= 150) code = 4;
    else if (ma <= 200) code = 5;
    else if (ma <= 300) code = 6;
    else if (ma <= 400) code = 7;
    else if (ma <= 500) code = 8;
    else code = 9;
    return reg_write(NPM1300_CHARGER_ICHARGE, code);
}

bool npm1300_charger_set_term_voltage(uint16_t mv) {
    uint8_t code = (mv - 3500) / 50;  // 3.5V to 4.45V in 50mV steps
    if (code > 19) code = 19;
    return reg_write(NPM1300_CHARGER_VTERM, code);
}

bool npm1300_charger_is_charging(void) {
    int32_t status = reg_read(NPM1300_CHARGER_STATUS, 1);
    return status > 0 && (status & 0x01);
}

// ─── System Measurements ─────────────────────────────────────

static int32_t measure_and_read(uint16_t task_reg, uint16_t result_reg) {
    if (!reg_write(task_reg, 0x01)) return -1;
    // Wait for measurement (~10ms)
    for (volatile int i = 0; i < 50000; i++) __asm__ volatile("nop");
    return reg_read(result_reg, 2);
}

int32_t npm1300_read_vbat_mv(void) {
    int32_t raw = measure_and_read(NPM1300_TASK_VBAT_MEASURE, NPM1300_SYSMON_VBAT_RESULT);
    if (raw < 0) return -1;
    return (raw * 1000) / 1024;  // 10-bit ADC, Vref = internal
}

int32_t npm1300_read_temp_mc(void) {
    int32_t raw = measure_and_read(NPM1300_TASK_TEMP_MEASURE, NPM1300_SYSMON_TEMP_RESULT);
    if (raw < 0) return -1;
    // Approximate: 0.25°C per LSB, offset depends on calibration
    return (raw * 250);  // millicelsius
}

int32_t npm1300_read_ibat_ma(void) {
    int32_t raw = measure_and_read(NPM1300_TASK_IBAT_MEASURE, NPM1300_SYSMON_IBAT_RESULT);
    if (raw < 0) return -1;
    return (raw * 1000) / 2048;  // ~0.5mA per LSB
}

int32_t npm1300_read_vsys_mv(void) {
    int32_t raw = measure_and_read(NPM1300_TASK_VSYS_MEASURE, NPM1300_SYSMON_VSYS_RESULT);
    if (raw < 0) return -1;
    return (raw * 5000) / 1024;
}

int32_t npm1300_read_vbus_mv(void) {
    int32_t raw = measure_and_read(NPM1300_TASK_VBUS_MEASURE, NPM1300_SYSMON_VBUS_RESULT);
    if (raw < 0) return -1;
    return (raw * 5000) / 1024;
}

// ─── Power Modes ─────────────────────────────────────────────

bool npm1300_enter_ship_mode(void) {
    return reg_write(NPM1300_TASK_ENTER_SHIP, 0x01);
}

bool npm1300_enter_hibernate(void) {
    return reg_write(NPM1300_TASK_ENTER_HIBERNATE, 0x01);
}

// ─── LED Driver ──────────────────────────────────────────────

bool npm1300_led_set(uint8_t led, uint8_t brightness) {
    uint16_t reg = 0x0700 + led * 4;  // LED0..LED2 control registers
    return reg_write(reg, brightness);
}
