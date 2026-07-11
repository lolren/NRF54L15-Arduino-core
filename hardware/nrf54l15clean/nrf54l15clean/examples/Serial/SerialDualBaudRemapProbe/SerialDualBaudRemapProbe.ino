/*
 * Regression probe for issue #97: two runtime-remapped UARTs, including a
 * baud rate other than 115200. HardwareSerial::setPins() uses (rx, tx) order.
 */

#include <Arduino.h>

#if defined(ARDUINO_NRF54L15DK_PCA10156)
static_assert(PIN_SERIAL1_TX == PIN_P1_02,
              "PCA10156 Serial1 TX must use the independent UARTE21 route");
static_assert(PIN_SERIAL1_RX == PIN_P1_03,
              "PCA10156 Serial1 RX must use the independent UARTE21 route");
static_assert(PIN_SERIAL_TX != PIN_SERIAL1_TX || PIN_SERIAL_RX != PIN_SERIAL1_RX,
              "PCA10156 Serial and Serial1 must not share their default pins");
#endif

#if defined(ARDUINO_HOLYIOT_25008_NRF54L15)
static constexpr uint8_t kSerial1Rx = PIN_D17;
static constexpr uint8_t kSerial1Tx = PIN_D16;
#else
static constexpr uint8_t kSerial1Rx = PIN_SERIAL1_RX;
static constexpr uint8_t kSerial1Tx = PIN_SERIAL1_TX;
#endif
static constexpr uint8_t kSerialRx = PIN_D1;
static constexpr uint8_t kSerialTx = PIN_D0;

static_assert(kSerial1Rx != kSerialRx || kSerial1Tx != kSerialTx,
              "SerialDualBaudRemapProbe requires two independent UART routes");

void setup() {
  const bool serial1RouteOk = Serial1.setPins(kSerial1Rx, kSerial1Tx);
  const bool serialRouteOk = Serial.setPins(kSerialRx, kSerialTx);

  Serial1.begin(115200);
  Serial.begin(9600);

  Serial1.print("Serial1 route: ");
  Serial1.println(serial1RouteOk ? "ok" : "failed");
  Serial.print("Serial route: ");
  Serial.println(serialRouteOk ? "ok" : "failed");
  Serial.print("Serial1 configured: ");
  Serial.println(Serial1.isConfigured() ? "yes" : "no");
#if defined(ARDUINO_NRF54L15DK_PCA10156)
  const bool nfcPadsReleased =
      ((NRF_NFCT->PADCONFIG & NFCT_PADCONFIG_ENABLE_Msk) >>
       NFCT_PADCONFIG_ENABLE_Pos) == NFCT_PADCONFIG_ENABLE_Disabled;
  Serial.print("NFC pads in GPIO mode: ");
  Serial.println(nfcPadsReleased ? "yes" : "no");
#endif
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
