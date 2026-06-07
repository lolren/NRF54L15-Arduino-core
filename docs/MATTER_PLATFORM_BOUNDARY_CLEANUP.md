# Matter Platform Boundary Cleanup

**Target:** nRF54L15 Clean Arduino Core - Matter Implementation
**Date:** 2026-06-06
**Status:** Audit / Planning
**Scope:** `matter_*.h` / `matter_*.cpp` under `Nrf54L15-Clean-Implementation/src/`

---

## Executive Summary

The current Matter implementation is a **prototype/staging** implementation designed for proof-of-concept on-network commissioning. It violates Matter specification requirements in seven categories, ranging from critical (software crypto, hardcoded credentials) to architectural (no real CHIP message layer, no mDNS/SRP). This document catalogs every violation, pinpoints file locations, and proposes a migration path to production-ready Matter support.

**Priority Order:** Crypto -> Credentials -> Random -> SPAKE2+ -> Message Layer -> Certificates -> Discovery -> Access Control

---

## Violation 1: Software Cryptography Instead of Hardware CRACEN

**Severity:** CRITICAL
**Impact:** Performance, side-channel resistance, flash usage, spec compliance

### Current State

The implementation ships a complete software secp256r1 (P-256) elliptic curve library and SHA-256 implementation, bypassing the nRF54L15's CRACEN hardware crypto accelerator.

| Component | File | Lines | Description |
|-----------|------|-------|-------------|
| P-256 EC arithmetic | `matter_secp256r1.h` | All | Full header: BigNum256, Jacobian coords, ECDSA |
| P-256 EC implementation | `matter_secp256r1.cpp` | ~960 lines | Barrett reduction, windowed scalar multiply, ECDSA sign/verify |
| SHA-256 | `matter_pbkdf2.cpp` | Lines 18-120 | Full SHA-256 transform with round constants |
| HMAC-SHA256 | `matter_pbkdf2.cpp` | Lines 185-215 | HMAC construction using software SHA-256 |
| PBKDF2-HMAC-SHA256 | `matter_pbkdf2.cpp` | Lines 217-270 | Full PBKDF2 iteration loop |
| AES-CTR (software wrapper) | `matter_case_session.cpp` | Lines 62-92 | AES-CTR built on top of hardware ECB only |

### Hardware Capability (Available)

The nRF54L15 CRACEN subsystem provides:

| Hardware | HAL Class | Capability |
|----------|-----------|------------|
| PKE (Public Key Engine) | `CracenIkg` (in `nrf54l15_hal_security.cpp`) | ECDSA P-256 sign/verify, point multiply, pubkey derivation |
| RNG | `CracenRng` | True hardware TRNG |
| ECB | `Ecb` | AES-128/256 ECB encrypt/decrypt |
| KMU | `CracenIkg` | Key storage and management |

### Problems

1. **Performance:** Software scalar multiply uses 64 Jacobian doublings + additions per operation. CRACEN PKE completes the same in ~100us.
2. **Side-channel:** Software implementation has no constant-time guarantees. Barrett reduction, modular inverse (binary GCD), and windowed multiplication all leak through timing.
3. **Flash:** The software secp256r1 + SHA-256 + HMAC + PBKDF2 implementation consumes ~40KB of flash.
4. **Maintenance:** Two crypto implementations (software + HAL) must be kept in sync.

### Proposed Fix

**Phase 1 - Replace secp256r1 with CRACEN PKE:**

1. Remove `matter_secp256r1.h` and `matter_secp256r1.cpp` entirely.
2. Create `matter_cracen_crypto.h` / `.cpp` as a thin wrapper around `CracenIkg`:
   - `ecdsaSign(privateKeySlot, hash[32], r[32], s[32])` -> `CracenIkg::ecdsaSign()`
   - `ecdsaVerify(pubKey, hash[32], r[32], s[32])` -> `CracenIkg::ecdsaVerify()`
   - `scalarMultiplyBase(k, outPoint)` -> `CracenIkg::ecPublicKey()` or PKE point multiply
   - `generateKeyPair()` -> `CracenIkg::keyGen()` + `CracenIkg::ecPublicKey()`
