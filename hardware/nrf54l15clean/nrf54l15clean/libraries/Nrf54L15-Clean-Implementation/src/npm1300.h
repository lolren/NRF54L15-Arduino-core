/*
 * nPM1300 PMIC Driver for nRF54L Series
 * 
 * Full-featured driver for the Nordic nPM1300 Power Management IC.
 * Controls LDOs, battery charger, ship/hibernate modes, GPIOs, LEDs,
 * and system monitoring for ultra-low power applications.
 *
 * I2C: Address 0x6B, SDA=P1.18, SCL=P1.17
 *
 * Register Map:
 *   0x0000: SHIP/HIBERNATE control
 *   0x0500: System Monitor (VBAT, temperature, current measurements)
 *   0x0600: Battery Charger
 *   0x0700: LED Drivers
 *   0x0800: Load Switches / LDOs
 *   0x0900: GPIO
 *   0x0A00: Fuel Gauge / Timers
 *   0x0B00: Events/Interrupts
 *   0x0E00: Error Log
 */

#ifndef NPM1300_H
#define NPM1300_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ─── I2C Address ─────────────────────────────────────────────
#define NPM1300_ADDR            0x6B

// ─── Register Base Addresses ─────────────────────────────────
#define NPM1300_BASE_SHIP       0x0000  // Ship/Hibernate mode
#define NPM1300_BASE_SYSMON     0x0500  // System Monitor
#define NPM1300_BASE_CHARGER    0x0600  // Battery Charger
#define NPM1300_BASE_LED        0x0700  // LED Drivers
#define NPM1300_BASE_LDSW       0x0800  // Load Switches / LDOs
#define NPM1300_BASE_GPIO       0x0900  // GPIO
#define NPM1300_BASE_TIMER      0x0A00  // Fuel Gauge / Timers
#define NPM1300_BASE_EVENT      0x0B00  // Event control

// ─── SHIP/HIBERNATE Registers (0x0000) ───────────────────────
#define NPM1300_TASK_ENTER_SHIP      0x0000
#define NPM1300_TASK_ENTER_HIBERNATE 0x0004
#define NPM1300_TASK_EXIT_HIBERNATE  0x0008
#define NPM1300_SHIP_CONFIG          0x0028

// ─── System Monitor Registers (0x0500) ───────────────────────
#define NPM1300_TASK_VBAT_MEASURE    0x0504
#define NPM1300_TASK_TEMP_MEASURE    0x050C
#define NPM1300_TASK_VSYS_MEASURE    0x0514
#define NPM1300_TASK_IBAT_MEASURE    0x051C
#define NPM1300_TASK_VBUS_MEASURE    0x0524
#define NPM1300_SYSMON_VBAT_RESULT   0x0540
#define NPM1300_SYSMON_TEMP_RESULT   0x0544
#define NPM1300_SYSMON_VSYS_RESULT   0x0548
#define NPM1300_SYSMON_IBAT_RESULT   0x054C
#define NPM1300_SYSMON_VBUS_RESULT   0x0550

// ─── Charger Registers (0x0600) ──────────────────────────────
#define NPM1300_CHARGER_CONFIG       0x0600
#define NPM1300_CHARGER_ICHARGE      0x0604
#define NPM1300_CHARGER_VTERM        0x0608
#define NPM1300_CHARGER_STATUS       0x060C
#define NPM1300_CHARGER_ENABLE       0x0610
#define NPM1300_CHARGER_DISABLE      0x0614
#define NPM1300_CHARGER_VBUS_LIMIT   0x0618
#define NPM1300_CHARGER_DISCHARGE    0x061C

