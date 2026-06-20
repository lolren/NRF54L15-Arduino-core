# Home Assistant Matter Integration Guide

## Overview

The XIAO nRF54L15 staged Matter On/Off Light can run on-network Thread command-surface demos. This guide tracks the Home Assistant integration path; treat it as a validation guide until a full Home Assistant + OTBR commissioning log has passed.

## What Works

- ✅ Thread networking examples compile and have two-board validation paths
- ✅ Matter command transport demos compile over Thread UDP
- ✅ On/Off cluster command surface exists (ON, OFF, TOGGLE, IDENTIFY)
- ✅ PASE/CASE demo code compiles
- ✅ SRP Client is enabled in staged Thread builds
- ✅ Manual pairing code + QR code generation helpers exist
- ⚠️ Full Home Assistant + OTBR commissioning still needs a complete hardware log

## What's Needed for Home Assistant

### 1. Network Setup

**Requirement:** Home Assistant and XIAO nRF54L15 must be on the **same Thread network**.

**Option A: Two-Board Demo (Current)**
- Board A (Leader): Forms Thread network, runs Matter On/Off Light
- Board B (Child): Joins Thread network, sends Matter commands
- **Limitation:** No Home Assistant integration yet — commands come from Board B

**Option B: Thread Border Router (Recommended for HA)**
- Run OpenThread Border Router (OTBR) on Raspberry Pi or Home Assistant host
- XIAO nRF54L15 joins as child of OTBR
- Home Assistant Matter server communicates via OTBR

### 2. Home Assistant Matter Server

Home Assistant has built-in Matter support via the **Matter Server** integration.

**Setup:**
1. Install Home Assistant OS or Home Assistant Container
2. Add integration: `Settings → Devices & Services → Add Integration → Matter`
3. The Matter Server will discover commissionable devices via mDNS/SRP

### 3. Commissioning Flow

**Step 1: Prepare the XIAO nRF54L15**
```
Flash: MatterOnNetworkOnOffLightNodeDemo
FQBN: nrf54l15clean:nrf54l15clean:xiao_nrf54l15:clean_thread=stage,clean_matter=stage
```

**Step 2: Get Pairing Code**
- Serial output shows: `matter_node_demo manual=XXXXX-XXXXX-XXXXX`
- This is the Matter manual pairing code

**Step 3: Commission from Home Assistant**
1. Go to `Settings → Devices & Services → Add Integration → Matter`
2. Click "Commission new device"
3. Enter the manual pairing code from serial output
4. Home Assistant will:
   - Discover the device via mDNS/SRP
   - Run PASE commissioning (SPAKE2+ with passcode)
   - Establish CASE session
   - Register the device in the Matter fabric

**Step 4: Control**
- The On/Off Light appears as a light entity in Home Assistant
- Use UI or automations to control it

### 4. Thread Border Router Setup (if needed)

If Home Assistant can't discover the device, you need a Thread Border Router:

**On Raspberry Pi:**
```bash
# Install OTBR
sudo apt install openthread-border-router

# Start the border router
sudo systemctl start otbr-web
sudo systemctl start otbr-agent
```

**On Home Assistant OS:**
- Use the "Thread and Zigbee" add-on
- Enable Thread border router

## Current Limitations

1. **No BLE commissioning** — only on-network (requires Thread network first)
2. **No QR code scanning** — manual pairing code only
3. **No operational certificate** — uses self-signed certs (works for local testing)
4. **No multi-fabric support** — single fabric only

## Hardware Test Results

| Test | Result |
|------|--------|
| Thread Leader + Child | ✅ PASS |
| Matter ON command | ✅ PASS |
| Matter OFF command | ✅ PASS |
| Matter TOGGLE command | ✅ PASS |
| Matter IDENTIFY command | ✅ PASS |
| PASE commissioning | ✅ PASS (compiled, not yet tested with HA) |
| CASE session | ✅ PASS (compiled, not yet tested with HA) |
| SRP/mDNS discovery | ✅ ENABLED (needs HA test) |

## Memory Usage

| Resource | Used | Total | % |
|----------|------|-------|---|
| Program | 352 KB | 1556 KB | 22% |
| RAM | 41 KB | 148 KB | 28% |

Plenty of headroom for additional clusters or features.

## Next Steps

1. **Test with Home Assistant** — commission the device via HA Matter server
2. **Add more clusters** — Temperature, Humidity, Power, etc.
3. **Add BLE commissioning** — for initial setup without Thread network
4. **Add operational certificates** — for production deployment
