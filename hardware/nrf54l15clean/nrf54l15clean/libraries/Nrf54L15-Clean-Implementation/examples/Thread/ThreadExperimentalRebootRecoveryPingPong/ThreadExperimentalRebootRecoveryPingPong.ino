/**
 * Thread Experimental Reboot Recovery Ping Pong
 *
 * Two-board test: Proves Thread dataset survives power cycle and boards
 * re-attach to the same network automatically.
 *
 * Board A (PONGER): Leader/router, responds to PING with PONG.
 * Board B (PINGER): Child, sends PING every 2s.
 *
 * Test procedure:
 *   1. Flash both boards (first boot seeds demo dataset).
 *   2. Wait for "boot=0 phase=idle" → "boot=0 phase=ready" on both.
 *   3. pyocd reset both boards.
 *   4. Wait for "boot=1 phase=ready" — should re-attach with SAME partition ID.
 *   5. Verify ping/pong resumes without re-provisioning.
 *
 * Change line 30 to switch roles before compiling.
 *
 * FQBN: nrf54l15clean:nrf54l15clean:xiao_nrf54l15:clean_thread=stage
 */

#include <nrf54_all.h>
#include "openthread_platform_nrf54l15.h"

#if !defined(NRF54L15_CLEAN_OPENTHREAD_CORE_ENABLE) || \
    (NRF54L15_CLEAN_OPENTHREAD_CORE_ENABLE == 0)
#error "Enable Tools > Thread Core > Experimental Stage Core before building."
#endif

// CHANGE THIS to switch roles: PONGER or PINGER
enum class DemoRole : uint8_t { PINGER = 0, PONGER = 1 };
constexpr DemoRole ROLE = DemoRole::PONGER;

namespace {

xiao_nrf54l15::Nrf54ThreadExperimental gThread;

constexpr uint16_t kPingPort = 49152;
constexpr uint16_t kPongPort = 49153;

static const uint8_t kPingMsg[] = { 'P', 'I', 'N', 'G' };
static const uint8_t kPongMsg[] = { 'P', 'O', 'N', 'G' };

constexpr bool kLedActiveLow = true;

static void setLed(bool on) {
#if defined(LED_BUILTIN)
  digitalWrite(LED_BUILTIN, (on ^ kLedActiveLow) ? HIGH : LOW);
#endif
}

uint32_t gPingCount = 0;
uint32_t gPongCount = 0;
uint32_t gPingFailCount = 0;
bool gPeerKnown = false;
otIp6Address gPeerAddr = {};
uint32_t gLastPingMs = 0;
uint32_t gLastStatusMs = 0;
uint32_t gBootCount = 0;
char gPartitionId[9] = {0};
bool gWasAttached = false;
bool gFirstBoot = true;

void udpReceiveCallback(void* context, const uint8_t* payload,
                        uint16_t length, const otMessageInfo& info) {
  (void)context;

  if (!gPeerKnown) {
    gPeerAddr = info.mPeerAddr;
    gPeerKnown = true;
    Serial.println(F("peer discovered!"));
  }

  if (ROLE == DemoRole::PONGER) {
    if (length == sizeof(kPingMsg) &&
        memcmp(payload, kPingMsg, sizeof(kPingMsg)) == 0) {
      gPongCount++;
      gThread.sendUdp(info.mPeerAddr, kPongPort, kPongMsg, sizeof(kPongMsg));
      setLed((gPongCount % 2) != 0);
      Serial.print(F("pong tx="));
      Serial.println(gPongCount);
    }
  } else {
    if (length == sizeof(kPongMsg) &&
        memcmp(payload, kPongMsg, sizeof(kPongMsg)) == 0) {
      gPongCount++;
      setLed((gPongCount % 2) != 0);
      Serial.print(F("pong rx="));
      Serial.println(gPongCount);
    }
  }
}

void printPartitionId() {
  uint32_t pid = gThread.partitionId();
  snprintf(gPartitionId, sizeof(gPartitionId), "%08X", pid);
}

}  // namespace

