#include <Arduino.h>
#include <Zigbee.h>

#if defined(ARDUINO_XIAO_NRF54L15_RADIO_BLE_ONLY)

void setup() {
  Serial.begin(115200);
  delay(1200);
  Serial.println("zigbee_radio_config requires Tools -> Radio Profile -> BLE + 802.15.4 or 802.15.4 Only.");
}

void loop() {
  delay(1000);
}

#else

void setup() {
  Serial.begin(115200);
  delay(1200);

  if (!Zigbee.begin()) {
    Serial.print("begin failed, err=");
    Serial.println(Zigbee.lastError());
    return;
  }

  Serial.println("XIAO nRF54L15 Zigbee/802.15.4 radio config");

  if (!Zigbee.setChannel(15)) {
    Serial.print("setChannel failed, err=");
    Serial.println(Zigbee.lastError());
  }

  if (!Zigbee.setPanId(0x1234)) {
    Serial.print("setPanId failed, err=");
    Serial.println(Zigbee.lastError());
  }

  if (!Zigbee.setShortAddress(0x0101)) {
    Serial.print("setShortAddress failed, err=");
    Serial.println(Zigbee.lastError());
  }

  (void)Zigbee.setTxPower(0);

  Serial.print("channel=");
  Serial.println(Zigbee.channel());

  Serial.print("panId=0x");
  Serial.println(Zigbee.panId(), HEX);

  Serial.print("shortAddr=0x");
  Serial.println(Zigbee.shortAddress(), HEX);

  Serial.print("txPower(dBm)=");
  Serial.println(Zigbee.txPower());

  Serial.print("extAddr=");
  Serial.println(Zigbee.extendedAddressString());
}

void loop() {
  delay(2000);
}

#endif
