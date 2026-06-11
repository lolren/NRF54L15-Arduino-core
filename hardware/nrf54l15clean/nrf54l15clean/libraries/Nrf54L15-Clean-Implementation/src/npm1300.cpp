/*
 * nPM1300 PMIC driver implementation.
 *
 * Uses Arduino Wire transactions with the same {base, offset} register
 * addressing convention as Zephyr's nPM13xx MFD driver. On boards without
 * PIN_PMIC_SDA/PIN_PMIC_SCL the public API compiles but returns failure.
 */

#include "npm1300.h"

#include <Arduino.h>
#include <Wire.h>

namespace {

#if defined(PIN_PMIC_SDA) && defined(PIN_PMIC_SCL) && defined(NRF_TWIM21)
#define NPM1300_HAS_BOARD_PMIC 1
TwoWire g_pmicWire(NRF_TWIM21, PIN_PMIC_SDA, PIN_PMIC_SCL);
#else
#define NPM1300_HAS_BOARD_PMIC 0
#endif

constexpr uint8_t kMainOffsetVersion = 0x26U;

constexpr uint8_t kVbusOffsetStatus = 0x07U;

constexpr uint8_t kChargerOffsetErrClr = 0x00U;
constexpr uint8_t kChargerOffsetEnSet = 0x04U;
constexpr uint8_t kChargerOffsetEnClr = 0x05U;
constexpr uint8_t kChargerOffsetISet = 0x08U;
constexpr uint8_t kChargerOffsetITermVoltage = 0x0CU;
constexpr uint8_t kChargerOffsetStatus = 0x34U;
constexpr uint8_t kChargerOffsetError = 0x36U;

constexpr uint8_t kBuckOffsetEnSet = 0x00U;
constexpr uint8_t kBuckOffsetEnClr = 0x01U;
constexpr uint8_t kBuckOffsetVoutNorm = 0x08U;
constexpr uint8_t kBuckOffsetSwCtrl = 0x0FU;
constexpr uint8_t kBuckOffsetStatus = 0x34U;
constexpr uint8_t kBuck1OnMask = 0x04U;
constexpr uint8_t kBuck2OnMask = 0x40U;

constexpr uint8_t kAdcOffsetTaskVbat = 0x00U;
constexpr uint8_t kAdcOffsetConfig = 0x09U;
constexpr uint8_t kAdcOffsetTaskAuto = 0x0CU;
constexpr uint8_t kAdcOffsetResults = 0x10U;
constexpr uint8_t kAdcOffsetIbatEnable = 0x24U;
constexpr uint32_t kAdcConversionUs = 1000UL;
constexpr uint32_t kAdcCacheWindowMs = 25UL;

constexpr uint8_t kLdSwOffsetEnSet = 0x00U;
constexpr uint8_t kLdSwOffsetEnClr = 0x01U;
constexpr uint8_t kLdSwOffsetStatus = 0x04U;
constexpr uint8_t kLdSwOffsetLdoSel = 0x08U;
constexpr uint8_t kLdSwOffsetVoutSel = 0x0CU;
constexpr uint8_t kLdSw1OnMask = 0x03U;
constexpr uint8_t kLdSw2OnMask = 0x0CU;

constexpr uint8_t kGpioOffsetMode = 0x00U;
constexpr uint8_t kGpioOffsetStatus = 0x1EU;
constexpr uint8_t kGpioCount = 5U;

constexpr uint8_t kLedOffsetMode = 0x00U;
constexpr uint8_t kLedOffsetSet = 0x03U;
constexpr uint8_t kLedOffsetClr = 0x04U;
constexpr uint8_t kLedHostMode = 2U;
constexpr uint8_t kLedCount = 3U;

constexpr uint8_t kShipOffsetHibernate = 0x00U;
constexpr uint8_t kShipOffsetShip = 0x02U;

constexpr uint8_t kAdcMsbShift = 2U;
constexpr uint8_t kAdcLsbMask = 0x03U;
constexpr uint8_t kAdcLsbVbatShift = 0U;
constexpr uint8_t kAdcLsbNtcShift = 2U;
constexpr uint8_t kAdcLsbDieShift = 4U;
constexpr uint8_t kAdcLsbVsysShift = 6U;
constexpr uint8_t kAdcLsbIbatShift = 4U;
constexpr uint8_t kAdcLsbVbusShift = 6U;

constexpr uint8_t kIbatStatDischarge = 0x04U;
constexpr uint8_t kIbatStatChargeTrickle = 0x0CU;
constexpr uint8_t kIbatStatChargeCool = 0x0DU;
constexpr uint8_t kIbatStatChargeNormal = 0x0FU;

constexpr int32_t kDieTempOffsetMilliC = 394670L;
constexpr int32_t kDieTempFactorMul = 3963000L;
constexpr int32_t kDieTempFactorDiv = 5000L;

struct AdcResults {
  uint8_t ibatStat;
  uint8_t msbVbat;
  uint8_t msbNtc;
  uint8_t msbDie;
  uint8_t msbVsys;
  uint8_t lsbA;
  uint8_t reserved1;
  uint8_t reserved2;
  uint8_t msbIbat;
  uint8_t msbVbus;
  uint8_t lsbB;
};

static_assert(sizeof(AdcResults) == 11, "nPM1300 ADC result layout changed");

bool g_busStarted = false;
bool g_probeValid = false;
bool g_present = false;
bool g_adcValid = false;
uint32_t g_adcCachedMs = 0;
AdcResults g_adcCache{};
uint8_t g_chargerStatus = 0;
uint8_t g_chargerError = 0;
uint8_t g_vbusStatus = 0;
int32_t g_chargeCurrentUa = 32000;
int32_t g_dischargeLimitUa = 84000;

bool pmic_bus_begin() {
#if NPM1300_HAS_BOARD_PMIC
  if (!g_busStarted) {
    g_pmicWire.begin();
    g_pmicWire.setClock(400000UL);
    g_busStarted = true;
  }
  return true;
#else
  return false;
#endif
}

uint8_t clamp_channel(uint8_t channel) {
  return channel == 0U ? 0U : 1U;
}

bool voltage_to_index(uint16_t mv, uint8_t* index) {
  if (index == nullptr || mv < 1000U || mv > 3300U) {
    return false;
  }

  *index = static_cast<uint8_t>((mv - 1000U + 99U) / 100U);
  if (*index > 23U) {
    *index = 23U;
  }
  return true;
}

uint16_t adc10(uint8_t msb, uint8_t lsb, uint8_t shift) {
  return (static_cast<uint16_t>(msb) << kAdcMsbShift) |
         ((lsb >> shift) & kAdcLsbMask);
}

int32_t adc_to_mv(uint16_t code) {
  return static_cast<int32_t>((static_cast<uint32_t>(code) * 5000UL + 512UL) /
                              1024UL);
}

bool is_charge_stat(uint8_t stat) {
  return stat == kIbatStatChargeTrickle || stat == kIbatStatChargeCool ||
         stat == kIbatStatChargeNormal;
}

bool read_adc_results(AdcResults* out) {
  if (out == nullptr) {
    return false;
  }

  const uint32_t now = millis();
  if (g_adcValid && (now - g_adcCachedMs) <= kAdcCacheWindowMs) {
    *out = g_adcCache;
    return true;
  }

  const uint8_t tasks[] = {1U, 1U, 1U};
  if (!npm1300_write_burst(NPM1300_BASE_ADC, kAdcOffsetTaskVbat, tasks,
                           sizeof(tasks))) {
    return false;
  }

  (void)npm1300_read_reg(NPM1300_BASE_CHARGER, kChargerOffsetStatus,
                         &g_chargerStatus);
  (void)npm1300_read_reg(NPM1300_BASE_CHARGER, kChargerOffsetError,
                         &g_chargerError);
  (void)npm1300_read_reg(NPM1300_BASE_VBUS, kVbusOffsetStatus, &g_vbusStatus);

  delayMicroseconds(kAdcConversionUs);

  AdcResults results{};
  if (!npm1300_read_burst(NPM1300_BASE_ADC, kAdcOffsetResults,
                          reinterpret_cast<uint8_t*>(&results),
                          sizeof(results))) {
    return false;
  }

  g_adcCache = results;
  g_adcCachedMs = millis();
  g_adcValid = true;
  *out = results;
  return true;
}

bool ldo_enable(uint8_t channel, bool enable) {
  channel = clamp_channel(channel);
  const uint8_t offset =
      static_cast<uint8_t>((enable ? kLdSwOffsetEnSet : kLdSwOffsetEnClr) +
                           (channel * 2U));
  return npm1300_write_reg(NPM1300_BASE_LDSW, offset, 1U);
}

bool ldo_set_voltage(uint8_t channel, uint16_t mv) {
  uint8_t index = 0;
  if (!voltage_to_index(mv, &index)) {
    return false;
  }
  channel = clamp_channel(channel);
  return npm1300_write_reg(NPM1300_BASE_LDSW,
                           static_cast<uint8_t>(kLdSwOffsetVoutSel + channel),
                           index);
}

bool ldo_is_enabled(uint8_t channel) {
  uint8_t status = 0;
  if (!npm1300_read_reg(NPM1300_BASE_LDSW, kLdSwOffsetStatus, &status)) {
    return false;
  }
  return (status & (channel == 0U ? kLdSw1OnMask : kLdSw2OnMask)) != 0U;
}

bool buck_enable(uint8_t channel, bool enable) {
  channel = clamp_channel(channel);
  const uint8_t offset =
      static_cast<uint8_t>((enable ? kBuckOffsetEnSet : kBuckOffsetEnClr) +
                           (channel * 2U));
  return npm1300_write_reg(NPM1300_BASE_BUCK, offset, 1U);
}

bool buck_set_voltage(uint8_t channel, uint16_t mv) {
  uint8_t index = 0;
  if (!voltage_to_index(mv, &index)) {
    return false;
  }
  channel = clamp_channel(channel);
  const uint8_t swMask = static_cast<uint8_t>(1U << channel);
  return npm1300_write_reg(NPM1300_BASE_BUCK,
                           static_cast<uint8_t>(kBuckOffsetVoutNorm +
                                                (channel * 2U)),
                           index) &&
         npm1300_update_reg(NPM1300_BASE_BUCK, kBuckOffsetSwCtrl, swMask,
                            swMask);
}

bool buck_is_enabled(uint8_t channel) {
  uint8_t status = 0;
  if (!npm1300_read_reg(NPM1300_BASE_BUCK, kBuckOffsetStatus, &status)) {
    return false;
  }
  return (status & (channel == 0U ? kBuck1OnMask : kBuck2OnMask)) != 0U;
}

// Buck PWM/hysteretic mode control.
// BUCK1PWMSET = {BUCK,0x04}, BUCK2PWMSET = {BUCK,0x06}
// BUCK1PWMCLR = {BUCK,0x05}, BUCK2PWMCLR = {BUCK,0x07}
// BUCKCTRL0   = {BUCK,0x0A} — bits 0/1 = force hysteretic buck1/buck2
constexpr uint8_t kBuckOffsetPwmSet = 0x04U;
constexpr uint8_t kBuckOffsetPwmClr = 0x05U;
constexpr uint8_t kBuckOffsetCtrl0  = 0x0AU;

bool buck_set_mode(uint8_t channel, uint8_t mode) {
  channel = clamp_channel(channel);

  // Always clear PWM first (move to AUTO)
  npm1300_write_reg(NPM1300_BASE_BUCK,
                     static_cast<uint8_t>(kBuckOffsetPwmClr + (channel * 2U)), 1U);

  if (mode == NPM1300_BUCK_MODE_FORCE_PWM) {
    return npm1300_write_reg(NPM1300_BASE_BUCK,
                              static_cast<uint8_t>(kBuckOffsetPwmSet + (channel * 2U)), 1U);
  }

  if (mode == NPM1300_BUCK_MODE_FORCE_HYST) {
    return npm1300_update_reg(NPM1300_BASE_BUCK, kBuckOffsetCtrl0,
                               static_cast<uint8_t>(1U << channel),
                               static_cast<uint8_t>(1U << channel));
  }

  // AUTO: clear hysteretic force bit
  npm1300_update_reg(NPM1300_BASE_BUCK, kBuckOffsetCtrl0,
                      static_cast<uint8_t>(1U << channel), 0U);
  return true;
}

}  // namespace

