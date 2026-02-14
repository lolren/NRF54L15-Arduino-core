#include <Arduino.h>
#include <HPFMSPI.h>

void setup() {
  Serial.begin(115200);
  delay(1200);

  Serial.println("HPF MSPI info");
  Serial.println(HPFMSPI.info());

  if (!HPFMSPI.begin()) {
    Serial.print("HPFMSPI begin failed, err=");
    Serial.println(HPFMSPI.lastError());
    return;
  }

  Serial.print("channel 0 status=");
  Serial.println(HPFMSPI.channelStatus(0));
}

void loop() {
  delay(2000);
}
