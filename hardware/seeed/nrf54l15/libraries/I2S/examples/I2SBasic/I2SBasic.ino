#include <Arduino.h>

#include <I2S.h>

uint8_t txBlock[256];
uint8_t rxBlock[256];

void setup() {
  Serial.begin(115200);
  delay(300);

  for (size_t i = 0; i < sizeof(txBlock); ++i) {
    txBlock[i] = (uint8_t)i;
  }

  if (!I2S.begin(16000, 16, 2)) {
    Serial.println("I2S begin failed (device/pins may be unavailable)");
    return;
  }

  Serial.println("I2S configured");
}

void loop() {
  int written = I2S.write(txBlock, sizeof(txBlock));
  int readn = I2S.read(rxBlock, sizeof(rxBlock));

  Serial.print("write=");
  Serial.print(written);
  Serial.print(" read=");
  Serial.println(readn);

  delay(500);
}