bool npm1300_read_burst(uint8_t base, uint8_t offset, uint8_t* data, size_t len) {
  if (data == nullptr || len == 0U || len > BUFFER_LENGTH || !pmic_bus_begin()) {
    return false;
  }

#if NPM1300_HAS_BOARD_PMIC
  g_pmicWire.beginTransmission(NPM1300_ADDR);
  g_pmicWire.write(base);
  g_pmicWire.write(offset);
  if (g_pmicWire.endTransmission(false) != 0U) {
    return false;
  }

  const uint8_t received =
      g_pmicWire.requestFrom(static_cast<uint8_t>(NPM1300_ADDR), len, true);
  if (received != len) {
    while (g_pmicWire.available() > 0) {
      (void)g_pmicWire.read();
    }
    return false;
  }

  for (size_t i = 0; i < len; ++i) {
    const int value = g_pmicWire.read();
    if (value < 0) {
      g_pmicWire.end();
      return false;
    }
    data[i] = static_cast<uint8_t>(value);
  }
  g_pmicWire.end();
  return true;
#else
  (void)base;
  (void)offset;
  return false;
#endif
}

bool npm1300_write_burst(uint8_t base, uint8_t offset, const uint8_t* data,
                         size_t len) {
  if (len > (BUFFER_LENGTH - 2U) || !pmic_bus_begin()) {
    return false;
  }
  if (len > 0U && data == nullptr) {
    return false;
  }

#if NPM1300_HAS_BOARD_PMIC
  g_pmicWire.beginTransmission(NPM1300_ADDR);
  g_pmicWire.write(base);
  g_pmicWire.write(offset);
  for (size_t i = 0; i < len; ++i) {
    g_pmicWire.write(data[i]);
  }
  const bool ok = (g_pmicWire.endTransmission(true) == 0U);
  g_pmicWire.end();
  return ok;
#else
  (void)base;
  (void)offset;
  (void)data;
  return false;
#endif
}

