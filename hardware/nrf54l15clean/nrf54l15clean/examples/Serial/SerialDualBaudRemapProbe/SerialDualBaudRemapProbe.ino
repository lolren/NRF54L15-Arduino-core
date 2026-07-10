/*
 * Regression probe for issue #97: two runtime-remapped UARTs, including a
 * baud rate other than 115200. HardwareSerial::setPins() uses (rx, tx) order.
 */

#include <Arduino.h>

static constexpr uint8_t kSerial1Rx = PIN_SERIAL1_RX;
static constexpr uint8_t kSerial1Tx = PIN_SERIAL1_TX;
static constexpr uint8_t kSerialRx = PIN_D1;
static constexpr uint8_t kSerialTx = PIN_D0;

void setup() {
  const bool serial1RouteOk = Serial1.setPins(kSerial1Rx, kSerial1Tx);
  const bool serialRouteOk = Serial.setPins(kSerialRx, kSerialTx);

  Serial1.begin(115200);
  Serial.begin(9600);

  Serial1.print("Serial1 route: ");
  Serial1.println(serial1RouteOk ? "ok" : "failed");
  Serial.print("Serial route: ");
  Serial.println(serialRouteOk ? "ok" : "failed");
}

void loop() {
  static uint32_t sequence = 0;

  Serial.print("Serial 9600 sequence ");
  Serial.println(sequence);
  Serial1.print("Serial1 115200 sequence ");
  Serial1.println(sequence);

  ++sequence;
  delay(1000);
}
