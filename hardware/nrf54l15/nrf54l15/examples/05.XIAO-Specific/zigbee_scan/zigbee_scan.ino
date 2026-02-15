#include <Arduino.h>
#include <Zigbee.h>
#include <XiaoNrf54L15.h>

#if defined(ARDUINO_XIAO_NRF54L15_RADIO_BLE_ONLY)

void setup() {
  Serial.begin(115200);
  delay(1200);
  Serial.println("zigbee_scan requires Tools -> Radio Profile -> BLE + 802.15.4 or 802.15.4 Only.");
}

void loop() {
  delay(1000);
}

#else

static uint32_t scanMask11to26()
{
  uint32_t mask = 0;
  for (uint8_t ch = 11; ch <= 26; ++ch) {
    mask |= (1UL << (ch - 1));
  }
  return mask;
}

static String formatAddress(const ZigbeeClass::ScanResult &entry)
{
  if (entry.addressLength == 0) {
    return String("-");
  }

  char buf[3 * 8] = {0};
  size_t pos = 0;
  for (uint8_t i = 0; i < entry.addressLength && i < sizeof(entry.address); ++i) {
    int n = snprintf(
      buf + pos,
      sizeof(buf) - pos,
      (i + 1 < entry.addressLength) ? "%02X:" : "%02X",
      entry.address[i]);
    if (n <= 0) {
      break;
    }
    pos += static_cast<size_t>(n);
    if (pos >= sizeof(buf)) {
      break;
    }
  }

  return String(buf);
}

void setup() {
  Serial.begin(115200);
  delay(1200);

  Serial.println("XIAO nRF54L15 Zigbee/802.15.4 scan");
  Serial.println("Note: BLE advertisements are not Zigbee/802.15.4 beacons.");
  Serial.print("Radio profile: ");
  Serial.println(static_cast<int>(XiaoNrf54L15.getRadioProfile()));

  if (!Zigbee.begin()) {
    Serial.print("Zigbee begin failed, err=");
    Serial.println(Zigbee.lastError());
  }
}

void loop() {
  size_t found = Zigbee.activeScan(scanMask11to26(), 200);
  int err = Zigbee.lastError();

  if (found == 0 && err == 0) {
    found = Zigbee.passiveScan(scanMask11to26(), 200);
    err = Zigbee.lastError();
    Serial.print("passiveScan results=");
  } else {
    Serial.print("activeScan results=");
  }

  Serial.print(found);
  Serial.print(" err=");
  Serial.println(err);

  for (size_t i = 0; i < Zigbee.scanResultCount(); ++i) {
    ZigbeeClass::ScanResult r;
    if (!Zigbee.getScanResult(i, r)) {
      continue;
    }

    Serial.print("  ch=");
    Serial.print(r.channel);
    Serial.print(" pan=0x");
    Serial.print(r.panId, HEX);
    Serial.print(" lqi=");
    Serial.print(r.lqi);
    Serial.print(" assoc=");
    Serial.print(r.associationPermitted ? "yes" : "no");
    Serial.print(" addr=");
    Serial.println(formatAddress(r));
  }

  delay(2000);
}

#endif
