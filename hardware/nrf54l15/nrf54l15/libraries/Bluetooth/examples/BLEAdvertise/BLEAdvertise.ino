#include <Bluetooth.h>

void setup()
{
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);

  Serial.begin(115200);
  while (!Serial) {
    delay(10);
  }

  Serial.println("BLE Advertise Example (Clean Core)");
  BLE.begin("XIAO-nRF54L15");

  if (!BLE.advertise()) {
    int err = BLE.lastError();
    Serial.print("BLE advertise failed, err=");
    Serial.print(err);
    Serial.print(" -> ");
    if (err == -123) {
      Serial.println("BLE disabled by Tools > Radio Profile");
      Serial.println("Set Tools > Radio Profile to BLE Only (or BLE + 802.15.4).");
    } else if (err == -88) {
      Serial.println("BLE stack not linked in clean no-blob core yet.");
      Serial.println("Use the Zephyr core board for working BLE right now.");
    } else {
      Serial.println("BLE unavailable.");
    }
  }
}

void loop()
{
  digitalWrite(LED_BUILTIN, LOW);
  delay(100);
  digitalWrite(LED_BUILTIN, HIGH);
  delay(900);
}
