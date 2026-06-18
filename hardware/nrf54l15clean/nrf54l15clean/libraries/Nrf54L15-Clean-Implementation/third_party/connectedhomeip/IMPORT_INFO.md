# CHIP SDK Import Documentation

## Source
- **Repository:** https://github.com/project-chip/connectedhomeip
- **Tag:** v1.4.2.0
- **Commit:** 06523c22640ceb8b89f9a11ff2325a4481a178a3
- **Import Date:** 2026-06-07

## Import Strategy
Minimal subset for Matter On/Off Light device. Full SDK is ~10,000+ files; we import ~250 files.

## Directory Structure
```
third_party/connectedhomeip/
├── config/
│   └── arduino/
│       └── CHIPProjectConfig.h  (Arduino-specific config)
├── src/
│   ├── lib/
│   │   ├── core/          (already imported: CHIPError, CHIPKeyIds, etc.)
│   │   ├── support/       (already imported: Base64, BytesToHex, etc.)
│   │   ├── asn1/          (ASN.1 for certificates)
│   │   └── tlv/           (TLV encoding/decoding)
│   ├── system/            (event loop, timers, packet buffers)
│   ├── inet/              (IP transport abstraction)
│   ├── crypto/            (crypto abstraction layer)
│   ├── ble/               (BLE transport)
│   ├── transport/         (secure sessions, message framing)
│   ├── messaging/         (exchange manager, reliable messaging)
│   ├── protocols/         (PASE, CASE, interaction model)
│   ├── app/               (interaction model engine, clusters)
│   ├── access/            (access control)
│   ├── credentials/       (fabric storage, certificates)
│   ├── platform/          (device layer)
│   └── setup_payload/     (QR codes, manual codes)
└── IMPORT_INFO.md         (this file)
```

## Excluded Features
- OTA Requestor
- ICD (Intermittently Connected Device) support
- Shell commands
- Detailed logging (CHIP_LOG_SIZE_OPTIMIZATION=1)
- BDX (Bulk Data Exchange)
- User Directed Commissioning
- NFC onboarding
- Rotating Device ID
- Extended Discovery

## Configuration
```c
// CHIPProjectConfig.h
#define CHIP_SYSTEM_CONFIG_NO_LOCKING 1
#define CHIP_SYSTEM_CONFIG_PACKETBUFFER_POOL_SIZE 8
#define CHIP_SYSTEM_CONFIG_NUM_TIMERS 8
#define CHIP_SYSTEM_CONFIG_PACKETBUFFER_CAPACITY_MAX 1024
#define CHIP_CONFIG_MAX_FABRICS 1
#define CHIP_CONFIG_MAX_EXCHANGE_CONTEXTS 4
#define CHIP_LOG_SIZE_OPTIMIZATION 1
```

## Build Integration
- Include paths added to `boards.txt` via `matter_seam_flags`
- Source files compiled via `platform.txt`
- Arduino loop provides event loop (no OS)

## Phase 0 Checklist
- [x] Clone CHIP SDK v1.4.2.0
- [x] Create directory structure
- [x] Create CHIPProjectConfig.h
- [ ] Create chip_all.h header
- [ ] Create chip_build_config.h verification
- [ ] Compile test: stub sketch includes CHIPProjectConfig.h
- [ ] Regression test: all Thread examples compile