3. Update all callers in `matter_pase_commissioning.cpp`, `matter_case_session.cpp` to use the new wrapper.

**Phase 2 - Replace SHA-256/HMAC/PBKDF2:**

1. Remove `matter_pbkdf2.h` and `matter_pbkdf2.cpp`.
2. SHA-256: Use the imported CHIP crypto layer (`chip::Crypto::SHA256`) when `NRF54L15_CLEAN_MATTER_CORE_ENABLE` is set. Fall back to a minimal verified SHA-256 for the staging build.
3. HMAC-SHA256: Same - delegate to `chip::Crypto::HMAC_SHA256`.
4. PBKDF2: Delegate to `chip::Crypto::PBKDF2_HMAC_SHA256`.

**Phase 3 - Replace AES:**

1. The current AES-CTR in `matter_case_session.cpp` uses hardware ECB with software counter management. Replace with proper AES-CCM via the imported CHIP crypto layer or a dedicated AES-CCM hardware wrapper.

---

## Violation 2: Hardcoded Credentials

**Severity:** HIGH
**Impact:** Security, interoperability, production readiness

### Current State

All Matter identity credentials are hardcoded to demo values across multiple files:

| Credential | Value | Files |
|------------|-------|-------|
| Setup PIN Code | `20202021` | `matter_foundation_target.cpp:321,337`, `matter_onnetwork_onoff_light.cpp:1116`, `matter_pase_commissioning.h:211` |
| Discriminator | `3840` (0x0F00) | `matter_foundation_target.cpp:322,338`, `matter_onnetwork_onoff_light.cpp:1117`, `matter_pase_commissioning.h:212` |
| Vendor ID | `12` (Connected Home IP Working Group) | `matter_foundation_target.cpp:323`, `matter_onnetwork_onoff_light.cpp:1118` |
| Product ID | `1` | `matter_foundation_target.cpp:324`, `matter_onnetwork_onoff_light.cpp:1119` |
| Thread Passphrase | `"THREAD54"` | Example sketches only (not in library) |

### Problems

1. **Security:** Anyone can commission the device with the known PIN. No factory-provisioned unique credentials.
2. **Interoperability:** Vendor ID 12 is the spec's reserved "Connected Home IP Working Group" ID. Production devices MUST use a DA-assigned vendor ID.
3. **No provisioning path:** No mechanism to inject credentials from factory data, NVS, or secure element.

### Proposed Fix

1. **Define a `MatterDeviceIdentity` struct** in `matter_platform_nrf54l15.h`:
   ```cpp
   struct MatterDeviceIdentity {
     uint32_t setupPinCode;
     uint16_t discriminator;
     uint16_t vendorId;
     uint16_t productId;
   };
   ```

2. **Load from factory data:** Add `MatterPlatform::loadDeviceIdentity()` that reads from NVMC/Preferences storage. Fall back to compile-time defaults only in staging mode.

3. **Remove all hardcoded values** from:
   - `matter_foundation_target.cpp` - `buildDefaultThreadOnNetworkQrPayload()`, `buildDefaultThreadOnNetworkManualPayload()`
   - `matter_onnetwork_onoff_light.cpp` - `buildDefaultIdentity()`
   - `matter_pase_commissioning.h` - default member initializers for `setupPinCode_` and `discriminator_`

4. **Provide a sketch-level API:** `MatterPlatform::setDeviceIdentity(MatterDeviceIdentity)` called from `setup()` before `begin()`.

---

## Violation 3: Weak/Inconsistent Random Number Generation

**Severity:** HIGH
**Impact:** Cryptographic security, key compromise

### Current State

Three different PRNG implementations exist:

| Source | File | Quality |
|--------|------|---------|
| `Secp256r1::randomBytes()` | `matter_secp256r1.cpp:896-924` | CRACEN RNG XOR'd with XorShift64 fallback |
| `MatterPaseCommissioning::generateRandom()` | `matter_pase_commissioning.cpp:1195-1207` | **LCG (Linear Congruential Generator)** - cryptographically broken |
| `MatterCaseSession::generateRandom()` | `matter_case_session.cpp:574-578` | `micros() ^ millis()` - **not random at all** |

### Problems

