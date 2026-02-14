#include <IEEE802154.h>

// Bits are channel-1. This selects channels 11..26.
static const uint32_t kAll24GHzChannelsMask = 0x03FFF800UL;

static void onScanResult(uint16_t channel, uint16_t panId, uint16_t shortAddr, uint8_t lqi, bool associationPermitted)
{
  Serial.print("scan hit ch=");
  Serial.print(channel);
  Serial.print(" pan=0x");
  Serial.print(panId, HEX);
  Serial.print(" short=0x");
  Serial.print(shortAddr, HEX);
  Serial.print(" lqi=");
  Serial.print(lqi);
  Serial.print(" assoc=");
  Serial.println(associationPermitted ? "yes" : "no");
}

void setup()
{
  Serial.begin(115200);
  while (!Serial) {
    delay(10);
  }

  Serial.println("IEEE802154 Passive Scan Example");
  if (!IEEE802154.begin()) {
    Serial.print("begin failed err=");
    Serial.println(IEEE802154.lastError());
  }
}

void loop()
{
  Serial.println("passive scan start...");
  const bool ok = IEEE802154.passiveScan(kAll24GHzChannelsMask, 60, onScanResult);
  if (!ok) {
    Serial.print("scan failed err=");
    Serial.println(IEEE802154.lastError());
  }
  delay(2000);
}
