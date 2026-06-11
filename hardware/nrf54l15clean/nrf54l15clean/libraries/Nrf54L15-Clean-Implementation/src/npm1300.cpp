/*
 * nPM1300 PMIC driver — GPIO bit-bang I2C.
 *
 * Uses P1.17 (SCL) / P1.18 (SDA) with direct GPIO register access.
 * No TWIM peripheral is used — zero residual current between transactions.
 * Pin state is reset to input after every I2C operation.
 */
#include "npm1300.h"
#include <Arduino.h>

#if defined(PIN_PMIC_SDA) && defined(PIN_PMIC_SCL)
#define NPM1300_HAS_BOARD_PMIC 1
#else
#define NPM1300_HAS_BOARD_PMIC 0
#endif

namespace {

#if NPM1300_HAS_BOARD_PMIC

static volatile uint32_t* const gpio = (volatile uint32_t*)0x500D8200UL;

static uint8_t _sclPin = 0, _sdaPin = 0;
static bool _pinsReady = false;

static void pins_init() {
    if (_pinsReady) return;
    uint8_t port;
    if (pinToPortPin(PIN_PMIC_SCL, &port, &_sclPin)) {
        gpio[0x80/4 + _sclPin*4] = (1<<0)|(1<<1);  // Output+Connect
    }
    if (pinToPortPin(PIN_PMIC_SDA, &port, &_sdaPin)) {
        gpio[0x80/4 + _sdaPin*4] = (1<<0)|(1<<1);  // Output+Connect
    }
    gpio[0x04/4] = (1UL << _sclPin) | (1UL << _sdaPin);  // Both high
    _pinsReady = true;
}

static void pins_default() {
    if (!_pinsReady) return;
    gpio[0x80/4 + _sclPin*4] = 2;  // Input+Connect
    gpio[0x80/4 + _sdaPin*4] = 2;
    _pinsReady = false;
}

static void scl_h() { gpio[0x04/4] = (1UL << _sclPin); }
static void scl_l() { gpio[0x08/4] = (1UL << _sclPin); }
static void sda_h() { gpio[0x04/4] = (1UL << _sdaPin); }
static void sda_l() { gpio[0x08/4] = (1UL << _sdaPin); }
static void sda_in() { gpio[0x80/4 + _sdaPin*4] = (0<<0)|(1<<1); }
static void sda_out(){ gpio[0x80/4 + _sdaPin*4] = (1<<0)|(1<<1); }
static int  sda_rd() { return (gpio[0x0C/4] >> _sdaPin) & 1; }

static void i2c_delay() { for(volatile int i=0; i<20; i++) __asm__("nop"); }

static void i2c_start() { sda_out(); sda_h(); scl_h(); i2c_delay(); sda_l(); i2c_delay(); scl_l(); }
static void i2c_stop()  { sda_l(); i2c_delay(); scl_h(); i2c_delay(); sda_h(); }

static bool i2c_write(uint8_t b) {
    sda_out();
    for (int i=0; i<8; i++) {
        if (b & 0x80) sda_h(); else sda_l();
        i2c_delay(); scl_h(); i2c_delay(); scl_l();
        b <<= 1;
    }
    sda_h(); sda_in(); i2c_delay();
    scl_h(); i2c_delay();
    bool ack = (sda_rd() == 0);
    scl_l();
    return ack;
}

static uint8_t i2c_read(bool nack) {
    sda_in();
    uint8_t val = 0;
    for (int i=0; i<8; i++) {
        scl_h(); i2c_delay();
        val = (val << 1) | sda_rd();
        scl_l(); i2c_delay();
    }
    sda_out();
    if (nack) sda_h(); else sda_l();
    i2c_delay(); scl_h(); i2c_delay(); scl_l();
    sda_h();
    return val;
}

bool pmic_bus_begin() {
    pins_init();
    return true;
}

void pmic_bus_end() {
    pins_default();
}

bool pmic_read_burst(uint8_t base, uint8_t offset, uint8_t* data, size_t len) {
    if (!pmic_bus_begin()) return false;
    
    i2c_start();
    bool ok = i2c_write(NPM1300_ADDR << 1);     // Write address
    ok = ok && i2c_write(base);                   // Register base
    ok = ok && i2c_write(offset);                 // Register offset
    if (!ok) { i2c_stop(); pmic_bus_end(); return false; }
    
    // Repeated start for read
    sda_h(); scl_h(); i2c_delay();
    sda_l(); i2c_delay(); scl_l();
    
    ok = i2c_write((NPM1300_ADDR << 1) | 1);     // Read address
    if (!ok) { i2c_stop(); pmic_bus_end(); return false; }
    
    for (size_t i = 0; i < len; i++) {
        data[i] = i2c_read(i == len - 1);  // NACK last byte
    }
    i2c_stop();
    pmic_bus_end();
    return true;
}

bool pmic_write_burst(uint8_t base, uint8_t offset, const uint8_t* data, size_t len) {
    if (!pmic_bus_begin()) return false;
    
    i2c_start();
    bool ok = i2c_write(NPM1300_ADDR << 1);      // Write address
    ok = ok && i2c_write(base);                    // Register base
    ok = ok && i2c_write(offset);                  // Register offset
    for (size_t i = 0; i < len && ok; i++) {
        ok = i2c_write(data[i]);                   // Data bytes
    }
    i2c_stop();
    pmic_bus_end();
    return ok;
}

#endif  // NPM1300_HAS_BOARD_PMIC

// ─── Register offsets ──────────────────────────────────────
constexpr uint8_t kMainOffsetVersion = 0x26U;
constexpr uint8_t kVbusOffsetStatus = 0x07U;
constexpr uint8_t kChargerOffsetStatus = 0x07U;
constexpr uint8_t kChargerOffsetError = 0x08U;
constexpr uint8_t kChargerOffsetISet = 0x02U;
constexpr uint8_t kChargerOffsetVTerm = 0x03U;
constexpr uint8_t kChargerOffsetEnSet = 0x00U;
constexpr uint8_t kChargerOffsetEnClr = 0x01U;
constexpr uint8_t kChargerOffsetDischargeSet = 0x14U;
constexpr uint8_t kAdcOffsetIbatEnable = 0x02U;
constexpr uint8_t kAdcOffsetTaskAuto = 0x07U;
constexpr uint8_t kAdcOffsetConfig = 0x00U;
constexpr uint8_t kAdcOffsetTaskVbat = 0x10U;
constexpr uint8_t kAdcOffsetTaskNtc = 0x11U;
constexpr uint8_t kAdcOffsetTaskTemp = 0x12U;
constexpr uint8_t kAdcOffsetResults = 0x36U;
constexpr uint8_t kAdcLsbVbatShift = 0U;
constexpr uint8_t kAdcLsbDieShift = 2U;
constexpr uint8_t kAdcLsbIbatShift = 4U;
constexpr uint32_t kAdcCacheWindowMs = 500U;
constexpr uint8_t kLdSwOffsetEnSet = 0x00U;
constexpr uint8_t kLdSwOffsetEnClr = 0x04U;
constexpr uint8_t kLdSwOffsetVoutSel = 0x20U;
constexpr uint8_t kLdSwOffsetStatus = 0x08U;
constexpr uint8_t kBuckOffsetEnSet = 0x00U;
constexpr uint8_t kBuckOffsetEnClr = 0x01U;
constexpr uint8_t kBuckOffsetPwmSet = 0x04U;
constexpr uint8_t kBuckOffsetPwmClr = 0x05U;
constexpr uint8_t kBuckOffsetCtrl0  = 0x15U;
constexpr uint8_t kBuckOffsetVoutNorm = 0x08U;
constexpr uint8_t kBuckOffsetSwCtrl = 0x0FU;
constexpr uint8_t kBuckOffsetStatus = 0x34U;
constexpr uint8_t kBuck1OnMask = 0x04U;
constexpr uint8_t kBuck2OnMask = 0x40U;
constexpr uint8_t kGpioCount = 2U;
constexpr uint8_t kGpioOffsetMode = 0x04U;
constexpr uint8_t kGpioOffsetStatus = 0x01U;
constexpr uint8_t kLedOffsetEnSet = 0x00U;
constexpr int32_t kDieTempOffsetMilliC = 267000;
constexpr int32_t kDieTempFactorMul = 1000;
constexpr int32_t kDieTempFactorDiv = 3870;

// ─── State ─────────────────────────────────────────────────
bool g_present = false;
bool g_probeValid = false;
bool g_adcValid = false;
uint32_t g_adcCachedMs = 0;

struct AdcResults {
    uint8_t msbVbat, msbDie, msbIbat, lsbA, lsbB;
};
AdcResults g_adcCache{};
uint8_t g_chargerStatus = 0;
uint8_t g_chargerError = 0;
uint8_t g_vbusStatus = 0;
int32_t g_chargeCurrentUa = 32000;
int32_t g_dischargeLimitUa = 84000;

// ─── Helpers ───────────────────────────────────────────────
static uint8_t clamp_channel(uint8_t ch) { return (ch > 1U) ? 1U : ch; }

static uint16_t adc10(uint8_t msb, uint8_t lsb, uint8_t shift) {
    return ((static_cast<uint16_t>(msb) << 2) | ((lsb >> shift) & 3U));
}

static int32_t adc_to_mv(uint16_t raw) { return (raw * 5000) / 1024; }

static bool voltage_to_index(uint16_t mv, uint8_t* idx) {
    if (mv <= 1000) { *idx = 0; return true; }
    if (mv <= 1100) { *idx = 1; return true; }
    if (mv <= 1200) { *idx = 2; return true; }
    if (mv <= 1300) { *idx = 3; return true; }
    if (mv <= 1400) { *idx = 4; return true; }
    if (mv <= 1500) { *idx = 5; return true; }
    if (mv <= 1600) { *idx = 6; return true; }
    if (mv <= 1700) { *idx = 7; return true; }
    if (mv <= 1800) { *idx = 8; return true; }
    if (mv <= 2000) { *idx = 9; return true; }
    if (mv <= 2100) { *idx = 10; return true; }
    if (mv <= 2500) { *idx = 11; return true; }
    if (mv <= 2700) { *idx = 12; return true; }
    if (mv <= 3000) { *idx = 13; return true; }
    *idx = 14; return true;  // 3.3V
}

static uint8_t charger_current_code(uint16_t ma) {
    if (ma <= 20) return 1; else if (ma <= 50) return 2;
    else if (ma <= 100) return 3; else if (ma <= 150) return 4;
    else if (ma <= 200) return 5; else if (ma <= 300) return 6;
    else if (ma <= 400) return 7; else if (ma <= 500) return 8;
    return 9;
}

// ─── Internal helpers ─────────────────────────────────────
static void ensure_adc_active() {
    uint8_t revision = 0;
    if (!g_probeValid) {
        g_present = npm1300_read_reg(NPM1300_BASE_MAIN, kMainOffsetVersion, &revision);
        g_probeValid = true;
    }
    if (g_present) {
        npm1300_write_reg(NPM1300_BASE_ADC, kAdcOffsetIbatEnable, 1U);
        npm1300_write_reg(NPM1300_BASE_ADC, kAdcOffsetTaskAuto, 1U);
        npm1300_write_reg(NPM1300_BASE_ADC, kAdcOffsetConfig, 0U);
    }
}

static bool read_adc_results(AdcResults* out) {
    if (!out) return false;
    const uint32_t now = millis();
    if (g_adcValid && (now - g_adcCachedMs) <= kAdcCacheWindowMs) {
        *out = g_adcCache; return true;
    }
    ensure_adc_active();
    const uint8_t tasks[] = {1U, 1U, 1U};
    if (!npm1300_write_burst(NPM1300_BASE_ADC, kAdcOffsetTaskVbat, tasks, sizeof(tasks)))
        return false;
    npm1300_read_reg(NPM1300_BASE_CHARGER, kChargerOffsetStatus, &g_chargerStatus);
    npm1300_read_reg(NPM1300_BASE_CHARGER, kChargerOffsetError, &g_chargerError);
    npm1300_read_reg(NPM1300_BASE_VBUS, kVbusOffsetStatus, &g_vbusStatus);
    uint8_t buf[5];
    if (!npm1300_read_burst(NPM1300_BASE_ADC, kAdcOffsetResults, buf, sizeof(buf)))
        return false;
    g_adcCache.msbVbat = buf[0]; g_adcCache.msbDie = buf[1];
    g_adcCache.msbIbat = buf[2]; g_adcCache.lsbA = buf[3]; g_adcCache.lsbB = buf[4];
    g_adcValid = true; g_adcCachedMs = now;
    *out = g_adcCache;
    return true;
}

static bool buck_set_mode(uint8_t channel, uint8_t mode) {
    channel = clamp_channel(channel);
    npm1300_write_reg(NPM1300_BASE_BUCK, kBuckOffsetPwmClr + (channel * 2U), 1U);
    if (mode == NPM1300_BUCK_MODE_FORCE_PWM)
        return npm1300_write_reg(NPM1300_BASE_BUCK, kBuckOffsetPwmSet + (channel * 2U), 1U);
    if (mode == NPM1300_BUCK_MODE_FORCE_HYST)
        return npm1300_write_reg(NPM1300_BASE_BUCK, kBuckOffsetCtrl0, 1U << channel);
    return npm1300_write_reg(NPM1300_BASE_BUCK, kBuckOffsetCtrl0, 0U);
}

static bool buck_enable(uint8_t channel, bool enable) {
    channel = clamp_channel(channel);
    return npm1300_write_reg(NPM1300_BASE_BUCK,
        static_cast<uint8_t>((enable ? kBuckOffsetEnSet : kBuckOffsetEnClr) + (channel * 2U)), 1U);
}

static bool buck_set_voltage(uint8_t channel, uint16_t mv) {
    uint8_t index = 0;
    if (!voltage_to_index(mv, &index)) return false;
    channel = clamp_channel(channel);
    return npm1300_write_reg(NPM1300_BASE_BUCK, kBuckOffsetVoutNorm + (channel * 2U), index) &&
           npm1300_write_reg(NPM1300_BASE_BUCK, kBuckOffsetSwCtrl, 1U << channel);
}

static bool buck_is_enabled(uint8_t channel) {
    uint8_t status = 0;
    if (!npm1300_read_reg(NPM1300_BASE_BUCK, kBuckOffsetStatus, &status)) return false;
    return (status & (channel == 0U ? kBuck1OnMask : kBuck2OnMask)) != 0U;
}

}  // namespace

