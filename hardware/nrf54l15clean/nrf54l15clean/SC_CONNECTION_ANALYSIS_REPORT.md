# BLE Secure Connections (SC) Connection Failure Analysis Report

> Current status note, 2026-06-26: this report documents the first HID pairing
> root cause that was found during LE Secure Connections work. That issue was
> real, but it is no longer the whole current failure. For the active Pixel /
> Snapdragon HID pairing state, test commands, traces, and remaining hypotheses,
> start with `docs/BLE_SECURE_CONNECTIONS_HANDOVER_2026_06_26.md`.

## Summary

The `blehid_mouse` sketch never connects to the phone because the **peripheral never initiates the pairing procedure**. The connection is established at the link layer, but the SMP (Security Manager Protocol) security request is never sent by the peripheral, so the phone has no trigger to start encryption/pairing. Without user-facing pairing, the phone silently refuses to connect or immediately disconnects.

---

## Root Cause

### The Problem Chain

1. **HID service requires security** — All `BLEHidAdafruit` characteristics use `SECMODE_ENC_NO_MITM` (0x21) for read permissions. The HAL's `customGattHasSecureHidService()` correctly detects this.

2. **Central (phone) initiates connection** — When the phone connects as central, the HAL sets `connectionSmpSecurityRequestPending_ = true` in `startCentralConnection()` (file: `nrf54l15_hal_ble_scanning_connections.inc:2027`).

3. **Peripheral sends SMP Security Request** — The peripheral's event tail code (`nrf54l15_hal_ble_peripheral_event_tail.inc:294-306`) checks this flag and transmits `SMP_CODE_SECURITY_REQUEST` (0x0B) with `kSmpAuthReqSecureConnectionsMask` set.

4. **BUT the Bluefruit compatibility layer never calls `sendSmpSecurityRequest()`** — The `BluefruitCompatManager::handleConnectionEdge()` (file: `bluefruit.cpp:2455-2498`) handles the peripheral connection event. It:
   - Enables background connection service
   - Dispatches the user connect callback
   - Dispatches CCCD=0 callbacks
   - **Does NOT set `connectionSmpSecurityRequestPending_`**
   - **Does NOT call `radio_.sendSmpSecurityRequest()`**

5. **The HAL's auto-security-request path only fires for central-role connections** — `startCentralConnection()` sets the flag, but the HID mouse sketch is a **peripheral** (advertising, phone connects to it). The peripheral's `startPeripheralConnection()` path does NOT auto-set this flag.

6. **Result**: The phone connects at the link layer, receives no SMP security request, and either:
   - Times out waiting for encryption
   - Disconnects immediately because the HID service requires security
   - Shows "cannot connect" in the Bluetooth settings

---

## Detailed Code Path Analysis

### What Happens When Phone Connects (Peripheral Role)

```
Phone sends CONNECT_IND
  → BleRadio::startPeripheralConnection()
    → Sets connected_ = true, connectionRole_ = kPeripheral
    → Does NOT set connectionSmpSecurityRequestPending_  ← BUG
    → Builds advertising/scan response packets

BluefruitCompatManager::handleConnectionEdge(true)
  → last_connection_role_ = kPeripheral
  → radio_.setBackgroundConnectionServiceEnabled(true)
  → Dispatches user connect_callback
  → Dispatches CCCD=0 callbacks
  → Does NOT trigger security  ← BUG

idleService() [peripheral role]
  → radio_.prefetchConnectionSecurityMaterial(10000UL)  // Only if !isConnectionEncrypted()
  → maybeDispatchSecurityCallbacks()
    → isPairingInProgress() = false (no procedure started)
    → isSecurityProcedureActive() = false
    → Does nothing
```

### What Should Happen

```
Phone sends CONNECT_IND
  → BleRadio::startPeripheralConnection()
    → Sets connected_ = true, connectionRole_ = kPeripheral

BluefruitCompatManager::handleConnectionEdge(true)
  → last_connection_role_ = kPeripheral
  → radio_.setBackgroundConnectionServiceEnabled(true)
  → Dispatches user connect_callback
  → Dispatches CCCD=0 callbacks
  → *** radio_.sendSmpSecurityRequest() ***  ← MISSING

Peripheral event tail (next connection event)
  → connectionSmpSecurityRequestPending_ = true
  → Sends SMP_SECURITY_REQUEST (0x0B) with SC flag
  → Phone responds with pairing request
  → LE Secure Connections handshake proceeds
```

---

## Fix

### Option 1: Fix in BluefruitCompatManager (Recommended)

Add the security request call in `handleConnectionEdge()` for the peripheral role, after dispatching user callbacks:

