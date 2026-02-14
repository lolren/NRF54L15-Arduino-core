#include <Arduino.h>
#include <Zigbee.h>

void setup() {
  Serial.begin(115200);
  delay(1200);

  if (!Zigbee.begin()) {
    Serial.print("Zigbee init failed, err=");
    Serial.println(Zigbee.lastError());
    return;
  }

  Serial.println("IEEE 802.15.4 radio config example");

  if (!Zigbee.setChannel(15)) {
    Serial.print("setChannel failed, err=");
    Serial.println(Zigbee.lastError());
  }

  if (!Zigbee.setPanId(0x1234)) {
    Serial.print("setPanId failed, err=");
    Serial.println(Zigbee.lastError());
  }

  (void)Zigbee.setTxPower(0);

  Serial.print("channel=");
  Serial.println(Zigbee.channel());
  Serial.print("panId=0x");
  Serial.println(Zigbee.panId(), HEX);
  Serial.print("txPower(dBm)=");
  Serial.println(Zigbee.txPower());
  Serial.print("extAddr=");
  Serial.println(Zigbee.extendedAddressString());
}

void loop() {
  delay(2000);
}
