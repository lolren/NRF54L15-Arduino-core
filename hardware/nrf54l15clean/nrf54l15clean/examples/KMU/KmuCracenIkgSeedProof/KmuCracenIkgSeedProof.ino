#include "nrf54l15_hal.h"

using namespace xiao_nrf54l15;

namespace {

CracenIkg g_ikg;

void printStatus() {
  Serial.print("status=0x");
  Serial.print(g_ikg.status(), HEX);
  Serial.print(" hw=0x");
  Serial.print(g_ikg.hwConfig(), HEX);
  Serial.print(" active=");
  Serial.print(g_ikg.active() ? 1 : 0);
  Serial.print(" seed_valid=");
  Serial.print(g_ikg.seedValid() ? 1 : 0);
  Serial.print(" seed_locked=");
  Serial.print(g_ikg.seedLocked() ? 1 : 0);
  Serial.print(" seed_state_owner=");
  Serial.print(g_ikg.seedStateManagedByKmu() ? "KMU" : "provisioner");
  Serial.print(" okay=");
  Serial.print(g_ikg.okay() ? 1 : 0);
  Serial.print(" seed_err=");
  Serial.print(g_ikg.seedError() ? 1 : 0);
  Serial.print(" entropy_err=");
  Serial.print(g_ikg.entropyError() ? 1 : 0);
  Serial.print(" catastrophic=");
  Serial.print(g_ikg.catastrophicError() ? 1 : 0);
  Serial.print(" sym_stored=");
  Serial.print(g_ikg.symmetricKeysStored() ? 1 : 0);
  Serial.print(" priv_stored=");
  Serial.println(g_ikg.privateKeysStored() ? 1 : 0);
}

bool deriveProvisionedKeys() {
  if (!g_ikg.active() && !g_ikg.begin(800000UL)) {
    Serial.println("BLOCKED: CRACEN IKG did not become ready");
    return false;
  }
  if (!g_ikg.seedValid()) {
    Serial.println("BLOCKED: no trusted KMU-provisioned IKG seed is valid");
    Serial.println("The core will not validate unknown or reset-zero seed RAM.");
    return false;
  }
  if (!g_ikg.ikgGenerateKey()) {
    Serial.println("BLOCKED: isolated-key derivation failed");
    return false;
  }
  Serial.println("Isolated keys derived from the provisioned hardware seed.");
  return true;
}

void printHelp() {
  Serial.println("Commands: s status, g derive provisioned isolated keys, x erase derived keys");
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(1200);
  Serial.println();
  Serial.println("CRACEN IKG seed-state diagnostic (non-provisioning)");
  Serial.println("This sketch never provisions KMU slots or validates a seed.");
  printHelp();

  const bool ready = g_ikg.begin(800000UL);
  Serial.print("begin=");
  Serial.println(ready ? 1 : 0);
  if (ready) {
    deriveProvisionedKeys();
  }
  printStatus();
}

void loop() {
  if (!Serial.available()) {
    delay(20);
    return;
  }

  switch (static_cast<char>(Serial.read())) {
    case 's':
      printStatus();
      break;
    case 'g':
      Serial.print("derive=");
      Serial.println(deriveProvisionedKeys() ? 1 : 0);
      printStatus();
      break;
    case 'x':
      Serial.print("erase_derived_keys=");
      Serial.println(g_ikg.softResetKeys(800000UL) ? 1 : 0);
      printStatus();
      break;
    default:
      printHelp();
      break;
  }
}