bool npm1300_read_reg(uint8_t base, uint8_t offset, uint8_t* value) {
  return npm1300_read_burst(base, offset, value, 1U);
}

bool npm1300_write_reg(uint8_t base, uint8_t offset, uint8_t value) {
  return npm1300_write_burst(base, offset, &value, 1U);
}

bool npm1300_update_reg(uint8_t base, uint8_t offset, uint8_t mask,
                        uint8_t value) {
  uint8_t current = 0;
  if (!npm1300_read_reg(base, offset, &current)) {
    return false;
  }
  current = static_cast<uint8_t>((current & ~mask) | (value & mask));
  return npm1300_write_reg(base, offset, current);
}

void npm1300_begin(void) {
  uint8_t revision = 0;
  g_present = npm1300_read_reg(NPM1300_BASE_MAIN, kMainOffsetVersion, &revision);
  g_probeValid = true;
  if (g_present) {
    (void)npm1300_write_reg(NPM1300_BASE_ADC, kAdcOffsetIbatEnable, 1U);
    (void)npm1300_write_reg(NPM1300_BASE_ADC, kAdcOffsetTaskAuto, 1U);
    (void)npm1300_write_reg(NPM1300_BASE_ADC, kAdcOffsetConfig, 0U);
  }
}

void npm1300_init(void) {
  npm1300_begin();
}

