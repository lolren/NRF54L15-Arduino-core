// ThreadExperimentalReconnectStress
//
// Two-board staged Thread attach/reconnect stress test. Flash the same sketch
// to two boards with Tools > Thread Core > Experimental Stage Core. One board
// becomes leader, the other becomes child.
//
// The child board cycles through:
//   1. Attach to network
//   2. Run UDP soak (send/recv payloads)
//   3. Detach from network
//   4. Reattach to network
//   5. Repeat
//
// The leader board stays up and responds to UDP.
//
// Serial markers:
//   thread_stress reason=attach role=child cycles=N
//   thread_stress reason=detach role=detached cycles=N
//   thread_stress reason=reattach role=child cycles=N
//   thread_stress reason=udp_soak sent=N recv=N cycles=N
//   thread_stress reason=heartbeat role=child cycles=N

#include <nrf54_all.h>

#if !defined(NRF54L15_CLEAN_OPENTHREAD_CORE_ENABLE) || \
    (NRF54L15_CLEAN_OPENTHREAD_CORE_ENABLE == 0)
#error "Enable Tools > Thread Core > Experimental Stage Core before building."
#endif

#include <string.h>

using xiao_nrf54l15::Nrf54ThreadExperimental;

namespace {

constexpr uint16_t kUdpPort = 61631U;
constexpr uint32_t kStatusIntervalMs = 2000UL;
constexpr uint32_t kSoakDurationMs = 10000UL;
constexpr uint16_t kMaxPayloadLength = 256U;
constexpr uint8_t kMagic = 0x54U;
constexpr uint8_t kPingType = 0x50U;
constexpr uint8_t kAckType = 0x41U;

Nrf54ThreadExperimental gThread;
Nrf54ThreadExperimental::Role gLastRole =
    Nrf54ThreadExperimental::Role::kUnknown;

enum class StressPhase : uint8_t {
  kAttach = 0U,
  kSoak = 1U,
  kDetach = 2U,
  kReattach = 3U,
};

uint16_t gCurrentSeq = 1U;
uint32_t gCycleCount = 0U;
uint32_t gAttachAttempts = 0U;
uint32_t gReattachAttempts = 0U;
uint32_t gUdpSent = 0U;
uint32_t gUdpRecv = 0U;
uint32_t gUdpLost = 0U;
uint32_t gCrcErrors = 0U;
uint32_t gLastStatusMs = 0U;
uint32_t gPhaseStartMs = 0U;
uint32_t gAckTimeoutMs = 0U;
StressPhase gCurrentPhase = StressPhase::kAttach;
bool gTestStarted = false;
bool gWaitingForAck = false;

void printStatus(const char* reason) {
  Serial.print("thread_stress reason=");
  Serial.print(reason);
  Serial.print(" role=");
  Serial.print(gThread.roleName());
  Serial.print(" rloc16=0x");
  Serial.print(gThread.rloc16(), HEX);
  Serial.print(" attached=");
  Serial.print(gThread.attached() ? 1 : 0);
  Serial.print(" cycles=");
  Serial.print(gCycleCount);
  Serial.print(" attach_attempts=");
  Serial.print(gAttachAttempts);
  Serial.print(" reattach_attempts=");
  Serial.print(gReattachAttempts);
  Serial.print(" udp_sent=");
  Serial.print(gUdpSent);
  Serial.print(" udp_recv=");
  Serial.print(gUdpRecv);
  Serial.print(" udp_lost=");
  Serial.print(gUdpLost);
  Serial.print(" crc_errors=");
  Serial.print(gCrcErrors);
  Serial.print(" phase=");
  Serial.print(static_cast<uint8_t>(gCurrentPhase));
  Serial.print(" err=");
  Serial.println(static_cast<int>(gThread.lastError()));
}

void onUdp(void*, const uint8_t* payload, uint16_t length,
           const otMessageInfo& info) {
  if (length < 4U) return;

  uint8_t type = payload[0];
  uint8_t magic = payload[1];
  uint16_t seq = (static_cast<uint16_t>(payload[2]) << 8) | payload[3];

  if (magic != kMagic) {
    gCrcErrors++;
    return;
  }

  gUdpRecv++;

  if (type == kPingType) {
    uint8_t ackBuf[4] = {kAckType, kMagic,
                         static_cast<uint8_t>(seq >> 8),
                         static_cast<uint8_t>(seq & 0xFF)};
    gThread.sendUdp(info.mPeerAddr, info.mPeerPort, ackBuf, sizeof(ackBuf));
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  const uint32_t waitStart = millis();
  while (!Serial && (millis() - waitStart) < 1500UL) {
  }

#if defined(NRF54L15_CLEAN_OPENTHREAD_CORE_ENABLE) && \
    (NRF54L15_CLEAN_OPENTHREAD_CORE_ENABLE != 0)
  if (gThread.beginAsRouter(true)) {
    Serial.println("thread_stress boot=ok");
  } else {
    Serial.println("thread_stress boot=failed");
  }
#else
  Serial.println(
      "Enable Tools > Thread Core > Experimental Stage Core (Leader/Child/Router + UDP).");
#endif
}

void loop() {
  gThread.process();

#if defined(NRF54L15_CLEAN_OPENTHREAD_CORE_ENABLE) && \
    (NRF54L15_CLEAN_OPENTHREAD_CORE_ENABLE != 0)
  const uint32_t now = millis();

  if ((now - gLastStatusMs) >= kStatusIntervalMs) {
    gLastStatusMs = now;
    printStatus("heartbeat");
  }

  if (!gTestStarted) {
    if (gThread.attached()) {
      gTestStarted = true;
      gPhaseStartMs = now;
      gAttachAttempts++;
      gLastRole = gThread.role();
      printStatus("attach");
      gCurrentPhase = StressPhase::kSoak;
      gThread.openUdp(kUdpPort, onUdp, nullptr);
    }
    return;
  }

  switch (gCurrentPhase) {
    case StressPhase::kSoak: {
      if (!gWaitingForAck && gThread.attached()) {
        otIp6Address leaderAddr = {};
        if (gThread.getLeaderRloc(&leaderAddr)) {
          uint8_t payload[4] = {kPingType, kMagic,
                                static_cast<uint8_t>(gCurrentSeq >> 8),
                                static_cast<uint8_t>(gCurrentSeq & 0xFF)};
          gThread.sendUdp(leaderAddr, kUdpPort, payload, sizeof(payload));
          gUdpSent++;
          gCurrentSeq++;
          gWaitingForAck = true;
          gAckTimeoutMs = now + 2000UL;
        }
      }

      if (gWaitingForAck && now > gAckTimeoutMs) {
        gUdpLost++;
        gWaitingForAck = false;
      }

      if ((now - gPhaseStartMs) >= kSoakDurationMs) {
        gCurrentPhase = StressPhase::kDetach;
        gPhaseStartMs = now;
        printStatus("udp_soak");
      }
      break;
    }

    case StressPhase::kDetach: {
      if (gThread.attached()) {
        if (gThread.restart(false)) {
          gCurrentPhase = StressPhase::kReattach;
          gPhaseStartMs = now;
          gCycleCount++;
          gReattachAttempts++;
          printStatus("detach");
        } else {
          printStatus("restart_failed");
        }
      }
      break;
    }

    case StressPhase::kReattach: {
      if (gThread.attached()) {
        gCurrentPhase = StressPhase::kSoak;
        gPhaseStartMs = now;
        gLastRole = gThread.role();
        printStatus("reattach");
      }
      break;
    }

    default:
      break;
  }
#endif
}
