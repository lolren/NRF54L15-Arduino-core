#include <Arduino.h>

/*
  GPS passthrough sample for modules connected to Serial1.
  XIAO nRF54L15 Serial1 pins: TX=D6, RX=D7
*/

#ifndef GPS_BAUD
#define GPS_BAUD 9600
#endif

void setup() {
  Serial.begin(115200);
  Serial1.begin(GPS_BAUD);
  delay(1000);

  Serial.println("XIAO nRF54L15 GPS sample");
  Serial.print("Listening on Serial1 @ ");
  Serial.println(GPS_BAUD);
}

void loop() {
  while (Serial1.available() > 0) {
    const char c = static_cast<char>(Serial1.read());
    Serial.write(static_cast<uint8_t>(c));
  }

  while (Serial.available() > 0) {
    Serial1.write(static_cast<uint8_t>(Serial.read()));
  }
}
