#include <Arduino.h>
#include <XiaoNrf54L15.h>

/*
  XIAO nRF54L15 board test: print radio/tool settings selected in Arduino Tools.

  Useful to confirm:
  - Tools -> Antenna
  - Tools -> Radio Profile
  - Tools -> BLE TX Power
*/

static const char *profileToText(XiaoNrf54L15Class::RadioProfile profile)
{
  switch (profile) {
    case XiaoNrf54L15Class::RADIO_BLE_ONLY: return "BLE only";
    case XiaoNrf54L15Class::RADIO_DUAL: return "BLE + 802.15.4";
    case XiaoNrf54L15Class::RADIO_802154_ONLY: return "802.15.4 only";
    default: return "unknown";
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("XIAO nRF54L15 radio profile info");
  Serial.print("Compile profile: ");
  Serial.println(profileToText(XiaoNrf54L15.getRadioProfile()));

  Serial.print("Compile BLE TX power: ");
  Serial.print(XiaoNrf54L15.getBtTxPowerDbm());
  Serial.println(" dBm");

  Serial.print("Compile antenna: ");
  Serial.println(XiaoNrf54L15.isExternalAntennaBuild() ? "External U.FL" : "Ceramic");

  Serial.print("Current runtime antenna: ");
  Serial.println(XiaoNrf54L15.getAntenna() == XiaoNrf54L15Class::EXTERNAL ? "External U.FL" : "Ceramic");

  if (XiaoNrf54L15.getRadioProfile() == XiaoNrf54L15Class::RADIO_DUAL ||
      XiaoNrf54L15.getRadioProfile() == XiaoNrf54L15Class::RADIO_802154_ONLY) {
    Serial.println("802.15.4 support is enabled in Zephyr base for this build.");
  } else {
    Serial.println("802.15.4 support is disabled in this build.");
  }
}

void loop() {
  delay(2000);
}