1. `MatterPaseCommissioning::generateRandom()` uses a 64-bit LCG (`state * 6364136223846793005 + 1442695040888963407`). This is trivially predictable given a few outputs.
2. `MatterCaseSession::generateRandom()` uses `micros() ^ (millis() >> (i % 8))` - sequential bytes with no entropy. Sigma randoms (32 bytes each) are fully predictable.
3. Inconsistent sources mean the same "random" value could be generated differently across protocol phases.

### Proposed Fix

1. **Single random source:** Create `matter_random.h` with one function:
   ```cpp
   void matterGetRandomBytes(uint8_t* out, size_t len);
   ```
2. **Backed by CRACEN RNG exclusively:** Use `CracenRng::fill()` with a timeout. No software fallback for crypto contexts.
3. **Replace all three generators** with calls to `matterGetRandomBytes()`.

---

## Violation 4: SPAKE2+ Implementation May Not Match Spec

**Severity:** HIGH
**Impact:** Interoperability with any Matter controller

### Current State

`matter_pase_commissioning.cpp` implements a custom SPAKE2+ flow with several spec deviations:

| Issue | File | Location |
|-------|------|----------|
| `computeSpake2pZ()` generates a **fresh random scalar** instead of reusing the ephemeral `x`/`y` from the X/Y computation | `matter_pase_commissioning.cpp:608-648` | Lines 631-648 |
| Shared secret derivation: `SHA256(Z \|\| V \|\| w0)` - V is never properly computed | `matter_pase_commissioning.cpp:650-668` | V field is all zeros |
| Confirmation strings use different context labels (`kSpake2pContextAlpha` vs `kSpake2pContextBeta`) - both are `"SPAKE2P Key Confirmation"` | `matter_pase_commissioning.cpp:41-42` | Should be `"SPAKE2P Key Confirmation"` for both per spec |
| w0/w1 derivation: `PBKDF2(passcode, salt \|\| "SPAKE2P Key Salt", ...)` - the Matter spec uses `salt \|\| w0s \|\| "SPAKE2P Key Salt"` for w1s | `matter_pase_commissioning.cpp:149-226` | Partially correct but w1s context string concatenation may be off |
| No proper `V` computation for shared secret | `matter_pase_commissioning.cpp:650-668` | `session_.V` is never set |

### Problems

1. **Z computation is broken:** The prover and verifier will compute different Z values because `computeSpake2pZ()` generates a new random scalar instead of using the original ephemeral key. This means the shared secret will never match between peers unless both sides happen to generate the same random.
2. **V is never computed:** The shared secret formula includes V, which should be `w1 * (peerPoint - w0*G)`. Without V, the shared secret is incomplete.
3. **Even if both sides use the same code,** the fresh random in Z means the PASE handshake can only succeed between two devices running this exact code - not with any Matter controller.

### Proposed Fix

1. **Replace the entire PASE implementation** with the imported CHIP PASE implementation when `NRF54L15_CLEAN_MATTER_CORE_ENABLE` is set.
2. **For staging builds,** fix the Z computation to reuse the ephemeral scalar, compute V properly, and verify against known test vectors from the Matter spec.
3. **Add test vectors:** Include at least one known-good PASE test vector from the CHIP test suite to validate the implementation.

---

## Violation 5: Custom CHIP Message Layer - Not Wire-Compatible

**Severity:** MEDIUM
**Impact:** Interoperability with any Matter controller or device

### Current State

`matter_pase_commissioning.cpp` implements a custom message framing layer:

| Component | File | Issue |
|-----------|------|-------|
| `MatterMessageHeader` struct | `matter_pase_commissioning.h:87-98` | Custom header format - not CHIP-compliant |
| `parseMessageHeader()` | `matter_pase_commissioning.cpp:1074-1119` | 20-byte header with custom field ordering |
| `buildMessageHeader()` | `matter_pase_commissioning.cpp:1121-1168` | Custom serialization |
| Message types | `matter_pase_commissioning.h:53-78` | Custom enum values |
| Exchange management | `matter_pase_commissioning.cpp:1170-1192` | Custom exchange/message ID generation |

