/*
 * BLE6RangeProbe
 *
 * Practical RSSI-based range probe for BLE/BLE6 setups.
 * Pair with BLE6RangeBeacon (name: XIAO-BLE6-TX).
 */

#include <Bluetooth.h>
#include <XiaoNrf54L15.h>

#include <string.h>

static const char kTargetName[] = "XIAO-BLE6-TX";
static const char kTargetAddress[] = "";
static const uint32_t kScanWindowMs = 1500;

static uint32_t g_samples = 0;
static int g_minRssi = 127;
static int g_maxRssi = -127;
static int32_t g_sumRssi = 0;

static uint32_t g_windowHits = 0;
static int g_windowBestRssi = -127;
static char g_windowBestAddress[32] = {0};
static char g_windowBestName[32] = {0};

static const char* qualityFromRssi(int rssi)
{
  if (rssi >= -55) {
    return "excellent";
  }
  if (rssi >= -67) {
    return "good";
  }
  if (rssi >= -75) {
    return "fair";
  }
  if (rssi >= -85) {
    return "weak";
  }
  return "edge";
}

static void onTargetScanResult(const char* address, const char* name, int rssi, uint8_t advType)
{
  (void)advType;
  g_windowHits++;

  if (rssi > g_windowBestRssi) {
    g_windowBestRssi = rssi;
    strncpy(g_windowBestAddress, address ? address : "", sizeof(g_windowBestAddress) - 1);
    g_windowBestAddress[sizeof(g_windowBestAddress) - 1] = '\0';
    strncpy(g_windowBestName, (name && name[0] != '\0') ? name : kTargetName, sizeof(g_windowBestName) - 1);
    g_windowBestName[sizeof(g_windowBestName) - 1] = '\0';
  }
}

void setup()
{
  Serial.begin(115200);
  delay(250);

  Serial.println("ble6-range-probe: ready");
  Serial.print("radio-profile: ");
  Serial.println(XiaoNrf54L15.radioProfileName());
  Serial.print("ble6-requested: ");
  Serial.println(XiaoNrf54L15.ble6FeatureSetRequested() ? "yes" : "no");
  Serial.print("ble6-enabled: ");
  Serial.println(XiaoNrf54L15.ble6FeatureSetEnabled() ? "yes" : "no");

  if (!BLE.begin("XIAO-BLE6-RX")) {
    Serial.print("BLE.begin failed err=");
    Serial.println(BLE.lastError());
  }

  if (!BLE.setScanFilterName(kTargetName)) {
    Serial.print("setScanFilterName failed err=");
    Serial.println(BLE.lastError());
  } else {
    Serial.print("scan-filter-name: ");
    Serial.println(BLE.scanFilterName());
  }

  if (kTargetAddress[0] != '\0') {
    if (!BLE.setScanFilterAddress(kTargetAddress)) {
      Serial.print("setScanFilterAddress failed err=");
      Serial.println(BLE.lastError());
    } else {
      Serial.print("scan-filter-address: ");
      Serial.println(BLE.scanFilterAddress());
    }
  }

  Serial.print("target-name: ");
  Serial.println(kTargetName);
  if (kTargetAddress[0] != '\0') {
    Serial.print("target-address: ");
    Serial.println(kTargetAddress);
  }
}

void loop()
{
  g_windowHits = 0;
  g_windowBestRssi = -127;
  g_windowBestAddress[0] = '\0';
  g_windowBestName[0] = '\0';

  if (!BLE.scanForEach(onTargetScanResult, kScanWindowMs)) {
    const int err = BLE.lastError();
    if (err == BluetoothClass::ErrorScanFilterNoMatch) {
      Serial.println("target not found in this scan window");
    } else {
      Serial.print("scan failed err=");
      Serial.println(err);
    }
    delay(300);
    return;
  }

  const int rssi = g_windowBestRssi;

  if (rssi < g_minRssi) {
    g_minRssi = rssi;
  }
  if (rssi > g_maxRssi) {
    g_maxRssi = rssi;
  }
  g_sumRssi += rssi;
  g_samples++;

  const float avg = static_cast<float>(g_sumRssi) / static_cast<float>(g_samples);

  Serial.print("target=");
  Serial.print(g_windowBestName);
  Serial.print(" addr=");
  Serial.print(g_windowBestAddress);
  Serial.print(" rssi=");
  Serial.print(rssi);
  Serial.print(" window-hits=");
  Serial.print(g_windowHits);
  Serial.print(" quality=");
  Serial.print(qualityFromRssi(rssi));
  Serial.print(" samples=");
  Serial.print(g_samples);
  Serial.print(" min=");
  Serial.print(g_minRssi);
  Serial.print(" max=");
  Serial.print(g_maxRssi);
  Serial.print(" avg=");
  Serial.println(avg, 2);
}
