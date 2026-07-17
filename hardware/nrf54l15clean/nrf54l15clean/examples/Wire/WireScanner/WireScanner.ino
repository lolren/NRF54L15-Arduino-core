/*
 * WireScanner
 *
 * Scans Wire and Wire1 across the standard 7-bit I2C address range once
 * every five seconds.
 * endTransmission() returns zero only after a target acknowledges a real bus
 * address phase; an empty bus must report found=0.
 */

#include <Arduino.h>
#include <Wire.h>

#if defined(ARDUINO_NRF54LM20A) || defined(ARDUINO_NRF54LM20B) || \
    defined(ARDUINO_XIAO_NRF54L15_CLEAN)
#include "nrf54l15_hal.h"
using xiao_nrf54l15::BoardControl;
#endif

static void scanBus(TwoWire& bus, const char* name, uint8_t sda, uint8_t scl) {
  // A bus without pull-ups cannot generate STOP after an address NACK. Avoid
  // turning that electrical fault into 126 transaction timeouts.
  if (digitalRead(sda) == LOW || digitalRead(scl) == LOW) {
    Serial.print(name);
    Serial.println(" unavailable: SDA/SCL not idle high; check pull-ups and power");
    return;
  }

  uint8_t found = 0U;
  uint8_t errors = 0U;
  for (uint8_t address = 0x01U; address <= 0x7EU; ++address) {
    bus.beginTransmission(address);
    const uint8_t status = bus.endTransmission();
    if (status == 0U) {
      Serial.print(name);
      Serial.print(" device at 0x");
      if (address < 0x10U) {
        Serial.print('0');
      }
      Serial.println(address, HEX);
      ++found;
    } else if (status != 2U) {
      // Status 2 is the expected address NACK for an unused address. Other
      // statuses usually indicate missing pull-ups or a stuck bus.
      ++errors;
    }
  }
  Serial.print(name);
  Serial.print(" scan done found=");
  Serial.print(found);
  Serial.print(" errors=");
  Serial.println(errors);
}

void setup() {
  Serial.begin(115200);
  delay(300);

#if defined(ARDUINO_NRF54LM20A) || defined(ARDUINO_NRF54LM20B)
  const bool sensorPower = BoardControl::setImuMicEnabled(true);
  pinMode(PIN_IMU_CS, OUTPUT);
  digitalWrite(PIN_IMU_CS, HIGH);
  delay(25);
  Serial.print("LM20A Wire1 sensor rail=");
  Serial.println(sensorPower ? "on" : "failed");
#elif defined(ARDUINO_XIAO_NRF54L15_CLEAN)
  const bool sensorPower = BoardControl::setImuMicEnabled(true);
  delay(10);
  Serial.print("L15 Sense Wire1 sensor rail=");
  Serial.println(sensorPower ? "on" : "failed");
#endif

  Wire.begin();
  Wire.setClock(100000UL);
  Wire1.begin();
  Wire1.setClock(100000UL);
  Serial.println("WireScanner start");
  scanBus(Wire, "Wire", SDA, SCL);
  scanBus(Wire1, "Wire1", SDA1, SCL1);
}

void loop() {
  delay(5000);
  scanBus(Wire, "Wire", SDA, SCL);
  scanBus(Wire1, "Wire1", SDA1, SCL1);
}
