#include <Bluetooth.h>

#define SCAN_WINDOW_MS 4000U
#define CONNECT_TIMEOUT_MS 12000U
#define SCAN_FILTER_NAME ""

void setup()
{
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);

  Serial.begin(115200);
  while (!Serial) {
    delay(10);
  }

  Serial.println("BLE Central Connect Example");

  if (!BLE.begin("XIAO-CENTRAL")) {
    Serial.print("BLE.begin failed err=");
    Serial.println(BLE.lastError());
    return;
  }

  if (SCAN_FILTER_NAME[0] != '\0') {
    (void)BLE.setScanFilterName(SCAN_FILTER_NAME);
  } else {
    BLE.clearScanFilterName();
  }
}

void loop()
{
  Serial.println("Scanning...");
  if (!BLE.scan(SCAN_WINDOW_MS)) {
    Serial.print("scan failed err=");
    Serial.println(BLE.lastError());
    delay(1000);
    return;
  }

  Serial.print("Found ");
  Serial.print(BLE.address());
  Serial.print(" name=");
  Serial.print(BLE.name());
  Serial.print(" rssi=");
  Serial.println(BLE.rssi());

  Serial.println("Connecting to scan result...");
  if (!BLE.connectLastScanResult(CONNECT_TIMEOUT_MS)) {
    Serial.print("connect failed err=");
    Serial.println(BLE.lastError());
    delay(1000);
    return;
  }

  digitalWrite(LED_BUILTIN, LOW);
  Serial.print("Connected to ");
  Serial.println(BLE.connectedAddress());

  // 24-40 units = 30-50 ms connection interval
  if (!BLE.setConnectionInterval(24, 40, 0, 400)) {
    Serial.print("param update failed err=");
    Serial.println(BLE.lastError());
  }

  delay(5000);
  Serial.println("Disconnecting...");
  if (!BLE.disconnect()) {
    Serial.print("disconnect failed err=");
    Serial.println(BLE.lastError());
  }
  digitalWrite(LED_BUILTIN, HIGH);
  delay(1500);
}