**File**: `libraries/Bluefruit52Lib/src/bluefruit.cpp`
**Location**: After line ~2497 (end of `kPeripheral` block in `handleConnectionEdge`)

```cpp
// After the CCCD dispatch loop in the kPeripheral block:
if (Bluefruit.Periph.connect_callback_ != nullptr) {
  invokeBluefruitUserCallback(Bluefruit.Periph.connect_callback_, 0U);
}
// Dispatch CCCD=0 callbacks ...
for (uint8_t i = 0U; i < characteristic_count_; ++i) {
  BLECharacteristic* c = characteristics_[i];
  if (c != nullptr && c->_cccd_wr_cb_ != nullptr) {
    invokeBluefruitUserCallback(c->_cccd_wr_cb_, 0U, c, static_cast<uint16_t>(0U));
  }
}

// *** ADD THIS: Trigger SMP security request for peripheral role ***
// The HID service requires encryption. Without this, the phone connects
// at the link layer but never receives a pairing prompt and disconnects.
if (!radio_.isConnectionEncrypted()) {
  radio_.sendSmpSecurityRequest();
}
```

### Option 2: Fix in the HAL's startPeripheralConnection

**File**: `libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal_parts/nrf54l15_hal_ble_scanning_connections.inc`

In the peripheral connection path (around line 1984), add the same check that the central path has:

```cpp
// After: connectionSmpSecurityRequestPending_ = false;
// Add:
if (customGattHasSecureHidService()) {
  connectionSmpSecurityRequestPending_ = true;
  emitBleTrace("SMP_HID_SEC_REQ_PENDING");
}
```

**Note**: This is more invasive and couples the HAL to service-level knowledge. Option 1 is preferred because the Bluefruit layer already knows when security is needed (via the `sendSmpSecurityRequest()` API).

### Option 3: Application-Level Workaround (Temporary)

In the sketch, after `Bluefruit.begin()` and before advertising starts, manually trigger pairing on each connection by adding a connect callback:

```cpp
void onConnect(uint16_t conn_handle) {
  // Manually request pairing for HID devices
  Bluefruit.Security.requestPairing();
}

void setup() {
  // ... existing setup ...
  Bluefruit.Periph.setConnectCallback(onConnect);
  // ... rest of setup ...
}
```

**Caveat**: `Bluefruit.Security.requestPairing()` returns `false` if the connection is already encrypted or if the SMP state machine is not idle. It also may not work correctly for the peripheral role because the peripheral cannot initiate the pairing request directly — it can only send a security request to prompt the central.

---

## Why This Happened

The original Adafruit nRF52 Bluefruit core had a different BLE softdevice that handled security automatically. When this core was ported to the nRF54L15 with a new BLE HAL, the security request path was only implemented for the **central** role (where the HAL auto-detects HID service security requirements). The **peripheral** role path was left without an automatic security initiation mechanism.

The `sendSmpSecurityRequest()` API exists and is correct, but it is never called by the Bluefruit compatibility layer on the peripheral side.

---

## Verification

After applying the fix, the connection flow should be:

1. Peripheral advertises (ADV_IND on channels 37/38/39)
2. Phone sends CONNECT_IND
3. Peripheral enters connection, sends `SMP_SECURITY_REQUEST` with SC flag
4. Phone responds with `SMP_PAIRING_REQUEST`
5. LE Secure Connections handshake proceeds (ECDH key exchange)
6. Phone displays pairing passkey (if MITM required) or auto-approves (Just Works)
7. Link encrypted, HID reports flow

### Debug Traces

With the fix, you should see:
```
SMP_HID_SEC_REQ_PENDING  (or SMP_SEC_REQ_TX)
SMP_PAIRING_REQUEST_TX    (from phone)
SMP_PAIRING_RESPONSE_RX
...
```

Without the fix, you'll see the connection event followed by silence and then a disconnect.

---

## Related Files

| File | Role |
|------|------|
| `Bluefruit52Lib/src/bluefruit.cpp` | Bluefruit compatibility layer (fix location) |
| `Nrf54L15-Clean-Implementation/src/nrf54l15_hal_parts/nrf54l15_hal_ble_scanning_connections.inc` | HAL central connection path (has auto-security) |
| `Nrf54L15-Clean-Implementation/src/nrf54l15_hal_parts/nrf54l15_hal_ble_peripheral_event_tail.inc` | HAL peripheral event path (sends SMP security request) |
| `Nrf54L15-Clean-Implementation/src/nrf54l15_hal_parts/nrf54l15_hal_ble_ll_security.inc` | SMP implementation |
| `Nrf54L15-Clean-Implementation/src/nrf54l15_hal_parts/nrf54l15_hal_ble_custom_gatt.inc` | Custom GATT permission check |