bool npm1300_is_present(void) {
  if (!g_probeValid) {
    npm1300_begin();
  }
  return g_present;
}

bool npm1300_ldo1_enable(bool enable) {
  return ldo_enable(0U, enable);
}

bool npm1300_ldo1_set_voltage(uint16_t mv) {
  return ldo_set_voltage(0U, mv);
}

bool npm1300_ldo1_is_enabled(void) {
  return ldo_is_enabled(0U);
}

bool npm1300_ldo2_enable(bool enable) {
  return ldo_enable(1U, enable);
}

bool npm1300_ldo2_set_voltage(uint16_t mv) {
  return ldo_set_voltage(1U, mv);
}

bool npm1300_ldo2_is_enabled(void) {
  return ldo_is_enabled(1U);
}

bool npm1300_buck1_enable(bool enable) {
  return buck_enable(0U, enable);
}

bool npm1300_buck1_set_voltage(uint16_t mv) {
  return buck_set_voltage(0U, mv);
}

bool npm1300_buck1_is_enabled(void) {
  return buck_is_enabled(0U);
}

bool npm1300_buck2_enable(bool enable) {
  return buck_enable(1U, enable);
}

bool npm1300_buck2_set_voltage(uint16_t mv) {
  return buck_set_voltage(1U, mv);
}