// ─── Public API ────────────────────────────────────────────

bool npm1300_read_reg(uint8_t base, uint8_t offset, uint8_t* value) {
#if NPM1300_HAS_BOARD_PMIC
    return pmic_read_burst(base, offset, value, 1U);
#else
    (void)base; (void)offset; (void)value; return false;
#endif
}

bool npm1300_write_reg(uint8_t base, uint8_t offset, uint8_t value) {
#if NPM1300_HAS_BOARD_PMIC
    return pmic_write_burst(base, offset, &value, 1U);
#else
    (void)base; (void)offset; (void)value; return false;
#endif
}

bool npm1300_read_burst(uint8_t base, uint8_t offset, uint8_t* data, size_t len) {
#if NPM1300_HAS_BOARD_PMIC
    if (!data || !len || len > 32) return false;
    return pmic_read_burst(base, offset, data, len);
#else
    (void)base; (void)offset; (void)data; (void)len; return false;
#endif
}

bool npm1300_write_burst(uint8_t base, uint8_t offset, const uint8_t* data, size_t len) {
#if NPM1300_HAS_BOARD_PMIC
    if (!data || !len || len > 32) return false;
    return pmic_write_burst(base, offset, data, len);
#else
    (void)base; (void)offset; (void)data; (void)len; return false;
#endif
}

