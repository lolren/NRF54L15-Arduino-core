#include <Arduino.h>
#include <bluefruit.h>

#if !(defined(ARDUINO_NRF54LM20A) || defined(ARDUINO_NRF54LM20B))
#error "This diagnostic is for the XIAO nRF54LM20A onboard QSPI flash."
#endif

#include <Adafruit_SPIFlash.h>
#include <Adafruit_FlashTransport_QSPI_NRF54.h>

Adafruit_FlashTransport_QSPI_NRF54 flashTransport;
Adafruit_SPIFlash flash(&flashTransport);

static void parkFlashForBleIdle() {
  if (flash.begin()) {
    (void)flash.runCommand(0xB9);  // Deep power-down.
    flash.end();
  }
}

static void startAdv() {
  Bluefruit.Advertising.clearData();
  Bluefruit.ScanResponse.clearData();
  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  Bluefruit.Advertising.addName();
  Bluefruit.Advertising.restartOnDisconnect(true);
  Bluefruit.Advertising.setInterval(160, 1600);
  Bluefruit.Advertising.setFastTimeout(0);
  Bluefruit.Advertising.start(0);
}

void setup() {
  Serial.begin(115200);
  delay(300);

  parkFlashForBleIdle();

  Bluefruit.autoConnLed(false);
  Bluefruit.begin();
  Bluefruit.setName("LM20A-FLASH-SLEEP");
  Bluefruit.setTxPower(0);
  startAdv();

  Serial.println("LM20A flash is in deep power-down while Bluefruit advertises.");
}

void loop() {
  delay(1000);
}
