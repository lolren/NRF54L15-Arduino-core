#include <Arduino.h>

#include <PDM.h>

int16_t samples[128];

void setup() {
  Serial.begin(115200);
  delay(300);

  if (!PDM.begin(16000, 1)) {
    Serial.println("PDM begin failed");
    return;
  }

  Serial.println("PDM started");
}

void loop() {
  int got = PDM.read(samples, sizeof(samples));
  if (got > 0) {
    Serial.print("bytes=");
    Serial.print(got);
    Serial.print(" first=");
    Serial.println(samples[0]);
  }
  delay(20);
}