bool npm1300_buck2_is_enabled(void) {
  return buck_is_enabled(1U);
}

bool npm1300_charger_enable(bool enable) {
  if (enable) {
    (void)npm1300_write_reg(NPM1300_BASE_CHARGER, kChargerOffsetErrClr, 1U);
  }
  return npm1300_write_reg(NPM1300_BASE_CHARGER,
                           enable ? kChargerOffsetEnSet : kChargerOffsetEnClr,
                           1U);
}

bool npm1300_charger_set_current(uint16_t ma) {
  if (ma < 32U || ma > 800U) {
    return false;
  }

  const uint16_t idx =
      static_cast<uint16_t>(((static_cast<uint32_t>(ma) * 1000UL) - 32000UL +
                             1999UL) /
                            2000UL +
                            16UL);
  const uint8_t payload[] = {
      static_cast<uint8_t>(idx / 2U),
      static_cast<uint8_t>(idx & 1U),
  };

  if (!npm1300_write_burst(NPM1300_BASE_CHARGER, kChargerOffsetISet, payload,
                           sizeof(payload))) {
    return false;
  }
  g_chargeCurrentUa = static_cast<int32_t>(ma) * 1000L;
  return true;
}

bool npm1300_charger_set_term_voltage(uint16_t mv) {
  uint8_t idx = 0;
  if (mv >= 3500U && mv <= 3650U) {
    idx = static_cast<uint8_t>((mv - 3500U) / 50U);
  } else if (mv >= 4000U && mv <= 4450U) {
    idx = static_cast<uint8_t>(4U + ((mv - 4000U) / 50U));
  } else {
    return false;
  }
  return npm1300_write_reg(NPM1300_BASE_CHARGER, kChargerOffsetITermVoltage,
                           idx);
}

bool npm1300_charger_is_charging(void) {
  uint8_t status = 0;
  if (!npm1300_charger_status(&status)) {
    return false;
  }
  return status != 0U;
}

bool npm1300_charger_status(uint8_t* status) {
  if (status == nullptr) {
    return false;
  }
  if (npm1300_read_reg(NPM1300_BASE_CHARGER, kChargerOffsetStatus, status)) {
    g_chargerStatus = *status;
    return true;
  }
  return false;
}