void setup() {
  Serial.begin(115200);
  while (!Serial) { delay(10); }

  Serial.print(F("=== Reboot Recovery: "));
  Serial.println(ROLE == DemoRole::PONGER ? F("PONGER") : F("PINGER"));

#if defined(LED_BUILTIN)
  pinMode(LED_BUILTIN, OUTPUT);
  setLed(false);
#endif

  // Check if dataset is already configured (survived reboot)
  bool hasDataset = gThread.datasetConfigured();
  Serial.print(F("boot=0 dataset_persisted="));
  Serial.println(hasDataset ? 1 : 0);

  if (!hasDataset) {
    // First boot: seed demo dataset
    Serial.println(F("Seeding demo dataset..."));
    otOperationalDataset dataset = {};
    gThread.buildDemoDataset(&dataset);
    gThread.setActiveDataset(dataset);
    hasDataset = true;
  }

  // Start Thread WITHOUT wiping settings
  if (ROLE == DemoRole::PONGER) {
    gThread.beginAsRouter(false);  // false = preserve settings
    gThread.openUdp(kPingPort, udpReceiveCallback, nullptr);
  } else {
    gThread.beginAsChild(false);   // false = preserve settings
    gThread.openUdp(kPongPort, udpReceiveCallback, nullptr);
  }

  Serial.println(F("phase=attaching"));
}

void loop() {
  gThread.process();

  bool nowAttached = gThread.attached();

  // Detect attach transition
  if (!gWasAttached && nowAttached) {
    gWasAttached = true;
    gBootCount++;
    printPartitionId();
    Serial.print(F("boot="));
    Serial.print(gBootCount);
    Serial.print(F(" phase=ready role="));
    Serial.print(gThread.roleName());
    Serial.print(F(" rloc16=0x"));
    Serial.print(gThread.rloc16(), HEX);
    Serial.print(F(" part="));
    Serial.print(gPartitionId);
    Serial.print(F(" dataset="));
    Serial.println(gThread.datasetConfigured() ? 1 : 0);

    // LED blink pattern on successful attach
    setLed(true);
    delay(200);
    setLed(false);
  } else if (gWasAttached && !nowAttached) {
    gWasAttached = false;
    gPeerKnown = false;
    Serial.println(F("phase=detached"));
  }
  gWasAttached = nowAttached;

  // Pinger: re-discover leader and send pings
  if (ROLE == DemoRole::PINGER && nowAttached && !gPeerKnown) {
    otIp6Address leaderAddr;
    if (gThread.getLeaderRloc(&leaderAddr)) {
      gPeerAddr = leaderAddr;
      gPeerKnown = true;
      Serial.println(F("leader discovered!"));
    }
  }

  if (ROLE == DemoRole::PINGER && gPeerKnown &&
      (millis() - gLastPingMs > 2000UL)) {
    gLastPingMs = millis();
    gPingCount++;
    bool ok = gThread.sendUdp(gPeerAddr, kPingPort, kPingMsg, sizeof(kPingMsg));
    if (ok) {
      Serial.print(F("ping tx="));
      Serial.println(gPingCount);
    } else {
      gPingFailCount++;
      Serial.print(F("ping FAIL tx="));
      Serial.print(gPingCount);
      Serial.print(F(" fails="));
      Serial.println(gPingFailCount);
    }
  }

  // Status every 10 seconds
  if (millis() - gLastStatusMs > 10000UL) {
    gLastStatusMs = millis();
    printPartitionId();
    Serial.print(F("boot="));
    Serial.print(gBootCount);
    Serial.print(F(" role="));
    Serial.print(ROLE == DemoRole::PONGER ? F("ponger") : F("pinger"));
    Serial.print(F(" thread="));
    Serial.print(gThread.roleName());
    Serial.print(F(" attached="));
    Serial.print(nowAttached ? 1 : 0);
    Serial.print(F(" part="));
    Serial.print(gPartitionId);
    Serial.print(F(" pings="));
    Serial.print(gPingCount);
    Serial.print(F(" pongs="));
    Serial.print(gPongCount);
    Serial.print(F(" fails="));
    Serial.print(gPingFailCount);
    Serial.print(F(" peer="));
    Serial.println(gPeerKnown ? 1 : 0);
  }
}
