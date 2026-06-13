# Thread Missing Features Plan - 2026-06-07

## Current State

All core Thread features work on two XIAO nRF54L15 boards:
- Leader/child/router roles ✅
- Sleepy end device (SED) ✅
- UDP unicast/multicast ≤95 bytes ✅
- MeshCoP commissioner/joiner ✅
- PSK-based commissioning ✅
- Dataset persistence ✅
- Ping/pong demo with LED ✅

## Missing Features (Priority Order)

### 1. 6LoWPAN IP Fragmentation (Critical)

**Problem**: UDP payloads >95 bytes fail. `OPENTHREAD_CONFIG_IP6_FRAGMENTATION_ENABLE` defaults to 0. Radio PSdu is 127 bytes.

**Impact**: Blocks all real Matter traffic (Matter messages are typically 200-500 bytes).

**Fix**: Enable `OPENTHREAD_CONFIG_IP6_FRAGMENTATION_ENABLE=1` in `openthread-core-user-config.h`.

**Risk**: Previous attempt "broke runtime completely" — boards went silent. May need platform-level fixes.

### 2. Reboot Recovery Test

**Problem**: No automated test for dataset persistence and re-attach after reboot.

**Impact**: Can't prove Thread network survives power cycles.

**Fix**: Create `ThreadExperimentalRebootRecoveryPingPong` that tests:
- Dataset survives pyocd reset
- Board re-attaches to same network
- Ping/pong resumes after reboot

### 3. Reference Network Attach

**Problem**: No example for attaching to external Thread networks (OTBR, Zephyr/NCS, Nordic CLI).

**Impact**: Can't interoperate with real Thread ecosystems.

**Fix**: Create `ThreadExperimentalReferenceAttach` that:
- Accepts dataset TLV hex via serial
- Attaches to external network
- Reports attach status

### 4. Reconnect Stress Test

**Problem**: No test for detach/reattach resilience.

**Impact**: Can't prove child survives parent changes, channel changes, etc.

**Fix**: Create `ThreadExperimentalReconnectStress` that:
- Repeatedly detaches and reattaches
- Tracks success/failure rate
- Tests different detach causes

### 5. Channel Agility

**Problem**: No channel change support.

**Impact**: Required for Thread certification.

**Fix**: Implement channel change via MLE or manual command.

## Implementation Order

1. 6LoWPAN fragmentation (highest impact)
2. Reboot recovery test (quick win)
3. Reference network attach (quick win)
4. Reconnect stress test (medium effort)
5. Channel agility (deferred)
