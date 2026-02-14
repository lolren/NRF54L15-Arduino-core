#include <IEEE802154.h>

void setup()
{
  Serial.begin(115200);
  while (!Serial) {
    delay(10);
  }

  Serial.println("IEEE802154 Config Example");

  if (!IEEE802154.begin()) {
    Serial.print("begin failed err=");
    Serial.println(IEEE802154.lastError());
    return;
  }

  (void)IEEE802154.setChannel(15);
  (void)IEEE802154.setPanId(0x1234);
  (void)IEEE802154.setShortAddress(0x3344);
  (void)IEEE802154.setTxPower(0);
  (void)IEEE802154.setAckEnabled(true);

  Serial.print("channel=");
  Serial.println(IEEE802154.channel());
  Serial.print("pan=");
  Serial.println(IEEE802154.panId(), HEX);
  Serial.print("short=");
  Serial.println(IEEE802154.shortAddress(), HEX);
  Serial.print("tx-dbm=");
  Serial.println(IEEE802154.txPower());
  Serial.print("ext-addr=");
  Serial.println(IEEE802154.extendedAddress());
}

void loop()
{
  delay(2000);
  Serial.print("alive channel=");
  Serial.print(IEEE802154.channel());
  Serial.print(" err=");
  Serial.println(IEEE802154.lastError());
}