The CHIP spec defines a specific message header format with:
- Exchange flags (1 byte)
- Session type (1 byte)
- Security flags (1 byte)
- Message counter (4 bytes, not 2)
- Source/destination node IDs (variable length, not fixed 4 bytes)
- Exchange ID (2 bytes)
- Protocol ID (vendor + protocol)
- Message type
- Optional ACK

### Problems

1. **Message counter is 16-bit** instead of the spec's 32-bit counter.
2. **Node IDs are fixed 32-bit** instead of variable-length (Matter uses 64-bit node IDs).
3. **No message encryption** at the transport layer - PASE-derived keys are not used for message protection.
4. **No reliable messaging** - the `kReliable` flag exists but there's no retransmission logic.
5. **No EMX (Encrypted Messaging Exchange)** framing.

### Proposed Fix

1. **Use the CHIP MessageLayer** when `NRF54L15_CLEAN_MATTER_CORE_ENABLE` is set.
2. **For staging builds,** document that the message format is prototype-only and not wire-compatible.
3. **Minimum fix:** Align header fields to spec (32-bit message counter, 64-bit node IDs, proper EMX framing).

---

## Violation 6: No Certificate Authority - Self-Signed Only

**Severity:** MEDIUM
**Impact:** Production security, fabric management

### Current State

`matter_case_session.cpp` implements certificate generation and verification:

| Issue | File | Location |
|-------|------|----------|
| `generateSelfSignedCert()` | `matter_case_session.cpp:304-350` | Only self-signed certs supported |
| `verifyCertificate()` | `matter_case_session.cpp:510-530` | Verifies against issuer pubkey - no chain validation |
| `CaseCertificate` struct | `matter_case_session.h:53-66` | Minimal cert: no serial number, no extensions, no validity chain |
| No PAI/PAC/DCER/NOCS support | N/A | Certificate types not implemented |

### Problems

1. **No CA hierarchy:** Matter requires a PAI (Product Attestation Issuer) -> PAC (Product Attestation Certificate) -> DCER (Decommissionable Certificate for Exchange of Root) -> NOCS (Node Operational Credentials) chain.
2. **No certificate parsing:** The custom `CaseCertificate` struct doesn't support X.509/DER encoding.
3. **No revocation:** No CRL or OCSP support.
4. **No fabric management:** Fabric IDs are derived from issuer hash, not assigned by a fabric admin.

### Proposed Fix

1. **Use CHIP CertificateLayer** when `NRF54L15_CLEAN_MATTER_CORE_ENABLE` is set.
2. **For staging builds,** document that certificates are self-signed test credentials only.
3. **Add factory data support:** Store PAI certificate and device attestation data in NVMC for production provisioning.

---

## Violation 7: No Access Control Implementation

**Severity:** LOW (for staging)
**Impact:** Production security

### Current State

The foundation target declares `kAccessControlClusterId` (0x001F) in its root endpoint cluster list (`matter_foundation_target.cpp:19-20`), but no access control logic exists:

- No ACL (Access Control List) storage
- No subject/fabric/endpoint/cluster/command filtering
- No access control callback in the message layer

### Proposed Fix

1. **Use CHIP AccessControlLayer** when `NRF54L15_CLEAN_MATTER_CORE_ENABLE` is set.
2. **For staging builds,** implement a minimal default ACL: allow all operations from the commissioning fabric.
3. **Add ACL storage** in Preferences/NVMC for production configuration.

---

## Violation 8: Discovery Still Incomplete

**Severity:** MEDIUM
**Impact:** On-network commissioning, device discovery

### Current State

`matter_foundation_target.cpp` reports discovery capabilities:

```cpp
// matter_foundation_target.cpp:64-88
constexpr bool kOpenThreadMdnsCoreEnabled = ...;
constexpr bool kOpenThreadMdnsPublicApiEnabled = ...;
constexpr bool kOpenThreadPlatformDnssdEnabled = ...;
constexpr bool kOpenThreadSrpClientEnabled = ...;
```

