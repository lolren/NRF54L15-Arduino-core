/**
 * Thread Experimental Ping Pong
 *
 * Two-board demo: Board A (PONGER) listens for "PING" and replies "PONG",
 * toggling its LED. Board B (PINGER) sends "PING" every 2 seconds and
 * toggles its LED on each "PONG" reply.
 *
 * Both boards share the same Thread network via buildDemoDataset().
 *
 * Flash Board A with ROLE = PONGER, Board B with ROLE = PINGER.
 * Change line 28 to switch roles before compiling.
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

// Ping/pong protocol
constexpr uint16_t kPingPort = 49152;
constexpr uint16_t kPongPort = 49153;

static const uint8_t kPingMsg[] = { 'P', 'I', 'N', 'G' };
static const uint8_t kPongMsg[] = { 'P', 'O', 'N', 'G' };

// LED is active-low on XIAO nRF54L15
constexpr bool kLedActiveLow = true;

static void setLed(bool on) {
#if defined(LED_BUILTIN)
  digitalWrite(LED_BUILTIN, (on ^ kLedActiveLow) ? HIGH : LOW);
#endif
}

// State
uint32_t gPingCount = 0;
uint32_t gPongCount = 0;
uint32_t gPingFailCount = 0;
bool gPeerKnown = false;
otIp6Address gPeerAddr = {};
uint32_t gLastPingMs = 0;
uint32_t gLastStatusMs = 0;
uint32_t gLastReconnectMs = 0;
bool gWasAttached = false;
bool gWasPeerKnown = false;

void udpReceiveCallback(void* context, const uint8_t* payload,
                        uint16_t length, const otMessageInfo& info) {
  (void)context;

  // Remember peer address on first receive
  if (!gPeerKnown) {
    gPeerAddr = info.mPeerAddr;
    gPeerKnown = true;
    Serial.println(F("peer discovered!"));
  }

  if (ROLE == DemoRole::PONGER) {
    // Ponger: check for "PING" and reply "PONG"
    if (length == sizeof(kPingMsg) &&
        memcmp(payload, kPingMsg, sizeof(kPingMsg)) == 0) {
      gPongCount++;

      // Reply PONG to the sender on kPongPort
      gThread.sendUdp(info.mPeerAddr, kPongPort, kPongMsg, sizeof(kPongMsg));

      // Toggle LED on each PONG
      setLed((gPongCount % 2) != 0);

      Serial.print(F("pong tx="));
      Serial.println(gPongCount);
    }
  } else {
    // Pinger: check for "PONG" reply
    if (length == sizeof(kPongMsg) &&
        memcmp(payload, kPongMsg, sizeof(kPongMsg)) == 0) {
      gPongCount++;

      // Toggle LED on each PONG
      setLed((gPongCount % 2) != 0);

      Serial.print(F("pong rx="));
      Serial.println(gPongCount);
    }
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  Serial1.begin(115200);
  // Serial may not connect on nRF54L15 - use timeout instead

  Serial.print(F("=== Thread Ping Pong: "));
  Serial.println(ROLE == DemoRole::PONGER ? F("PONGER") : F("PINGER"));

#if defined(LED_BUILTIN)
  pinMode(LED_BUILTIN, OUTPUT);
  setLed(false);
#endif

  otOperationalDataset dataset = {};
  gThread.buildDemoDataset(&dataset);
  gThread.setActiveDataset(dataset);

  if (ROLE == DemoRole::PONGER) {
    // Ponger becomes router/leader, listens on kPingPort
    gThread.beginAsRouter(true);
    gThread.openUdp(kPingPort, udpReceiveCallback, nullptr);
    Serial.println(F("Ponger: waiting for Thread attach..."));
  } else {
    // Pinger attaches as child, listens on kPongPort
    gThread.beginAsChild();
    gThread.openUdp(kPongPort, udpReceiveCallback, nullptr);
    Serial.println(F("Pinger: waiting for Thread attach..."));
  }
}

void loop() {
  gThread.process();

  // Detect attach/detach transitions for reconnection
  bool nowAttached = gThread.attached();
  if (!gWasAttached && nowAttached) {
    Serial.println(F("thread attached!"));
    gWasAttached = true;
  } else if (gWasAttached && !nowAttached) {
    Serial.println(F("thread detached!"));
    gWasAttached = false;
    gPeerKnown = false;  // Clear peer on detach
  }
  gWasAttached = nowAttached;

  // Pinger: re-discover leader address when needed
  if (ROLE == DemoRole::PINGER && nowAttached && !gPeerKnown) {
    if (millis() - gLastReconnectMs > 1000UL) {
      gLastReconnectMs = millis();
      otIp6Address leaderAddr;
      if (gThread.getLeaderRloc(&leaderAddr)) {
        gPeerAddr = leaderAddr;
        gPeerKnown = true;
        Serial.println(F("leader address discovered!"));
      }
    }
  }

  // Pinger: send PING every 2 seconds when attached and peer is known
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

  // Print status every 10 seconds
  if (millis() - gLastStatusMs > 10000UL) {
    gLastStatusMs = millis();
    Serial.print(F("role="));
    Serial.print(ROLE == DemoRole::PONGER ? F("ponger") : F("pinger"));
    Serial.print(F(" thread="));
    Serial.print(gThread.roleName());
    Serial.print(F(" attached="));
    Serial.print(gThread.attached() ? 1 : 0);

    if (ROLE == DemoRole::PONGER) {
      Serial.print(F(" pongs="));
      Serial.print(gPongCount);
    } else {
      Serial.print(F(" pings="));
      Serial.print(gPingCount);
      Serial.print(F(" pongs="));
      Serial.print(gPongCount);
      Serial.print(F(" fails="));
      Serial.print(gPingFailCount);
    }
    Serial.print(F(" peer="));
    Serial.println(gPeerKnown ? 1 : 0);
  }
}
