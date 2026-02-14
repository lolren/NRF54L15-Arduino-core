#include <Bluetooth.h>

static uint32_t g_hits = 0;

static void onScanResult(const char* address, const char* name, int rssi, uint8_t advType)
{
  g_hits++;
  Serial.print("#");
  Serial.print(g_hits);
  Serial.print(" addr=");
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
  while (!Serial) {
    delay(10);
  }

  Serial.println("BLE Scan ForEach Example (Clean Core)");
  if (!BLE.begin("XIAO-nRF54L15")) {
    Serial.print("BLE.begin failed err=");
    Serial.println(BLE.lastError());
  }
}

void loop()
{
  g_hits = 0;
  Serial.println("scan window start");

  if (!BLE.scanForEach(onScanResult, 2500)) {
    const int err = BLE.lastError();
    if (err == 0) {
      Serial.println("scan window complete, no advertisers seen");
    } else {
      Serial.print("scanForEach failed err=");
      Serial.println(err);
    }
    delay(1000);
    return;
  }

  Serial.print("scan window complete, hits=");
  Serial.println(g_hits);
  delay(500);
}
