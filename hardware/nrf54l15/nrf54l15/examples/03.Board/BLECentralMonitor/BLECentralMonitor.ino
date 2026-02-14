/*
 * BLECentralMonitor
 *
 * Scan -> connect -> hold -> disconnect loop for validating central role.
 * Works best with Tools > Radio Profile = BLE Only (or BLE + 802.15.4)
 * and BLE Controller = Zephyr LL SW.
 */

#include <Bluetooth.h>
#include <XiaoNrf54L15.h>

#define SCAN_MS 3000U
#define CONNECT_TIMEOUT_MS 10000U
#define HOLD_MS 3000U
#define FILTER_NAME ""

static uint32_t g_cycle = 0;

void setup()
{
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);

  Serial.begin(115200);
  delay(300);

  Serial.println("ble-central-monitor: ready");
  Serial.print("radio-profile: ");
  Serial.println(XiaoNrf54L15.radioProfileName());

  if (!BLE.begin("XIAO-CENTRAL-MON")) {
    Serial.print("BLE.begin failed err=");
    Serial.println(BLE.lastError());
    return;
  }

  if (FILTER_NAME[0] != '\0') {
    (void)BLE.setScanFilterName(FILTER_NAME);
  } else {
    BLE.clearScanFilterName();
  }
}

void loop()
{
  Serial.print("cycle=");
  Serial.println(g_cycle++);
  Serial.println("scan start");

  if (!BLE.scan(SCAN_MS)) {
    Serial.print("scan failed err=");
    Serial.println(BLE.lastError());
    delay(1000);
    return;
  }

  Serial.print("scan hit addr=");
  Serial.print(BLE.address());
  Serial.print(" name=");
  Serial.print(BLE.name());
  Serial.print(" rssi=");
  Serial.println(BLE.rssi());

  if (!BLE.connectLastScanResult(CONNECT_TIMEOUT_MS)) {
    Serial.print("connect failed err=");
    Serial.println(BLE.lastError());
    delay(1000);
    return;
  }

  digitalWrite(LED_BUILTIN, LOW);
  Serial.print("connected=");
  Serial.println(BLE.connectedAddress());

  (void)BLE.setConnectionInterval(24, 56, 0, 400);
  delay(HOLD_MS);

  if (!BLE.disconnect()) {
    Serial.print("disconnect failed err=");
    Serial.println(BLE.lastError());
  } else {
    Serial.println("disconnected");
  }

  digitalWrite(LED_BUILTIN, HIGH);
  delay(1200);
}
