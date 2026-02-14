/*
 * BLEScanMonitor
 *
 * Streams every matched advertiser seen in each scan window.
 * Useful in busy RF environments to validate scanner stability.
 */

#include <Bluetooth.h>
#include <XiaoNrf54L15.h>

#include <string.h>

#define SCAN_WINDOW_MS 2500U
#define SCAN_FILTER_NAME ""
#define SCAN_FILTER_ADDRESS ""

static uint32_t g_windowHits = 0;
static int g_windowBestRssi = -127;
static char g_windowBestAddress[32] = {0};
static char g_windowBestName[32] = {0};

static void onScanResult(const char* address, const char* name, int rssi, uint8_t advType)
{
  g_windowHits++;

  if (rssi > g_windowBestRssi) {
    g_windowBestRssi = rssi;
    strncpy(g_windowBestAddress, address ? address : "", sizeof(g_windowBestAddress) - 1);
    g_windowBestAddress[sizeof(g_windowBestAddress) - 1] = '\0';
    strncpy(g_windowBestName, (name && name[0] != '\0') ? name : "-", sizeof(g_windowBestName) - 1);
    g_windowBestName[sizeof(g_windowBestName) - 1] = '\0';
  }

  Serial.print("[");
  Serial.print(g_windowHits);
  Serial.print("] addr=");
  Serial.print(address ? address : "");
  Serial.print(" rssi=");
  Serial.print(rssi);
  Serial.print(" type=0x");
  if (advType < 16U) {
    Serial.print("0");
  }
  Serial.print(advType, HEX);
  Serial.print(" name=");
  Serial.println((name && name[0] != '\0') ? name : "-");
}

void setup()
{
  Serial.begin(115200);
  delay(300);

  Serial.println("ble-scan-monitor: ready");
  Serial.print("radio-profile: ");
  Serial.println(XiaoNrf54L15.radioProfileName());
  Serial.print("ble-enabled: ");
  Serial.println(XiaoNrf54L15.bleEnabled() ? "yes" : "no");
  Serial.print("ble6-enabled: ");
  Serial.println(XiaoNrf54L15.ble6FeatureSetEnabled() ? "yes" : "no");

  if (!BLE.begin("XIAO-BLE-MON")) {
    Serial.print("BLE.begin failed err=");
    Serial.println(BLE.lastError());
    return;
  }

  if (SCAN_FILTER_NAME[0] != '\0') {
    if (!BLE.setScanFilterName(SCAN_FILTER_NAME)) {
      Serial.print("setScanFilterName failed err=");
      Serial.println(BLE.lastError());
    } else {
      Serial.print("scan-filter-name: ");
      Serial.println(BLE.scanFilterName());
    }
  } else {
    BLE.clearScanFilterName();
  }

  if (SCAN_FILTER_ADDRESS[0] != '\0') {
    if (!BLE.setScanFilterAddress(SCAN_FILTER_ADDRESS)) {
      Serial.print("setScanFilterAddress failed err=");
      Serial.println(BLE.lastError());
    } else {
      Serial.print("scan-filter-address: ");
      Serial.println(BLE.scanFilterAddress());
    }
  } else {
    BLE.clearScanFilterAddress();
  }
}

void loop()
{
  g_windowHits = 0;
  g_windowBestRssi = -127;
  g_windowBestAddress[0] = '\0';
  g_windowBestName[0] = '\0';

  Serial.println("---- scan window start ----");
  const bool ok = BLE.scanForEach(onScanResult, SCAN_WINDOW_MS);
  if (!ok) {
    const int err = BLE.lastError();
    if (err == BluetoothClass::ErrorScanFilterNoMatch) {
      Serial.println("scan window complete: no results matched active filter");
    } else if (err == 0) {
      Serial.println("scan window complete: no advertisers seen");
    } else {
      Serial.print("scan failed err=");
      Serial.println(err);
    }
    delay(500);
    return;
  }

  Serial.print("scan window complete: hits=");
  Serial.print(g_windowHits);
  Serial.print(" strongest-rssi=");
  Serial.print(g_windowBestRssi);
  Serial.print(" strongest-addr=");
  Serial.print(g_windowBestAddress);
  Serial.print(" strongest-name=");
  Serial.println(g_windowBestName);

  delay(500);
}
