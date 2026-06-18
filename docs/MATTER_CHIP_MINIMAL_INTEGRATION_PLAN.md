# Matter CHIP Minimal Integration Plan

## Phase 0: Foundation ✅ COMPLETE
CHIP SDK v1.4.2.0 imported, CHIPProjectConfig.h created.

## Phase 1: CHIP System Layer ✅ COMPLETE
SystemLayerImplArduino.h — inline timer/work scheduler.

## Phase 2: CHIP Inet Layer ✅ COMPLETE
InetArduino.h — inline OpenThread UDP wrapper.

## Phase 3: CHIP Crypto Layer ✅ COMPLETE
CryptoArduino.h — inline crypto (DRBG via Arduino random(), rest stubbed).

## Phase 4: CHIP TLV & Support Layer ✅ COMPLETE
TLV Reader/Writer headers compile inline.

## Phase 5: CHIP Transport Layer ✅ COMPLETE ✅ COMPLETE
**Goal:** CHIPoUDP transport over Thread.

### Phase 5.1: Transport Layer
- `transport/UDP/UDP.h` / `.cpp` — UDP transport
- `transport/PeerTransportManager.h` / `.cpp` — peer management
- `transport/TransportManager.h` / `.cpp` — transport manager

### Phase 5.2: Session Establishment
- `transport/SessionEstablishment.h` — session establishment
- `transport/SecureSession::BuildMessages` — secure message building

**Compile test:**
- [ ] Build stub sketch using TransportManager → ✅ PASS

## Phase 6: CHIP Messaging Layer ✅ COMPLETE
**Goal:** Exchange messages over CHIP transport.

### Phase 6.1: Messaging
- `messaging/ExchangeContext.h` / `.cpp` — exchange context
- `messaging/ExchangeManager.h` / `.cpp` — exchange manager
- `messaging/SystemPacketBuffer.h` — packet buffer
- `messaging/ReliableMessageManager.h` / `.cpp` — reliable messaging

**Compile test:**
- [x] Build stub sketch using ExchangeManager → ✅ PASS

---

## ⛔ BLOCKED: Phases 7-12

**Status:** Phases 7-12 are BLOCKED by cascading `.cpp` dependencies.

**Root Cause:** `arduino-cli` does NOT compile library `.cpp` files. Only `.ino` sketches are compiled. Phases 7-12 require `.cpp` files from `credentials`, `asn1`, `app`, `session`, `protocols/pase`, `protocols/case`, `protocols/interaction_model`, and `app/server` — none of which can be made fully inline without rewriting the entire CHIP SDK.

**Options:**
1. **Custom Matter implementation** (recommended) — Build Matter protocol handlers from scratch using only inline headers, leveraging our working Thread UDP transport.
2. **Custom build system** — Fork `arduino-cli` or create a CMake/Make build that compiles library `.cpp` files.
3. **Pause CHIP SDK integration** — Focus on Thread features and custom Matter implementation.

**Recommendation:** Option 1 — custom Matter implementation.

## Phase 7: CHIP Protocols ⛔ BLOCKED
**Goal:** PASE, CASE, Interaction Model. (Requires `.cpp` files)

## Phase 8: CHIP App Layer ⛔ BLOCKED
**Goal:** Clusters (On/Off, etc.) (Requires `.cpp` files)

## Phase 9: CHIP Credentials & Access Control ⛔ BLOCKED
**Goal:** Fabric management, credentials. (Requires `.cpp` files)

## Phase 10: Setup Payload & DNS-SD ⛔ BLOCKED
**Goal:** QR code setup payload, mDNS discovery. (Requires `.cpp` files)

## Phase 11: BLE Commissioning (CHIPoBLE) ⛔ BLOCKED
**Goal:** BLE pairing (optional for Thread-only devices). (Requires `.cpp` files)

## Phase 12: End-to-End Integration — Home Assistant Test ⛔ BLOCKED
**Goal:** Full Matter On/Off Light working with Home Assistant. (Requires all above)

---

## Build System Notes (2026-06-08)

**Key Finding:** arduino-cli does NOT compile library `.cpp` files.
All implementations must be inline headers (no `.cpp` dependencies).

**Pattern for each phase:**
1. Copy needed headers from CHIP SDK
2. Patch out references to missing types
3. Add missing stub headers (nlassert, etc.)
4. Test with inline compile test
