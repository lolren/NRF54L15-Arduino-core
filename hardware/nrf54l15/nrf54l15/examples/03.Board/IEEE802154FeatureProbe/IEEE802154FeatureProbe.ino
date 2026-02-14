/*
 * IEEE802154FeatureProbe
 *
 * Validates Arduino-facing 802.15.4 controls:
 * channel, PAN ID, short address, tx power, and passive scan callback path.
 */

#include <IEEE802154.h>
#include <XiaoNrf54L15.h>

static const uint32_t kChannel15Mask = (1UL << (15 - 1));
static uint32_t g_cycle = 0;

static void onScanResult(uint16_t channel, uint16_t panId, uint16_t shortAddr, uint8_t lqi, bool associationPermitted)
{
  Serial.print("scan result: ch=");
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
  delay(300);

  Serial.println("ieee802154-feature-probe: ready");
  Serial.print("radio-profile: ");
  Serial.println(XiaoNrf54L15.radioProfileName());

  if (!IEEE802154.begin()) {
    Serial.print("IEEE802154.begin failed err=");
    Serial.println(IEEE802154.lastError());
    return;
  }

  (void)IEEE802154.setChannel(15);
  (void)IEEE802154.setPanId(0x2345);
  (void)IEEE802154.setShortAddress(0x3456);
  (void)IEEE802154.setTxPower(0);
  (void)IEEE802154.setAckEnabled(true);
}

void loop()
{
  Serial.print("cycle=");
  Serial.print(g_cycle++);
  Serial.print(" ch=");
  Serial.print(IEEE802154.channel());
  Serial.print(" pan=0x");
  Serial.print(IEEE802154.panId(), HEX);
  Serial.print(" short=0x");
  Serial.print(IEEE802154.shortAddress(), HEX);
  Serial.print(" txdbm=");
  Serial.print(IEEE802154.txPower());
  Serial.print(" ext=");
  Serial.println(IEEE802154.extendedAddress());

  if (!IEEE802154.passiveScan(kChannel15Mask, 80, onScanResult)) {
    Serial.print("passiveScan failed err=");
    Serial.println(IEEE802154.lastError());
  }

  delay(2000);
}