bool npm1300_charger_error(uint8_t* error) {
  if (error == nullptr) {
    return false;
  }
  if (npm1300_read_reg(NPM1300_BASE_CHARGER, kChargerOffsetError, error)) {
    g_chargerError = *error;
    return true;
  }
  return false;
}

bool npm1300_vbus_status(uint8_t* status) {
  if (status == nullptr) {
    return false;
  }
  if (npm1300_read_reg(NPM1300_BASE_VBUS, kVbusOffsetStatus, status)) {
    g_vbusStatus = *status;
    return true;
  }
  return false;
}

int32_t npm1300_read_vbat_mv(void) {
  AdcResults results{};
  if (!read_adc_results(&results)) {
    return -1;
  }
  return adc_to_mv(adc10(results.msbVbat, results.lsbA, kAdcLsbVbatShift));
}

int32_t npm1300_read_temp_mc(void) {
  AdcResults results{};
  if (!read_adc_results(&results)) {
    return -1;
  }
  const uint16_t code = adc10(results.msbDie, results.lsbA, kAdcLsbDieShift);
  return kDieTempOffsetMilliC -
         ((static_cast<int32_t>(code) * kDieTempFactorMul) / kDieTempFactorDiv);
}

int32_t npm1300_read_ibat_ma(void) {
  AdcResults results{};
  if (!read_adc_results(&results)) {
    return -1;
  }

  const uint16_t code = adc10(results.msbIbat, results.lsbB, kAdcLsbIbatShift);
  int32_t fullScaleUa = 0;
  if (results.ibatStat == kIbatStatDischarge) {
    fullScaleUa = -(g_dischargeLimitUa * 112L) / 100L;
  } else if (is_charge_stat(results.ibatStat)) {
    fullScaleUa = (g_chargeCurrentUa * 125L) / 100L;
  } else {
    return 0;
  }

  const int32_t currentUa =
      (static_cast<int32_t>(code) * fullScaleUa) / 1023L;
  return currentUa / 1000L;
}

int32_t npm1300_read_vsys_mv(void) {
  return -1;
}

int32_t npm1300_read_vbus_mv(void) {
  return -1;
}

bool npm1300_enter_ship_mode(void) {
  return npm1300_write_reg(NPM1300_BASE_SHIP, kShipOffsetShip, 1U);
}

bool npm1300_enter_hibernate(void) {
  return npm1300_write_reg(NPM1300_BASE_SHIP, kShipOffsetHibernate, 1U);
}

bool npm1300_led_set(uint8_t led, uint8_t brightness) {
  if (led >= kLedCount) {
    return false;
  }

  return npm1300_write_reg(NPM1300_BASE_LED,
                           static_cast<uint8_t>(kLedOffsetMode + led),
                           kLedHostMode) &&
         npm1300_write_reg(
             NPM1300_BASE_LED,
             static_cast<uint8_t>((brightness > 0U ? kLedOffsetSet
                                                   : kLedOffsetClr) +
                                  (led * 2U)),
             1U);
}

bool npm1300_gpio_set_mode(uint8_t pin, uint8_t mode) {
  if (pin >= kGpioCount || mode > NPM1300_GPIO_GPOLOGIC0) {
    return false;
  }
  return npm1300_write_reg(NPM1300_BASE_GPIO,
                           static_cast<uint8_t>(kGpioOffsetMode + pin), mode);
}

bool npm1300_gpio_write(uint8_t pin, bool high) {
  return npm1300_gpio_set_mode(pin, high ? NPM1300_GPIO_GPOLOGIC1
                                         : NPM1300_GPIO_GPOLOGIC0);
}

bool npm1300_gpio_status(uint8_t* status) {
  return npm1300_read_reg(NPM1300_BASE_GPIO, kGpioOffsetStatus, status);
}

bool npm1300_buck1_set_mode(uint8_t mode) {
  return buck_set_mode(0U, mode);
}

bool npm1300_buck2_set_mode(uint8_t mode) {
  return buck_set_mode(1U, mode);
}
