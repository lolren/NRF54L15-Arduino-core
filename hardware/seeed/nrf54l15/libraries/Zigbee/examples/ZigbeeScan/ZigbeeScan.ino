#include <Arduino.h>
#include <Zigbee.h>

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

  if (!Zigbee.begin()) {
    Serial.print("Zigbee init failed, err=");
    Serial.println(Zigbee.lastError());
    return;
  }

  Serial.println("IEEE 802.15.4 active scan (channels 11-26)");
}

void loop() {
  const size_t found = Zigbee.activeScan(scanMask11to26(), 200);
  Serial.print("found=");
  Serial.print(found);
  Serial.print(" err=");
  Serial.println(Zigbee.lastError());

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
