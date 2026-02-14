#include <Bluetooth.h>

BLEService ledService("19B10000-E8F2-537E-4F6C-D104768A1214"); // Bluetooth® Low Energy LED Service

// Bluetooth® Low Energy LED Characteristic - custom 128-bit UUID, read and writable by central
BLECharacteristic switchCharacteristic("19B10001-E8F2-537E-4F6C-D104768A1214", BLECharacteristic::BLERead | BLECharacteristic::BLEWrite, 1);

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("XIAO nRF54L15 BLE Peripheral Test");

  pinMode(LED_BUILTIN, OUTPUT);

  // begin initialization
  if (!BLE.begin("XIAO-nRF54-LED")) {
    Serial.print("BLE init failed! err=");
    Serial.println(BLE.lastError());
    while (1);
  }
  Serial.println("BLE init ok");

  // set advertised local name and service UUID:
  BLE.setLocalName("XIAO-nRF54-LED");
  BLE.setAdvertisedService(ledService);

  // add the characteristic to the service
  ledService.addCharacteristic(switchCharacteristic);

  // add service
  BLE.addService(ledService);
  Serial.println("Service added");

  // set the initial value for the characteristic:
  uint8_t val = 1;
  switchCharacteristic.writeValue(&val, 1);

  // start advertising
  if (!BLE.advertise()) {
    Serial.print("BLE advertise failed! err=");
    Serial.println(BLE.lastError());
  } else {
    Serial.println("BLE advertising started");
  }
}

void loop() {
  if (switchCharacteristic.valueLength() > 0) {
    uint8_t val = switchCharacteristic.value()[0];
    if (val) {
      digitalWrite(LED_BUILTIN, LOW); // LED ON
    } else {
      digitalWrite(LED_BUILTIN, HIGH); // LED OFF
    }
  }
  
  static unsigned long lastPrint = 0;
  if (millis() - lastPrint > 2000) {
    Serial.print("Looping... LED state: ");
    Serial.println(switchCharacteristic.value()[0]);
    lastPrint = millis();
  }
  
  delay(100);
}
