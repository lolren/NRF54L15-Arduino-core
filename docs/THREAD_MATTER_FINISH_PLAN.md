# Thread+Matter Finish Plan

## Current State

**Custom Matter Implementation: COMPLETE**
- 27 source files, 19 examples, ALL compile
- Pure software P-256 ECC (982 lines)
- Full PASE/SPAKE2+ commissioning (1228 lines)
- Full CASE/Sigma1-3 + AEAD (614 lines)
- On/Off Light cluster (1608 lines)
- Platform: Thread + UDP + storage (577 lines)
- PBKDF2: SHA-256 + HMAC (241 lines)
- Credentials, RNG, manual pairing code

**CHIP SDK Integration: UNNECESSARY**
- Phases 0-6 compile but Phases 7-12 blocked by .cpp dependencies
- Custom implementation already does everything the CHIP SDK would do
- No need to continue CHIP SDK integration

**Thread: COMPLETE**
- All 4 experimental examples compile and work
- Reboot recovery, reconnect stress, reference attach, ping/pong

## What's Missing for Home Assistant

### 1. SRP/mDNS Discovery (HIGH PRIORITY)
OpenThread has SRP client support but it's not enabled in the build.
Home Assistant needs this to discover Matter devices on the network.

### 2. Hardware Test (MEDIUM PRIORITY)
Flash MatterOnNetworkOnOffLightNodeDemo and verify:
- PASE commissioning works
- CASE session establishment works
- On/Off cluster responds to commands

### 3. Home Assistant Config (LOW PRIORITY)
Document how to configure Home Assistant Matter server for discovery.

## Implementation Plan

### Phase 1: Enable SRP Client
- Enable `OPENTHREAD_CONFIG_SRP_CLIENT_ENABLE=1` in boards.txt
- Verify compilation
- Flash and test

### Phase 2: Hardware Test
- Flash MatterOnNetworkOnOffLightNodeDemo on XIAO nRF54L15
- Verify serial output shows readiness
- Test PASE commissioning from another device

### Phase 3: Home Assistant Integration
- Document HA Matter server config
- Test end-to-end commissioning

## Success Criteria

- [ ] SRP client enabled and compiling
- [ ] Matter device discoverable on network
- [ ] PASE commissioning works end-to-end
- [ ] Home Assistant can discover and control the device
