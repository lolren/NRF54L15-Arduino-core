/**
 * Thread Experimental Reference Attach
 *
 * Attaches to an external Thread network (OTBR, Zephyr/NCS, Nordic CLI)
 * using a dataset TLV hex string provided via serial.
 *
 * Usage:
 *   1. Flash this sketch.
 *   2. Open serial at 115200.
 *   3. Send: dataset-hex <tlv-hex-string>
 *   4. Board applies dataset and attempts to attach.
 *   5. Monitor status output for attach result.
 *
 * Example dataset-hex from OTBR:
 *   sudo ot-ctl dataset active -x <hex>
 *   sudo ot-ctl dataset active -x
 *
 * FQBN: nrf54l15clean:nrf54l15clean:xiao_nrf54l15:clean_thread=stage
 */

#include <nrf54_all.h>
#include "openthread_platform_nrf54l15.h"

#if !defined(NRF54L15_CLEAN_OPENTHREAD_CORE_ENABLE) || \
    (NRF54L15_CLEAN_OPENTHREAD_CORE_ENABLE == 0)
#error "Enable Tools > Thread Core > Experimental Stage Core before building."
#endif

namespace {

xiao_nrf54l15::Nrf54ThreadExperimental gThread;

constexpr bool kLedActiveLow = true;

static void setLed(bool on) {
#if defined(LED_BUILTIN)
  digitalWrite(LED_BUILTIN, (on ^ kLedActiveLow) ? HIGH : LOW);
#endif
}

uint32_t gLastStatusMs = 0;
bool gDatasetSet = false;
bool gWasAttached = false;
uint32_t gAttachAttempts = 0;

// Simple command parser
void handleCommand(const char* cmd) {
  if (strncmp(cmd, "dataset-hex ", 12) == 0) {
    const char* hex = cmd + 12;
    Serial.print(F("Applying dataset: "));
    Serial.println(hex);

    bool ok = gThread.setActiveDatasetHex(hex);
    if (ok) {
      gDatasetSet = true;
      Serial.println(F("dataset applied, starting Thread..."));

      // Start as child to attach to external network
      gThread.beginAsChild(false);
    } else {
      Serial.println(F("ERROR: failed to apply dataset"));
    }
  } else if (strcmp(cmd, "status") == 0) {
    Serial.print(F("role="));
    Serial.print(gThread.roleName());
    Serial.print(F(" attached="));
    Serial.print(gThread.attached() ? 1 : 0);
    Serial.print(F(" rloc16=0x"));
    Serial.print(gThread.rloc16(), HEX);
    Serial.print(F(" part="));
    Serial.print(gThread.partitionId(), HEX);
    
    
  } else if (strcmp(cmd, "help") == 0) {
    Serial.println(F("Commands:"));
    Serial.println(F("  dataset-hex <tlv-hex>  - Apply dataset and attach"));
    Serial.println(F("  status                 - Show current state"));
    Serial.println(F("  help                   - Show this help"));
  } else {
    Serial.print(F("Unknown command: "));
    Serial.println(cmd);
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  Serial1.begin(115200);
  // Serial may not connect on nRF54L15 - use timeout instead

  Serial.println(F("=== Thread Reference Attach ==="));
  Serial.println(F("Send 'dataset-hex <tlv-hex>' to attach to external network."));
  Serial.println(F("Send 'help' for commands."));

#if defined(LED_BUILTIN)
  pinMode(LED_BUILTIN, OUTPUT);
  setLed(false);
#endif
}

void loop() {
  gThread.process();

  // Read serial commands
  while (Serial.available() > 0) {
    char line[256];
    int len = 0;
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (len > 0) {
        line[len] = '\0';
        handleCommand(line);
      }
    } else if (len < sizeof(line) - 1) {
      line[len++] = c;
    }
  }

  // Detect attach/detach
  bool nowAttached = gThread.attached();
  if (!gWasAttached && nowAttached) {
    gWasAttached = true;
    gAttachAttempts++;
    setLed(true);
    Serial.print(F("ATTACHED! attempt="));
    Serial.print(gAttachAttempts);
    Serial.print(F(" role="));
    Serial.print(gThread.roleName());
    Serial.print(F(" rloc16=0x"));
    Serial.print(gThread.rloc16(), HEX);
    Serial.print(F(" part="));
    Serial.print(gThread.partitionId(), HEX);
    
    
  } else if (gWasAttached && !nowAttached) {
    gWasAttached = false;
    setLed(false);
    Serial.println(F("DETACHED"));
  }
  gWasAttached = nowAttached;

  // Status every 10 seconds
  if (millis() - gLastStatusMs > 10000UL) {
    gLastStatusMs = millis();
    Serial.print(F("dataset="));
    Serial.print(gDatasetSet ? 1 : 0);
    Serial.print(F(" role="));
    Serial.print(gThread.roleName());
    Serial.print(F(" attached="));
    Serial.print(nowAttached ? 1 : 0);
    Serial.print(F(" err="));
    Serial.println(static_cast<int>(gThread.lastError()));
  }
}