bool npm1300_update_reg(uint8_t base, uint8_t offset, uint8_t mask, uint8_t value) {
    uint8_t current = 0;
    if (!npm1300_read_reg(base, offset, &current)) return false;
    return npm1300_write_reg(base, offset, (current & ~mask) | (value & mask));
}

void npm1300_begin(void) { ensure_adc_active(); }
void npm1300_init(void) { npm1300_begin(); }
bool npm1300_is_present(void) { if (!g_probeValid) ensure_adc_active(); return g_present; }

bool npm1300_ldo1_enable(bool enable) {
    return npm1300_write_reg(NPM1300_BASE_LDSW, enable ? kLdSwOffsetEnSet : kLdSwOffsetEnClr, 1U);
}
bool npm1300_ldo1_set_voltage(uint16_t mv) {
    uint8_t idx; if (!voltage_to_index(mv, &idx)) return false;
    return npm1300_write_reg(NPM1300_BASE_LDSW, kLdSwOffsetVoutSel, idx);
}
bool npm1300_ldo1_is_enabled(void) {
    uint8_t s; return npm1300_read_reg(NPM1300_BASE_LDSW, kLdSwOffsetStatus, &s) && (s & 1);
}
bool npm1300_ldo2_enable(bool enable) {
    return npm1300_write_reg(NPM1300_BASE_LDSW, enable ? kLdSwOffsetEnSet+4 : kLdSwOffsetEnClr+4, 1U);
}
bool npm1300_ldo2_set_voltage(uint16_t mv) { return npm1300_ldo1_set_voltage(mv); }
bool npm1300_ldo2_is_enabled(void) {
    uint8_t s; return npm1300_read_reg(NPM1300_BASE_LDSW, kLdSwOffsetStatus, &s) && (s & 2);
}

