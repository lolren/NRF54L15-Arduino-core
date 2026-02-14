/*
 * BLE6FeatureProbe
 *
 * Prints BLE 6 / Channel Sounding status from the board menu selection
 * and the effective Zephyr configuration.
 */

#include <XiaoNrf54L15.h>

void setup() {
  Serial.begin(115200);
  delay(300);

  Serial.println("ble6-probe: ready");
  Serial.print("radio-profile: ");
  Serial.println(XiaoNrf54L15.radioProfileName());
  Serial.print("ble-enabled: ");
  Serial.println(XiaoNrf54L15.bleEnabled() ? "yes" : "no");
  Serial.print("ble6-requested: ");
  Serial.println(XiaoNrf54L15.ble6FeatureSetRequested() ? "yes" : "no");
  Serial.print("ble6-enabled: ");
  Serial.println(XiaoNrf54L15.ble6FeatureSetEnabled() ? "yes" : "no");
  Serial.print("channel-sounding-enabled: ");
  Serial.println(XiaoNrf54L15.channelSoundingEnabled() ? "yes" : "no");
}

void loop() {
  Serial.print("ble6-requested=");
  Serial.print(XiaoNrf54L15.ble6FeatureSetRequested() ? "yes" : "no");
  Serial.print(" enabled=");
  Serial.print(XiaoNrf54L15.ble6FeatureSetEnabled() ? "yes" : "no");
  Serial.print(" cs=");
  Serial.println(XiaoNrf54L15.channelSoundingEnabled() ? "yes" : "no");

  delay(2000);
}
