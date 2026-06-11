/*
 * nPM1300 LM20A Rail Control
 *
 * XIAO nRF54LM20A uses the nPM1300 LDO1 output for the shared
 * IMU&MIC_3V3 rail. This example shows the explicit PMIC sequence:
 *   1. Select LDO function for LDSW1/LDO1.
 *   2. Set output voltage to 3.3 V.
 *   3. Enable the rail while peripherals are needed.
 *   4. Disable the rail again to save current.
 *
 * Output: USB Serial Monitor at 115200 baud.
 */

#include <Arduino.h>
#include "npm1300.h"

static void printPmicStatus(const char* tag) {
  uint8_t vbusStatus = 0;
  const bool vbusOk = npm1300_vbus_status(&vbusStatus);

  Serial.print(tag);
  Serial.print(" ldo1=");
  Serial.print(npm1300_ldo1_is_enabled() ? "on" : "off");
  Serial.print(" vbat_mv=");
  Serial.print(npm1300_read_vbat_mv());
  Serial.print(" vsys_mv=");
  Serial.print(npm1300_read_vsys_mv());
  Serial.print(" vbus_mv=");
  Serial.print(npm1300_read_vbus_mv());
  Serial.print(" vbus_status=");
  Serial.println(vbusOk ? vbusStatus : 0, HEX);
}

static bool enableImuMicRail() {
  return npm1300_ldo1_set_mode(NPM1300_LDSW_MODE_LDO) &&
         npm1300_ldo1_set_voltage(NPM1300_LDO_VOLTAGE_3V3) &&
         npm1300_ldo1_enable(true);
}

void setup() {
  Serial.begin(115200);
  delay(500);

  pinMode(LED_GREEN, OUTPUT);
  digitalWrite(LED_GREEN, HIGH);

  Serial.println("nPM1300 LM20A Rail Control");
  Serial.println("Toggles IMU&MIC_3V3 from the PMIC LDO1 output.");
  npm1300_begin();
  printPmicStatus("boot");
}

void loop() {
  const bool enabled = enableImuMicRail();
  digitalWrite(LED_GREEN, enabled ? LOW : HIGH);
  printPmicStatus(enabled ? "rail_on" : "rail_on_failed");
  delay(2000);

  (void)npm1300_ldo1_enable(false);
  digitalWrite(LED_GREEN, HIGH);
  printPmicStatus("rail_off");
  delay(5000);
}
