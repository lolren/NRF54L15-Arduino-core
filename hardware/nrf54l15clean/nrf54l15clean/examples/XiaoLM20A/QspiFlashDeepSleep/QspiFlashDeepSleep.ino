/*
  QspiFlashDeepSleep

  XIAO nRF54LM20A has an onboard PY25Q64 flash on dedicated P2 HS-SPI/QSPI
  pins. For low-current sleep measurements, put that flash into deep
  power-down before sleeping; otherwise the board current can stay higher than
  an nRF54L15 board without external flash.

  This sketch:
    1. Wakes the flash once and prints its JEDEC ID.
    2. Sends deep-power-down.
    3. Parks the QSPI pins.
    4. Sleeps repeatedly using delay(), which this core maps to WFI in the
       low-power build profile.
*/

#include <Arduino.h>

#if !(defined(ARDUINO_NRF54LM20A) || defined(ARDUINO_NRF54LM20B))
#error "QspiFlashDeepSleep requires the XIAO nRF54LM20A board."
#endif

#include <XiaoNrf54lm20QspiFlash.h>

static void printHexByte(uint8_t value) {
  if (value < 0x10) {
    Serial.print('0');
  }
  Serial.print(value, HEX);
}

void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println();
  Serial.println("XIAO nRF54LM20A QSPI flash deep sleep");

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
    Serial.println("JEDEC ID read failed; still parking flash pins.");
  }

  if (XiaoQspiFlash.prepareForSleep()) {
    Serial.println("Flash deep-power-down command sent.");
  } else {
    Serial.println("Flash deep-power-down failed.");
  }

  Serial.println("Close Serial Monitor before measuring board sleep current.");
  Serial.flush();
  delay(50);
}

void loop() {
  delay(1000);
}