Matter-stage builds now enable the OpenThread DNS client, SRP client, and ECDSA path. `MatterOnNetworkOnOffLightNode` can queue a commissionable `_matterc._udp` record through OpenThread SRP when the commissioning window is open and Thread is attached, and it now requests SRP host/service removal when the window closes instead of only clearing local state. Generic Thread-only builds still leave this off to avoid pulling SRP/DNS/ECDSA into every Thread sketch.

### Problems

1. **No infrastructure mDNS/DNS-SD path:** Devices do not yet advertise `_matterc._udp` on non-Thread infrastructure networks.
2. **SRP needs real-network validation:** Commissionable SRP queueing and unregister requests compile, but must be tested against a Thread border router with SRP server data.
3. **No operational node registration:** After commissioning, devices still don't register via SRP to `_matter._tcp`.
4. **No CHIP DNSSD bridge:** The staged publisher is local to the current Matter node wrapper, not a complete CHIP Dnssd platform layer.

### Proposed Fix

1. **Validate staged SRP:** Use a real Thread border router/SRP server and confirm `_matterc._udp` appears while the commissioning window is open.
2. **Implement CHIP Dnssd layer:** Bridge OpenThread SRP and future infrastructure mDNS/DNS-SD to CHIP's Dnssd interface.
3. **Add operational registration:** Register `_matter._tcp` after fabric/CASE commissioning state exists.
4. **Keep infrastructure mDNS separate:** Do not enable `OPENTHREAD_CONFIG_MULTICAST_DNS_ENABLE` by default until there is a platform interface and measured memory/power impact.

---

## Migration Plan

### Phase 1: Credential and Random (Week 1-2)

**Goal:** Eliminate hardcoded credentials and fix random number generation.

| Task | Files | Effort |
|------|-------|--------|
| Define `MatterDeviceIdentity` struct | `matter_platform_nrf54l15.h` | 1h |
| Add `setDeviceIdentity()` / `loadDeviceIdentity()` | `matter_platform_stage.cpp` | 2h |
| Remove hardcoded values from foundation target | `matter_foundation_target.cpp` | 1h |
| Remove hardcoded values from onnetwork light | `matter_onnetwork_onoff_light.cpp` | 1h |
| Remove hardcoded defaults from PASE | `matter_pase_commissioning.h` | 0.5h |
| Create `matter_random.h` with CRACEN RNG | New file | 1h |
| Replace 3 PRNG implementations | `matter_secp256r1.cpp`, `matter_pase_commissioning.cpp`, `matter_case_session.cpp` | 2h |

**Verification:** Unit tests for random output uniqueness; credential loading from factory data.

### Phase 2: Hardware Crypto (Week 3-4)

**Goal:** Replace software crypto with CRACEN hardware.

| Task | Files | Effort |
|------|-------|--------|
| Create `matter_cracen_crypto.h/.cpp` wrapper | New files | 4h |
| Wire ECDSA sign/verify through CracenIkg | `matter_cracen_crypto.cpp` | 4h |
| Wire scalar multiply through CracenIkg | `matter_cracen_crypto.cpp` | 2h |
| Wire key generation through CracenIkg | `matter_cracen_crypto.cpp` | 2h |
| Replace SHA-256/HMAC/PBKDF2 with CHIP crypto | `matter_cracen_crypto.cpp` | 2h |
| Update PASE callers to use new crypto | `matter_pase_commissioning.cpp` | 2h |
| Update CASE callers to use new crypto | `matter_case_session.cpp` | 2h |
| Remove `matter_secp256r1.h/.cpp` | Delete | 0.5h |
| Remove `matter_pbkdf2.h/.cpp` | Delete | 0.5h |

**Verification:** ECDSA test vectors match CRACEN output; flash reduction measured (~40KB saved).

### Phase 3: SPAKE2+ and PASE Fix (Week 5-6)

**Goal:** Fix SPAKE2+ to match Matter spec.

| Task | Files | Effort |
|------|-------|--------|
| Fix `computeSpake2pZ()` to reuse ephemeral scalar | `matter_pase_commissioning.cpp` | 2h |
| Implement proper V computation | `matter_pase_commissioning.cpp` | 2h |
| Fix shared secret derivation | `matter_pase_commissioning.cpp` | 1h |
| Add known test vectors | New test file | 2h |
| Validate against CHIP PASE test vectors | Test harness | 4h |

