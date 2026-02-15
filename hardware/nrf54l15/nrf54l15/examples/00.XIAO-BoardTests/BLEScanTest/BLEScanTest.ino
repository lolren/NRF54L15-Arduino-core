#include <Arduino.h>
#include <XiaoNrf54L15.h>

#if defined(ARDUINO_XIAO_NRF54L15_RADIO_802154_ONLY)

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  Serial.begin(115200);
  delay(1000);
  Serial.println("BLEScanTest: BLE is disabled by Tools -> Radio Profile (802.15.4 only).");
  Serial.println("Select BLE Only or BLE + 802.15.4 to run this sketch.");
}

void loop() {
  digitalWrite(LED_BUILTIN, LOW);
  delay(100);
  digitalWrite(LED_BUILTIN, HIGH);
  delay(900);
}

#else

#include <Bluetooth.h>

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);

  Serial.begin(115200);
  delay(1200);

  (void)XiaoNrf54L15.useCeramicAntenna();

  Serial.println("XIAO nRF54L15 BLE scan test");
  if (!BLE.begin("XIAO-nRF54L15-Scanner")) {
    Serial.println("BLE init failed");
    Serial.print("err=");
    Serial.println(BLE.lastError());
  } else {
    Serial.println("BLE init ok");
  }
}

void loop() {
  Serial.println("Scanning 5s...");
  if (BLE.scan(5000)) {
    Serial.print("Name: ");
    Serial.print(BLE.name());
    Serial.print("  Address: ");
    Serial.print(BLE.address());
    Serial.print("  RSSI: ");
    Serial.println(BLE.rssi());
    digitalWrite(LED_BUILTIN, LOW);
    delay(120);
    digitalWrite(LED_BUILTIN, HIGH);
  } else {
    Serial.print("No device found, err=");
    Serial.println(BLE.lastError());
    digitalWrite(LED_BUILTIN, HIGH);
  }

  delay(500);
}

#endif

