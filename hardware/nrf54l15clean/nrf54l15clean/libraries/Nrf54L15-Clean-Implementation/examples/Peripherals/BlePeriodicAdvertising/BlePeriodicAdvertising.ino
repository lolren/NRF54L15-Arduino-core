/*
 * BlePeriodicAdvertising - unsupported-feature capability probe
 *
 * The current controller does not implement BLE periodic advertising. This
 * sketch verifies that the legacy API reports the limitation and fails closed;
 * it does not transmit any radio packets.
 *
 * Hardware: XIAO nRF54L15
 * Serial:   115200 baud
 */

#include <Arduino.h>
#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

static_assert(!BlePeriodicAdvertising::supported(),
              "Update this capability probe when periodic advertising lands");

void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);

  Serial.println(F("======================================"));
  Serial.println(F("  BLE Periodic Advertising Capability"));
  Serial.println(F("======================================"));
  Serial.println();

  BlePeriodicAdvertising adv;

  const uint8_t probeData[] = {0x02, 0x01, 0x06};
  const bool rejected =
      !adv.begin(probeData, sizeof(probeData), 100U) &&
      !adv.setData(probeData, sizeof(probeData)) &&
      !adv.setIntervalMs(100U) && !adv.setTxPowerDbm(0) &&
      !adv.isActive() && adv.packetCount() == 0U;

  Serial.println(F("======================================"));
  Serial.println(F("  Support: NOT IMPLEMENTED"));
  Serial.println(rejected ? F("  Fail-closed API check: PASS")
                          : F("  Fail-closed API check: FAIL"));
  Serial.println(F("  No periodic advertising packets were transmitted."));
  Serial.println(F("======================================"));

  while (true) delay(1000);
}

void loop() {
  // Not reached.
}
