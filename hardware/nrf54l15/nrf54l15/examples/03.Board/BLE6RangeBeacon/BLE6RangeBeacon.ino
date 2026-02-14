/*
 * BLE6RangeBeacon
 *
 * Companion transmitter for BLE6RangeProbe.
 * - Advertises local name "XIAO-BLE6-TX".
 * - Use BLE board menu options to enable BLE + optional BLE6 channel sounding.
 */

#include <Bluetooth.h>
#include <XiaoNrf54L15.h>

static const char kBeaconName[] = "XIAO-BLE6-TX";

void setup()
{
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);

  Serial.begin(115200);
  delay(250);

  Serial.println("ble6-range-beacon: ready");
  Serial.print("radio-profile: ");
  Serial.println(XiaoNrf54L15.radioProfileName());
  Serial.print("ble6-requested: ");
  Serial.println(XiaoNrf54L15.ble6FeatureSetRequested() ? "yes" : "no");
  Serial.print("ble6-enabled: ");
  Serial.println(XiaoNrf54L15.ble6FeatureSetEnabled() ? "yes" : "no");

  if (!BLE.begin(kBeaconName)) {
    Serial.print("BLE.begin failed err=");
    Serial.println(BLE.lastError());
    return;
  }

  BLE.setLocalName(kBeaconName);
  if (!BLE.advertise()) {
    Serial.print("BLE.advertise failed err=");
    Serial.println(BLE.lastError());
    return;
  }

  Serial.print("advertising as: ");
  Serial.println(kBeaconName);
}

void loop()
{
  static bool led = false;
  led = !led;
  digitalWrite(LED_BUILTIN, led ? LOW : HIGH);

  Serial.println("beacon alive");
  delay(1000);
}