// ─── Load Switch / LDO Registers (0x0800) ────────────────────
#define NPM1300_TASK_LDSW1_SET       0x0800
#define NPM1300_TASK_LDSW1_CLR       0x0804
#define NPM1300_TASK_LDSW2_SET       0x0810
#define NPM1300_TASK_LDSW2_CLR       0x0814
#define NPM1300_LDSW1_STATUS         0x0808
#define NPM1300_LDSW1_LDOSEL         0x080C
#define NPM1300_LDSW1_VOUTSEL        0x0820
#define NPM1300_LDSW2_STATUS         0x0818
#define NPM1300_LDSW2_LDOSEL         0x081C
#define NPM1300_LDSW2_VOUTSEL        0x0824

// ─── LDO Voltage Options (mV) ────────────────────────────────
#define NPM1300_LDO_VOLTAGE_1V0      1000
#define NPM1300_LDO_VOLTAGE_1V1      1100
#define NPM1300_LDO_VOLTAGE_1V2      1200
#define NPM1300_LDO_VOLTAGE_1V3      1300
#define NPM1300_LDO_VOLTAGE_1V4      1400
#define NPM1300_LDO_VOLTAGE_1V5      1500
#define NPM1300_LDO_VOLTAGE_1V6      1600
#define NPM1300_LDO_VOLTAGE_1V7      1700
#define NPM1300_LDO_VOLTAGE_1V8      1800
#define NPM1300_LDO_VOLTAGE_2V0      2000
#define NPM1300_LDO_VOLTAGE_2V1      2100
#define NPM1300_LDO_VOLTAGE_2V5      2500
#define NPM1300_LDO_VOLTAGE_2V7      2700
#define NPM1300_LDO_VOLTAGE_3V0      3000
#define NPM1300_LDO_VOLTAGE_3V3      3300

// ─── Charger Current Options (mA) ────────────────────────────
#define NPM1300_CHARGE_CURRENT_20MA   20
#define NPM1300_CHARGE_CURRENT_50MA   50
#define NPM1300_CHARGE_CURRENT_100MA  100
#define NPM1300_CHARGE_CURRENT_150MA  150
#define NPM1300_CHARGE_CURRENT_200MA  200
#define NPM1300_CHARGE_CURRENT_300MA  300
#define NPM1300_CHARGE_CURRENT_400MA  400
#define NPM1300_CHARGE_CURRENT_500MA  500
#define NPM1300_CHARGE_CURRENT_800MA  800

// ─── nPM1300 Public API ─────────────────────────────────────

/* Initialize PMIC I2C and configure defaults for low-power operation. */
void npm1300_begin(void);

/* Enable/disable LDO1 (IMU/MIC power). Default: 1.8V. */
bool npm1300_ldo1_enable(bool enable);
bool npm1300_ldo1_set_voltage(uint16_t mv);
bool npm1300_ldo1_is_enabled(void);

/* Enable/disable LDO2 (second sensor rail). */
bool npm1300_ldo2_enable(bool enable);
bool npm1300_ldo2_set_voltage(uint16_t mv);

/* Configure battery charger. */
bool npm1300_charger_enable(bool enable);
bool npm1300_charger_set_current(uint16_t ma);
bool npm1300_charger_set_term_voltage(uint16_t mv);
bool npm1300_charger_is_charging(void);

/* Read system measurements. Returns value in specified units, or -1 on error. */
int32_t npm1300_read_vbat_mv(void);     // Battery voltage in mV
int32_t npm1300_read_temp_mc(void);     // Temperature in millicelsius
int32_t npm1300_read_ibat_ma(void);     // Battery current in mA
int32_t npm1300_read_vsys_mv(void);     // System voltage in mV
int32_t npm1300_read_vbus_mv(void);     // USB voltage in mV

/* Ultra-low power modes. */
bool npm1300_enter_ship_mode(void);     // Ship mode — lowest power, GPIO wake only
bool npm1300_enter_hibernate(void);     // Hibernate — timer/GPIO wake

/* LED driver control. */
bool npm1300_led_set(uint8_t led, uint8_t brightness);  // 0=off, 255=max

#ifdef __cplusplus
}
#endif
#endif
