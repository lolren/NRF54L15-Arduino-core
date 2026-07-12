/*********************************************************************
 This is an example for the nRF54 Bluefruit compatibility layer.

 MIT license, check LICENSE for more information.
*********************************************************************/

#include <bluefruit.h>

// ble_gap_addr_t stores the least-significant address byte first. This value
// is displayed by scanners as 11:22:33:44:55:66.
const ble_gap_addr_t fallbackTarget = {
    {0x66, 0x55, 0x44, 0x33, 0x22, 0x11},
    BLE_GAP_ADDR_TYPE_PUBLIC,
};

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000UL) {
    delay(10);
  }

  if (!Bluefruit.begin()) {
    Serial.println("Bluefruit initialization failed");
    return;
  }

  ble_gap_addr_t target = fallbackTarget;
  if (Bluefruit.Security.getBondPeerIdentityAddress(&target)) {
    Serial.println("Using the retained peer identity address");
  } else if (Bluefruit.Security.getBondPeerAddress(&target)) {
    Serial.println("Using the retained peer connection address");
  } else {
    Serial.println("Using fallbackTarget; edit it to match the central");
  }

  // Legacy directed advertising carries AdvA + TargetA only. Advertising and
  // scan-response data must both be empty.
  Bluefruit.Advertising.clearData();
  Bluefruit.ScanResponse.clearData();
  Bluefruit.Advertising.setPeerAddress(target);
  Bluefruit.Advertising.setType(
      BLE_GAP_ADV_TYPE_CONNECTABLE_NONSCANNABLE_DIRECTED);
  Bluefruit.Advertising.setInterval(160, 160);  // 100 ms, low duty cycle
  Bluefruit.Advertising.restartOnDisconnect(true);

  if (!Bluefruit.Advertising.start(0)) {
    Serial.println("Directed advertising configuration rejected");
    return;
  }
  Serial.println("Directed advertising started");
}

void loop() {
  delay(1000);
}