bool npm1300_buck1_enable(bool e) { return buck_enable(0, e); }
bool npm1300_buck1_set_voltage(uint16_t mv) { return buck_set_voltage(0, mv); }
bool npm1300_buck1_is_enabled(void) { return buck_is_enabled(0); }
bool npm1300_buck2_enable(bool e) { return buck_enable(1, e); }
bool npm1300_buck2_set_voltage(uint16_t mv) { return buck_set_voltage(1, mv); }
bool npm1300_buck2_is_enabled(void) { return buck_is_enabled(1); }
bool npm1300_buck1_set_mode(uint8_t m) { return buck_set_mode(0, m); }
bool npm1300_buck2_set_mode(uint8_t m) { return buck_set_mode(1, m); }

bool npm1300_charger_enable(bool e) {
    return npm1300_write_reg(NPM1300_BASE_CHARGER, e ? kChargerOffsetEnSet : kChargerOffsetEnClr, 1U);
}
bool npm1300_charger_set_current(uint16_t ma) {
    return npm1300_write_reg(NPM1300_BASE_CHARGER, kChargerOffsetISet, charger_current_code(ma));
}
bool npm1300_charger_set_term_voltage(uint16_t mv) {
    uint8_t code = (mv > 3500) ? ((mv - 3500) / 50) : 0;
    if (code > 19) code = 19;
    return npm1300_write_reg(NPM1300_BASE_CHARGER, kChargerOffsetVTerm, code);
}
bool npm1300_charger_is_charging(void) {
    uint8_t s; return npm1300_read_reg(NPM1300_BASE_CHARGER, kChargerOffsetStatus, &s) && (s & 1);
}
bool npm1300_charger_status(uint8_t* s) { return npm1300_read_reg(NPM1300_BASE_CHARGER, kChargerOffsetStatus, s); }
bool npm1300_charger_error(uint8_t* e) { return npm1300_read_reg(NPM1300_BASE_CHARGER, kChargerOffsetError, e); }
bool npm1300_vbus_status(uint8_t* s) { return npm1300_read_reg(NPM1300_BASE_VBUS, kVbusOffsetStatus, s); }