**Verification:** PASE handshake succeeds between two devices; test vectors pass.

### Phase 4: CHIP Core Integration (Week 7-8)

**Goal:** Bridge to real CHIP implementation when `NRF54L15_CLEAN_MATTER_CORE_ENABLE` is set.

| Task | Files | Effort |
|------|-------|--------|
| Enable OpenThread mDNS/SRP config | `openthread-core-user-config.h` | 1h |
| Wire CHIP MessageLayer | Bridge code | 8h |
| Wire CHIP Dnssd layer | Bridge code | 4h |
| Wire CHIP AccessControlLayer | Bridge code | 4h |
| Wire CHIP CertificateLayer | Bridge code | 4h |
| Conditional compilation guards | All matter_*.h/.cpp | 4h |

**Verification:** CHIP test suite passes; mDNS discovery works; access control enforced.

### Phase 5: Production Hardening (Week 9-10)

**Goal:** Factory provisioning, secure storage, production credentials.

| Task | Files | Effort |
|------|-------|--------|
| Factory data schema for credentials | `matter_platform_stage.cpp` | 2h |
| Secure storage for private keys (KMU) | `matter_cracen_crypto.cpp` | 4h |
| Certificate chain provisioning | Bridge code | 4h |
| Production ACL configuration | Bridge code | 2h |
| End-to-end commissioning test | Test harness | 8h |

**Verification:** Full commissioning flow with production Matter controller; key storage in KMU; certificate chain validation.

---

## File Inventory

### Files to Modify

| File | Changes |
|------|---------|
| `matter_platform_nrf54l15.h` | Add `MatterDeviceIdentity` struct |
| `matter_platform_stage.h` | Add identity API declarations |
| `matter_platform_stage.cpp` | Implement identity loading from factory data |
| `matter_foundation_target.cpp` | Remove hardcoded credentials, use `MatterDeviceIdentity` |
| `matter_onnetwork_onoff_light.cpp` | Remove hardcoded credentials, use `MatterDeviceIdentity` |
| `matter_pase_commissioning.h` | Remove hardcoded defaults, add `matter_random.h` include |
| `matter_pase_commissioning.cpp` | Fix SPAKE2+ Z/V computation, replace PRNG, use CRACEN crypto |
| `matter_case_session.h` | Add `matter_random.h` include |
| `matter_case_session.cpp` | Replace PRNG, use CRACEN crypto, fix AES-CCM |

### Files to Create

| File | Purpose |
|------|---------|
| `matter_random.h` | Single CRACEN RNG interface |
| `matter_cracen_crypto.h` | CRACEN PKE/SHA/AES wrapper |
| `matter_cracen_crypto.cpp` | Hardware crypto implementation |

### Files to Delete (Phase 2+)

| File | Reason |
|------|--------|
| `matter_secp256r1.h` | Replaced by CRACEN PKE |
| `matter_secp256r1.cpp` | Replaced by CRACEN PKE |
| `matter_pbkdf2.h` | Replaced by CHIP crypto |
| `matter_pbkdf2.cpp` | Replaced by CHIP crypto |

---

## Risk Assessment

| Risk | Mitigation |
|------|-----------|
| CRACEN PKE API may not support all operations needed | Test each operation independently before integration |
| CHIP core import may conflict with custom code | Use `#if NRF54L15_CLEAN_MATTER_CORE_ENABLE` guards for clean separation |
| SPAKE2+ test vectors may not be available | Derive from CHIP source code test suite |
| Flash budget may be tight with CHIP core | Profile flash usage after each phase; remove staging code aggressively |
| Secure partition (TrustZone) may restrict CRACEN access | Verify CRACEN is accessible from NONSECURE context; use SMC calls if needed |

---

## References

- Matter Specification v1.1 - PASE, CASE, SPAKE2+
- CHIP Source: `connectedhomeip/src/credentials/`, `src/crypto/`, `src/messaging/`
- nRF54L15 Product Specification - CRACEN, KMU, PKE sections
- `MATTER_RUNTIME_OWNERSHIP.md` - Current ownership model
- `MATTER_FOUNDATION_MANIFEST.md` - Foundation cluster manifest
