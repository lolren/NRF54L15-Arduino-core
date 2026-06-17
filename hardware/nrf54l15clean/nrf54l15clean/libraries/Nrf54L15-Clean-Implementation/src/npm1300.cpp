/*
 * nPM1300 PMIC driver — TWIM23 via TwoWire.
 *
 * Uses TWIM23 (free serial fabric instance) on P1.17 (SCL) / P1.18 (SDA).
 * Wire.begin()/end() between each transaction for zero residual current.
 */
#include "npm1300.h"
#include <Arduino.h>
#include <Wire.h>

#if defined(PIN_PMIC_SDA) && defined(PIN_PMIC_SCL)
#define NPM1300_HAS_BOARD_PMIC 1
#else
#define NPM1300_HAS_BOARD_PMIC 0
#endif

namespace {

#if NPM1300_HAS_BOARD_PMIC

// TWIM23 as NRF_TWIM_Type pointer (defined in NCS, cast to address)
#define NRF_TWIM23    ((NRF_TWIM_Type *)0x500ED000UL)

static TwoWire g_pmicWire(NRF_TWIM23, PIN_PMIC_SDA, PIN_PMIC_SCL);
static bool g_busStarted = false;

bool pmic_bus_begin() {
    if (g_busStarted) return true;
    g_pmicWire.begin();
    g_pmicWire.setClock(100000);
    g_busStarted = true;
    return true;
}

void pmic_bus_end() {
    if (g_busStarted) {
        g_pmicWire.end();
        g_busStarted = false;
    }
}

bool pmic_read_burst(uint8_t base, uint8_t offset, uint8_t* data, size_t len) {
    if (!pmic_bus_begin()) return false;
    bool ok = true;
    g_pmicWire.beginTransmission(NPM1300_ADDR);
    g_pmicWire.write(base);
    g_pmicWire.write(offset);
    if (g_pmicWire.endTransmission(false) != 0) { ok = false; goto exit; }
    g_pmicWire.requestFrom(NPM1300_ADDR, (uint8_t)len);
    for (size_t i = 0; i < len; i++) {
        if (g_pmicWire.available()) data[i] = g_pmicWire.read();
        else { ok = false; break; }
    }
exit:
    pmic_bus_end();
    return ok;
}

bool pmic_write_burst(uint8_t base, uint8_t offset, const uint8_t* data, size_t len) {
    if (!pmic_bus_begin()) return false;
    g_pmicWire.beginTransmission(NPM1300_ADDR);
    g_pmicWire.write(base);
    g_pmicWire.write(offset);
    for (size_t i = 0; i < len; i++) g_pmicWire.write(data[i]);
    bool ok = (g_pmicWire.endTransmission() == 0);
    pmic_bus_end();
    return ok;
}

#endif  // NPM1300_HAS_BOARD_PMIC

// ... rest of the file remains the same ...

// ─── Register offsets ──────────────────────────────────────
constexpr uint8_t kMainOffsetVersion = 0x26U;
constexpr uint8_t kVbusOffsetStatus = 0x07U;
constexpr uint8_t kChargerOffsetErrClr = 0x00U;
constexpr uint8_t kChargerOffsetEnSet = 0x04U;
constexpr uint8_t kChargerOffsetEnClr = 0x05U;
constexpr uint8_t kChargerOffsetDisableSet = 0x06U;
constexpr uint8_t kChargerOffsetISet = 0x08U;
constexpr uint8_t kChargerOffsetVTerm = 0x0CU;
constexpr uint8_t kChargerOffsetStatus = 0x34U;
constexpr uint8_t kChargerOffsetError = 0x36U;
constexpr uint8_t kAdcOffsetTaskVbat = 0x00U;
constexpr uint8_t kAdcOffsetTaskNtc = 0x01U;
constexpr uint8_t kAdcOffsetTaskTemp = 0x02U;
constexpr uint8_t kAdcOffsetTaskVsys = 0x03U;
constexpr uint8_t kAdcOffsetTaskVbus = 0x07U;
constexpr uint8_t kAdcOffsetConfig = 0x09U;
constexpr uint8_t kAdcOffsetTaskAuto = 0x0CU;
constexpr uint8_t kAdcOffsetResults = 0x10U;
constexpr uint8_t kAdcOffsetIbatEnable = 0x24U;
constexpr uint8_t kAdcLsbVbatShift = 0U;
constexpr uint8_t kAdcLsbNtcShift = 2U;
constexpr uint8_t kAdcLsbDieShift = 4U;
constexpr uint8_t kAdcLsbVsysShift = 6U;
constexpr uint8_t kAdcLsbIbatShift = 4U;
constexpr uint8_t kAdcLsbVbusShift = 6U;
constexpr uint32_t kAdcCacheWindowMs = 500U;
constexpr uint32_t kAdcConversionTimeUs = 250U;
constexpr uint8_t kLdSwOffsetEnSet = 0x00U;
constexpr uint8_t kLdSwOffsetEnClr = 0x01U;
constexpr uint8_t kLdSwOffsetStatus = 0x04U;
constexpr uint8_t kLdSwOffsetGpiSel = 0x05U;
constexpr uint8_t kLdSwOffsetConfig = 0x07U;
constexpr uint8_t kLdSwOffsetLdoSel = 0x08U;
constexpr uint8_t kLdSwOffsetVoutSel = 0x0CU;
constexpr uint8_t kLdSw1OnMask = 0x03U;
constexpr uint8_t kLdSw2OnMask = 0x0CU;
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
constexpr uint8_t kGpioCount = 5U;
constexpr uint8_t kGpioOffsetMode = 0x00U;
constexpr uint8_t kGpioOffsetStatus = 0x1EU;
constexpr uint8_t kLedCount = 3U;
constexpr uint8_t kLedModeHost = 3U;
constexpr uint8_t kLedOffsetMode = 0x00U;
constexpr uint8_t kLedOffsetSet = 0x03U;
constexpr uint8_t kLedOffsetClr = 0x04U;
constexpr uint8_t kShipOffsetShip = 0x00U;
constexpr uint8_t kShipOffsetHibernate = 0x01U;
constexpr uint8_t kAdcMaxBatch = 6U;
constexpr uint16_t kNpm1300LdoTableSize = 4U;
static const uint16_t kLdoVoltages[] = {1100, 1800, 2500, 3300};
static constexpr uint16_t NPM1300_LDO_VOLTAGE_3V3_RAW = 3300U;

static constexpr uint8_t kChargerStatusChargingMask = 0x04U;
static constexpr int32_t kDieTempOffsetMilliC = 109000;
static constexpr int32_t kDieTempFactorMul = 3390;
static constexpr int32_t kDieTempFactorDiv = 10;

struct AdcResults {
    uint8_t ibatStat, msbVbat, msbNtc, msbDie, msbVsys,
            lsbA, ibatPre, ibatCnt, msbIbat, msbVbus, lsbB;
};

static bool g_probeValid = false;
static bool g_present = false;
static bool g_adcValid = false;
static uint32_t g_adcCachedMs = 0;
static AdcResults g_adcCache = {};
static uint8_t g_chargerStatus = 0;
static uint8_t g_chargerError = 0;
static uint8_t g_vbusStatus = 0;
static int32_t g_chargeCurrentUa = 0;

static inline uint16_t adc10(uint8_t msb, uint8_t lsb, uint8_t shift) {
    return (uint16_t)(((uint16_t)msb << 2U) | ((lsb >> shift) & 0x03U));
}

static int32_t adc_to_mv(uint16_t code) {
    return ((int32_t)code * 1800) / 512;
}

static int32_t ibat_to_ma(uint16_t code, uint8_t stat) {
    const int32_t mul = (stat & 1U) ? 50 : 100;
    return ((int32_t)code - 512) * mul;
}

static uint8_t clamp_channel(uint8_t ch) { return (ch > 1U) ? 0U : ch; }

static uint16_t charger_current_index(uint16_t ma) {
    if (ma > 2000) return 1008;
    uint16_t idx = 16 + (ma - 32) / 2;
    if (idx > 1008) idx = 1008;
    return idx;
}

static uint8_t charger_vterm_index(uint16_t mv) {
    if (mv >= 4500) return 39;
    if (mv <= 3650) return 0;
    return (mv - 3650) / 25;
}

static bool voltage_to_index(uint16_t mv, uint8_t* idx) {
    for (uint8_t i = 0; i < kNpm1300LdoTableSize; i++) {
        if (kLdoVoltages[i] == mv) { *idx = i; return true; }
    }
    return false;
}

static void ensure_adc_active() {
    uint8_t revision = 0;
    if (!g_probeValid || !g_present) {
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
    const uint8_t tasks[] = {1U, 1U, 1U, 1U};
    if (!npm1300_write_burst(NPM1300_BASE_ADC, kAdcOffsetTaskVbat, tasks, sizeof(tasks)))
        return false;
    (void)npm1300_write_reg(NPM1300_BASE_ADC, kAdcOffsetTaskVbus, 1U);
    npm1300_read_reg(NPM1300_BASE_CHARGER, kChargerOffsetStatus, &g_chargerStatus);
    npm1300_read_reg(NPM1300_BASE_CHARGER, kChargerOffsetError, &g_chargerError);
    npm1300_read_reg(NPM1300_BASE_VBUS, kVbusOffsetStatus, &g_vbusStatus);
    delayMicroseconds(kAdcConversionTimeUs * 6U);
    uint8_t buf[11];
    if (!npm1300_read_burst(NPM1300_BASE_ADC, kAdcOffsetResults, buf, sizeof(buf)))
        return false;
    g_adcCache.ibatStat = buf[0];
    g_adcCache.msbVbat = buf[1];
    g_adcCache.msbNtc = buf[2];
    g_adcCache.msbDie = buf[3];
    g_adcCache.msbVsys = buf[4];
    g_adcCache.lsbA = buf[5];
    g_adcCache.msbIbat = buf[8];
    g_adcCache.msbVbus = buf[9];
    g_adcCache.lsbB = buf[10];
    g_adcValid = true; g_adcCachedMs = now;
    *out = g_adcCache;
    return true;
}

static bool buck_set_mode(uint8_t channel, uint8_t mode) {
    channel = clamp_channel(channel);
    const uint8_t pfmMask = static_cast<uint8_t>(1U << channel);
    if (mode == NPM1300_BUCK_MODE_FORCE_PWM) {
        return npm1300_update_reg(NPM1300_BASE_BUCK, kBuckOffsetCtrl0, pfmMask, 0U) &&
               npm1300_write_reg(NPM1300_BASE_BUCK, kBuckOffsetPwmSet + (channel * 2U), 1U);
    }
    if (mode == NPM1300_BUCK_MODE_FORCE_HYST) {
        return npm1300_update_reg(NPM1300_BASE_BUCK, kBuckOffsetCtrl0, pfmMask, pfmMask) &&
               npm1300_write_reg(NPM1300_BASE_BUCK, kBuckOffsetPwmClr + (channel * 2U), 1U);
    }
    return npm1300_update_reg(NPM1300_BASE_BUCK, kBuckOffsetCtrl0, pfmMask, 0U) &&
           npm1300_write_reg(NPM1300_BASE_BUCK, kBuckOffsetPwmClr + (channel * 2U), 1U);
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
    uint8_t s; return npm1300_read_reg(NPM1300_BASE_LDSW, kLdSwOffsetStatus, &s) &&
                      ((s & kLdSw1OnMask) != 0U);
}
bool npm1300_ldo2_enable(bool enable) {
    return npm1300_write_reg(NPM1300_BASE_LDSW,
                             static_cast<uint8_t>((enable ? kLdSwOffsetEnSet : kLdSwOffsetEnClr) + 2U),
                             1U);
}
bool npm1300_ldo2_set_voltage(uint16_t mv) {
    uint8_t idx; if (!voltage_to_index(mv, &idx)) return false;
    return npm1300_write_reg(NPM1300_BASE_LDSW, kLdSwOffsetVoutSel + 1U, idx);
}
bool npm1300_ldo2_is_enabled(void) {
    uint8_t s; return npm1300_read_reg(NPM1300_BASE_LDSW, kLdSwOffsetStatus, &s) &&
                      ((s & kLdSw2OnMask) != 0U);
}
bool npm1300_ldo1_set_mode(uint8_t mode) {
    if (mode > NPM1300_LDSW_MODE_LDO) return false;
    return npm1300_write_reg(NPM1300_BASE_LDSW, kLdSwOffsetLdoSel,
                             mode == NPM1300_LDSW_MODE_LDO ? 1U : 0U);
}
bool npm1300_ldo2_set_mode(uint8_t mode) {
    if (mode > NPM1300_LDSW_MODE_LDO) return false;
    return npm1300_write_reg(NPM1300_BASE_LDSW, kLdSwOffsetLdoSel + 1U,
                             mode == NPM1300_LDSW_MODE_LDO ? 1U : 0U);
}
bool npm1300_imu_mic_power_enable(bool enable) {
    if (!enable) return npm1300_ldo1_enable(false);
    return npm1300_ldo1_set_mode(NPM1300_LDSW_MODE_LDO) &&
           npm1300_ldo1_set_voltage(NPM1300_LDO_VOLTAGE_3V3) &&
           npm1300_ldo1_enable(true);
}
bool npm1300_sensor_power_enable(bool enable) {
    return npm1300_imu_mic_power_enable(enable);
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
    if (e) {
        (void)npm1300_write_reg(NPM1300_BASE_CHARGER, kChargerOffsetErrClr, 1U);
    }
    return npm1300_write_reg(NPM1300_BASE_CHARGER, e ? kChargerOffsetEnSet : kChargerOffsetEnClr, 1U);
}
bool npm1300_charger_set_current(uint16_t ma) {
    const uint16_t idx = charger_current_index(ma);
    const uint8_t data[] = {
        static_cast<uint8_t>(idx / 2U),
        static_cast<uint8_t>(idx & 1U)
    };
    if (!npm1300_write_burst(NPM1300_BASE_CHARGER, kChargerOffsetISet, data, sizeof(data))) {
        return false;
    }
    g_chargeCurrentUa = 32000 + (static_cast<int32_t>(idx) - 16) * 2000;
    return true;
}
bool npm1300_charger_set_term_voltage(uint16_t mv) {
    return npm1300_write_reg(NPM1300_BASE_CHARGER, kChargerOffsetVTerm,
                             charger_vterm_index(mv));
}
bool npm1300_charger_is_charging(void) {
    uint8_t s; return npm1300_read_reg(NPM1300_BASE_CHARGER, kChargerOffsetStatus, &s) &&
                      ((s & kChargerStatusChargingMask) != 0U);
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
    return ibat_to_ma(adc10(r.msbIbat, r.lsbB, kAdcLsbIbatShift), r.ibatStat);
}
int32_t npm1300_read_vsys_mv(void) {
    AdcResults r{}; if (!read_adc_results(&r)) return -1;
    return adc_to_mv(adc10(r.msbVsys, r.lsbA, kAdcLsbVsysShift));
}
int32_t npm1300_read_vbus_mv(void) {
    AdcResults r{}; if (!read_adc_results(&r)) return -1;
    return adc_to_mv(adc10(r.msbVbus, r.lsbB, kAdcLsbVbusShift));
}

bool npm1300_enter_ship_mode(void) {
    return npm1300_write_reg(NPM1300_BASE_SHIP, kShipOffsetShip, 1U);
}
bool npm1300_enter_hibernate(void) {
    return npm1300_write_reg(NPM1300_BASE_SHIP, kShipOffsetHibernate, 1U);
}
bool npm1300_led_set(uint8_t led, uint8_t brightness) {
    if (led >= kLedCount) return false;
    return npm1300_write_reg(NPM1300_BASE_LED, kLedOffsetMode + led, kLedModeHost) &&
           npm1300_write_reg(NPM1300_BASE_LED,
                             static_cast<uint8_t>((brightness ? kLedOffsetSet : kLedOffsetClr) + (led * 2U)),
                             1U);
}
bool npm1300_gpio_set_mode(uint8_t pin, uint8_t mode) {
    if (pin >= kGpioCount || mode > 9) return false;
    return npm1300_write_reg(NPM1300_BASE_GPIO, kGpioOffsetMode + pin, mode);
}

bool npm1300_is_crc_corrupt(void) {
    return g_chargerError & 0x04U;
}