int32_t npm1300_read_vbat_mv(void) {
    AdcResults r{}; if (!read_adc_results(&r)) return -1;
    return adc_to_mv(adc10(r.msbVbat, r.lsbA, kAdcLsbVbatShift));
}
int32_t npm1300_read_temp_mc(void) {
    AdcResults r{}; if (!read_adc_results(&r)) return -1;
    uint16_t code = adc10(r.msbDie, r.lsbA, kAdcLsbDieShift);
    return kDieTempOffsetMilliC - ((int32_t)code * kDieTempFactorMul) / kDieTempFactorDiv;
}
int32_t npm1300_read_ibat_ma(void) {
    AdcResults r{}; if (!read_adc_results(&r)) return -1;
    return (adc10(r.msbIbat, r.lsbB, kAdcLsbIbatShift) * 1000) / 2048;
}
int32_t npm1300_read_vsys_mv(void) { return -1; }
int32_t npm1300_read_vbus_mv(void) {
    uint8_t s; if (!npm1300_read_reg(NPM1300_BASE_VBUS, 0x08, &s)) return -1;
    return (s * 5000) / 1024;
}

bool npm1300_enter_ship_mode(void) {
    return npm1300_write_reg(NPM1300_BASE_SHIP, 0x00, 1U);
}
bool npm1300_enter_hibernate(void) {
    return npm1300_write_reg(NPM1300_BASE_SHIP, 0x04, 1U);
}
bool npm1300_led_set(uint8_t led, uint8_t brightness) {
    return npm1300_write_reg(NPM1300_BASE_LED, led * 2U, brightness ? 1U : 0U);
}
bool npm1300_gpio_set_mode(uint8_t pin, uint8_t mode) {
    if (pin >= kGpioCount || mode > 9) return false;
    return npm1300_write_reg(NPM1300_BASE_GPIO, kGpioOffsetMode + pin, mode);
}
bool npm1300_gpio_write(uint8_t pin, bool high) {
    return npm1300_gpio_set_mode(pin, high ? 8U : 9U);
}
bool npm1300_gpio_status(uint8_t* status) {
    return npm1300_read_reg(NPM1300_BASE_GPIO, kGpioOffsetStatus, status);
}
