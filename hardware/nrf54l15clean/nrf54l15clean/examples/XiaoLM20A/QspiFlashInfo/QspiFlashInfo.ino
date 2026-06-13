/*
  QspiFlashInfo

  Reads the JEDEC ID and status register from the onboard PY25Q64 QSPI flash
  on XIAO nRF54LM20A. The flash is connected to dedicated P2 HS-SPI/QSPI pins,
  not to the standard XIAO SPI header.
*/

#include <Arduino.h>

#if !(defined(ARDUINO_NRF54LM20A) || defined(ARDUINO_NRF54LM20B))
#error "QspiFlashInfo requires the XIAO nRF54LM20A board."
#endif

#include <XiaoNrf54lm20QspiFlash.h>

static void printHexByte(uint8_t value) {
  if (value < 0x10) {
    Serial.print('0');
  }
  Serial.print(value, HEX);
}

static void probeFlashOnce() {
  uint8_t jedec[3] = {0, 0, 0};
  if (XiaoQspiFlash.begin() && XiaoQspiFlash.readJedecId(jedec)) {
    Serial.print("JEDEC ID: ");
    printHexByte(jedec[0]);
    Serial.print(' ');
    printHexByte(jedec[1]);
    Serial.print(' ');
    printHexByte(jedec[2]);
    Serial.println();
  } else {
    Serial.println("JEDEC ID read failed");
  }

  uint8_t status = 0;
  if (XiaoQspiFlash.readStatus(&status)) {
    Serial.print("Status-1: 0x");
    printHexByte(status);
    Serial.println();
  }

  uint8_t firstBytes[16] = {0};
  if (XiaoQspiFlash.read(0, firstBytes, sizeof(firstBytes))) {
    Serial.print("First 16 bytes:");
    for (uint8_t value : firstBytes) {
      Serial.print(' ');
      printHexByte(value);
    }
    Serial.println();
  }

  XiaoQspiFlash.prepareForSleep();
  Serial.println("Flash is now in deep power-down.");
}

void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println();
  Serial.println("XIAO nRF54LM20A QSPI flash info");
  Serial.println("Repeats every 5 seconds and powers the flash down after each read.");
}

void loop() {
  probeFlashOnce();
  Serial.println();
  delay(5000);
}
