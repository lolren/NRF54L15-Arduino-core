# BLE Secure Connections Handover - 2026-06-26

> Historical debugging record for 0.9.208-0.9.211. The described failure was
> superseded by the 0.9.220-0.9.221 interoperability fixes and the stable 1.0.0
> two-board security/privacy gate. See `BLE_COMPLIANCE_RESUME.md` and
> `TWO_BOARD_RELEASE_GATE.md` for current status.

This is the current handover for the BLE HID / LE Secure Connections pairing bug.

Current repo state when this was written:

```text
repo: /home/lolren/Desktop/test_pi_nrf54/NRF54L15-Clean-Arduino-core
branch: main
latest pushed commit: 06cc6d64 Improve BLE secure connection interop
board-manager test version installed locally: 0.9.211
installed test core: /home/lolren/.arduino15/packages/nrf54l15clean/hardware/nrf54l15clean/0.9.211
known connected board used for HID tests: /dev/ttyACM0, XIAO nRF54L15, CMSIS-DAP UID E91217E8
```

## 2026-06-27 Update: Pairing Regression Status

This bug is not solved.

Important history from live phone testing:

```text
Best observed state:
- Pixel connected.
- Fairphone Snapdragon connected.
- Sony Snapdragon still failed.

Problem:
- That exact best state was not saved in main.
- Later live-debug attempts regressed the behavior.
- Current testing is effectively back to square one: no phone should be assumed working until retested.
```

From this point forward, every meaningful BLE HID/LE SC debugging step should be committed before moving on, even if it is only a checkpoint. Do not rely on terminal scrollback or memory for phone-tested states.

Current local checkpoint content:

```text
- Encrypted TX counter handling was changed to treat promoted pending payloads as fresh plaintext for counter consumption.
- Bonded HID reconnects now avoid automatically sending another Security Request / requestPairing when a bond is already primed.
- Primed-bond replacement no longer clears the old bond before replacement pairing succeeds.
- Experimental responder identity-key distribution was added for LE Secure Connections bonding.
```

The encrypted TX counter change is still the strongest keep candidate. The code already treated a promoted queued L2CAP/ATT/SMP response as a fresh plaintext packet:

```cpp
const bool txPayloadIsNewPlain =
    txCanUseFreshPayload || promotePendingTxAfterEmptyAck ||
    (terminateInd && terminateMicFailure);
```

But the encrypted packet counter was only incremented when `txCanUseFreshPayload` was true. That missed the `promotePendingTxAfterEmptyAck` case. The promoted packet was encrypted as a new packet, but the TX counter was not consumed. The next encrypted packet could then reuse the same BLE encryption packet counter, which is exactly the kind of bug that produces Android/Snapdragon error `0x3d` / MIC failure.

Patch applied locally:

```diff
- if (txCanUseFreshPayload &&
+ if (txPayloadIsNewPlain &&
    txIsStartEncRspPlain &&
    connectionEncPrecomputedStartRspTxValid_ &&
    (connectionEncPrecomputedStartRspTxCounter_ == txCounterToUse)) {

- if (txCanUseFreshPayload) {
+ if (txPayloadIsNewPlain) {
    connectionEncTxCounter_ =
        (txCounterToUse + 1ULL) & kBleEncPacketCounterMask;

- } else if (txCanUseFreshPayload) {
+ } else if (txPayloadIsNewPlain) {
    connectionLastTxWasEncrypted_ = false;

- encDebug_.encLastTxWasFresh = txCanUseFreshPayload ? 1U : 0U;
+ encDebug_.encLastTxWasFresh = txPayloadIsNewPlain ? 1U : 0U;
```

Local validation command used for HID mouse debug builds:

```bash
rsync -a hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/ \
  /home/lolren/.arduino15/packages/nrf54l15clean/hardware/nrf54l15clean/0.9.211/libraries/Nrf54L15-Clean-Implementation/src/

arduino-cli compile \
  --fqbn nrf54l15clean:nrf54l15clean:xiao_nrf54l15:clean_ble_trace=on \
  /tmp/blehid_mouse_jw

arduino-cli upload \
  -p /dev/ttyACM0 \
  --fqbn nrf54l15clean:nrf54l15clean:xiao_nrf54l15:clean_ble_trace=on \
  /tmp/blehid_mouse_jw
```

Compile result:

```text
Sketch uses 158628 bytes (10%) of program storage space.
Global variables use 45492 bytes (29%) of dynamic memory.
```

Upload result:

```text
nrf_ocd 0.3.2
Probe UID E91217E8
Upload complete
```

Trace files used during the successful/partial tests included:

```text
/tmp/hid_pair_trace_counterfix.txt
/tmp/hid_pair_trace.txt
```

Observed result after the counter fix, before later regressions:

```text
User reported one successful Snapdragon connection.
Later testing narrowed this to: Pixel and Fairphone Snapdragon connected, Sony Snapdragon still failed.
That exact state was not preserved as a separate commit.
```

Representative post-fix trace:

```text
[BLE] LL_ENC_REQ_ACCEPTED_FAST
[BLE] LL_START_ENC_REQ_PENDING
[BLE] LL_ENC_RSP_FAST_TX_OK
[BLE] LL_START_ENC_REQ_QUEUED_EARLY
[BLE] LL_START_ENC_REQ_TX
[BLE] LL_START_ENC_RSP_FAST
[BLE] LL_START_ENC_RSP_RX
Connection encrypted
[BLE] LL_START_ENC_RSP_TX_ENC
[BLE] BOND_DEFERRED
[BLE] SMP_ID_INFO_RX
[BLE] SMP_ID_ADDR_RX
[BLE] ATT_RX_UUID_GATT_SERVER_FEATURES
[BLE] ATT_RX_UUID_GATT_DB_HASH
[BLE] ATT_ERROR_RSP_TX
[BLE] LL_PHY_REQ_RX
[BLE] LL_PHY_RSP_TX
[BLE] LL_CHANNEL_MAP_APPLIED
```

One later trace showed:

```text
[BLE] DISCONNECT
Disconnected, reason=0x13
```

That appeared after a period of successful encrypted traffic and after reconnect testing. Treat it as likely remote/user disconnect unless reproduced as an unwanted automatic disconnect.

Current local status after later attempts:

```text
Do not assume any phone connects.
Retest Pixel, Fairphone Snapdragon, and Sony Snapdragon from clean bonds after each checkpoint commit.
```

Recommended immediate next step after this checkpoint:

```text
1. Commit this checkpoint so it is not lost.
2. Rebuild/flash blehid_mouse with BLE trace enabled.
3. Test Pixel from a forgotten bond.
4. Test Fairphone Snapdragon from a forgotten bond.
5. Test Sony Snapdragon from a forgotten bond.
6. If a phone connects, commit immediately before trying another speculative patch.
```

## Current Status

Pixel phone behavior:

```text
Previously connected in the best observed intermediate state.
Current behavior must be retested.
```

Snapdragon Android behavior:

```text
Fairphone Snapdragon connected in the best observed intermediate state.
Sony Snapdragon still failed in that same state.
Current behavior must be retested.
```

Important: do not assume this is the same root cause as the first HID failure. The earlier "peripheral never initiates security" bug was fixed. The remaining Snapdragon problem happens much later, around LE encryption start, bonding, or encrypted ATT/GATT traffic.

## What Was Changed In The Latest Pushed Commit

Latest pushed commit:

```bash
git show --stat --oneline 06cc6d64
```

Commit:

```text
06cc6d64 Improve BLE secure connection interop
```

Files changed:

```text
hardware/nrf54l15clean/nrf54l15clean/SC_CONNECTION_ANALYSIS_REPORT.md
hardware/nrf54l15clean/nrf54l15clean/libraries/Bluefruit52Lib/src/bluefruit.cpp
hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal.cpp
hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal.h
hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal_parts/nrf54l15_hal_ble_att_l2cap.inc
hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal_parts/nrf54l15_hal_ble_connection_api.inc
hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal_parts/nrf54l15_hal_ble_core_setup.inc
hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal_parts/nrf54l15_hal_ble_ll_security.inc
hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal_parts/nrf54l15_hal_ble_peripheral_event_tail.inc
hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal_parts/nrf54l15_hal_ble_peripheral_event_tx.inc
hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal_parts/nrf54l15_hal_ble_scanning_connections.inc
hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal_parts/nrf54l15_hal_internal_ble_timing.inc
hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal_parts/nrf54l15_hal_internal_gatt_bond.inc
```

Summary of the latest work:

```text
- Restored the known-good LE encryption role behavior after a bad role-direction experiment.
- Kept Pixel working.
- Added/kept interop work for Android GATT cache/service discovery.
- Added/kept BLE security-request path needed for HID pairing.
- Added/kept trace points around SMP, ATT, LL encryption, and bond persistence.
- Added a queued LL_START_ENC_REQ priority guard to avoid starving start-encryption behind repeated LL_ENC_RSP replays.
```

## Important Historical Note

`hardware/nrf54l15clean/nrf54l15clean/SC_CONNECTION_ANALYSIS_REPORT.md` is useful historical context, but it is not the full current diagnosis.

That report says the HID mouse did not pair because the peripheral did not send an SMP Security Request. That was a real bug at the time. It was fixed. The current failure is after security starts:

```text
old fixed bug: no SMP security request, phone never got properly prompted
current bug: Snapdragon gets far enough to save/bond, but link does not remain connected
```

If another AI reads only `SC_CONNECTION_ANALYSIS_REPORT.md`, it will probably chase the wrong problem. Start with this handover instead.

## Test Sketch

The active HID test sketch used locally is:

```text
/tmp/blehid_mouse_jw/blehid_mouse_jw.ino
```

It is based on `Bluefruit52Lib/examples/HID/blehid_mouse/blehid_mouse.ino`.

Important sketch details:

```cpp
Bluefruit.begin();
Bluefruit.Security.setIOCaps(false, false, false);
Bluefruit.Security.setPairPasskeyCallback(pairing_passkey_callback);
Bluefruit.Security.setPairCompleteCallback(pairing_complete_callback);
Bluefruit.Security.setSecuredCallback(secured_callback);

#if defined(ARDUINO_NRF54LM20A) || defined(ARDUINO_NRF54LM20B)
  Bluefruit.Periph.setConnInterval(24, 40); // 30-50 ms
#else
  Bluefruit.Periph.setConnInterval(9, 16);  // 11.25-20 ms
#endif

Bluefruit.Periph.setConnectCallback(connect_callback);
Bluefruit.Periph.setDisconnectCallback(disconnect_callback);

void connect_callback(uint16_t conn_handle)
{
  (void) conn_handle;
  Serial.println("Connected, requesting Just Works pairing");
  Bluefruit.Security.requestPairing();
}
```

The important behavior is that the sketch requests Just Works pairing on connection. This mirrors what HID examples need because HID characteristics require encryption.

## Build And Flash Commands

Compile with BLE trace enabled:

```bash
cd /home/lolren/Desktop/test_pi_nrf54/NRF54L15-Clean-Arduino-core
arduino-cli compile \
  --fqbn nrf54l15clean:nrf54l15clean:xiao_nrf54l15:clean_ble_trace=on \
  /tmp/blehid_mouse_jw
```

Upload to the connected XIAO nRF54L15:

```bash
arduino-cli upload \
  -p /dev/ttyACM0 \
  --fqbn nrf54l15clean:nrf54l15clean:xiao_nrf54l15:clean_ble_trace=on \
  /tmp/blehid_mouse_jw
```

The last successful local compile showed:

```text
Sketch uses 158628 bytes (10%) of program storage space.
Global variables use 45492 bytes (29%) of dynamic memory.
```

The last successful upload used `nrf_ocd 0.3.2` and loaded about 156 KB to board UID `E91217E8`.

## Syncing Local Source Into Installed Core

If editing source in the repo and testing through the already-installed Arduino package, sync these folders:

```bash
rsync -a \
  hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/ \
  /home/lolren/.arduino15/packages/nrf54l15clean/hardware/nrf54l15clean/0.9.211/libraries/Nrf54L15-Clean-Implementation/src/

rsync -a \
  hardware/nrf54l15clean/nrf54l15clean/libraries/Bluefruit52Lib/src/ \
  /home/lolren/.arduino15/packages/nrf54l15clean/hardware/nrf54l15clean/0.9.211/libraries/Bluefruit52Lib/src/
```

Only do this when intentionally testing local source against installed Board Manager version `0.9.211`.

## Serial Trace Capture

Use this for board-side trace while testing phone pairing:

```bash
python3 - <<'PY'
import serial, time

patterns = (
    'CONNECTED', 'DISCONNECT', 'reason',
    'SMP_', 'LL_', 'ENC_', 'BOND_',
    'ATT_RX_UUID_GATT', 'ATT_RX_HANDLE_GATT', 'ATT_WRITE',
    'Pairing', 'Secured', 'Connected',
    'MIC', 'SUPERVISION_TIMEOUT', 'ERROR', 'FAIL'
)

ser = serial.Serial('/dev/ttyACM0', 115200, timeout=0.2)
print('BOARD_MONITOR_READY_TRY_PAIRING_NOW', flush=True)
start = time.time()

with open('/tmp/hid_pair_trace_latest.txt', 'w', buffering=1) as f:
    while time.time() - start < 180:
        data = ser.readline()
        if not data:
            continue
        line = data.decode('utf-8', 'replace').rstrip('\r\n')
        f.write(line + '\n')
        if any(p in line for p in patterns):
            print(line, flush=True)

ser.close()
print('BOARD_MONITOR_DONE /tmp/hid_pair_trace_latest.txt', flush=True)
PY
```

Recommended test order:

```text
1. Forget/remove old XIAO NRF54L15 bond from Android Bluetooth settings.
2. Reset/reflash board.
3. Start serial trace.
4. Pair/connect from Pixel first.
5. Save trace.
6. Forget/remove bond again.
7. Reset/reflash board.
8. Start serial trace.
9. Pair/connect from Snapdragon.
10. Save trace.
```

Do not mix Pixel and Snapdragon attempts in one trace if possible. The bond database and Android-side bond state can pollute the next attempt.

## Android BLE Debug Bridge

Snapdragon Android BLE debug bridge:

```text
http://192.168.1.209:8787
```

Useful endpoints:

```bash
curl -s http://192.168.1.209:8787/status
curl -s http://192.168.1.209:8787/scan/results
curl -s 'http://192.168.1.209:8787/logs?limit=300'
curl -s http://192.168.1.209:8787/gatt/status

curl -X POST 'http://192.168.1.209:8787/scan/start?profile=default'
curl -X POST 'http://192.168.1.209:8787/scan/start?profile=legacy_1m'
curl -X POST 'http://192.168.1.209:8787/scan/start?profile=legacy_api'
curl -X POST http://192.168.1.209:8787/scan/stop

curl -X POST 'http://192.168.1.209:8787/gatt/connect?address=AA:BB:CC:DD:EE:FF'
curl -X POST http://192.168.1.209:8787/gatt/discover
curl -X POST http://192.168.1.209:8787/gatt/disconnect
```

Limitations:

```text
- The bridge is useful for scan/connect/discover/logs.
- The bridge does not fully replace OS Bluetooth Settings HID pairing.
- For HID mouse, final pairing must be tested from Android Bluetooth Settings or equivalent HID-capable system UI.
```

Earlier bridge result:

```text
Plain GATT connect/discover from Snapdragon bridge succeeded and found the expected services:
GAP, GATT, Battery, Device Information, HID.
```

That means the basic advertiser, connection, and service discovery path is not the main remaining issue.

## Phone-Side Errors Observed

Snapdragon screenshots showed:

```text
Connection parameters update failed with status 61
Error 61 (0x3d): UNKNOWN (61)
Disconnected
Bond information lost, reason: REMOVED (9)
Bonding failed, reason: REMOVED (9)
Error 22 (0x16): GATT CONN TERMINATE LOCAL HOST
```

Interpretation:

```text
0x3d is BLE_HCI_CONN_TERMINATED_DUE_TO_MIC_FAILURE in BLE terminology.
That means the receiver rejected an encrypted packet because the MIC did not validate.
Common causes are wrong session key, wrong IV/SKD ordering, wrong packet counter, wrong direction bit, encrypted/plain transition off by one event, stale bond key, or retransmitting an encrypted packet with the wrong counter/header.
```

`0x16 GATT CONN TERMINATE LOCAL HOST` is Android deciding to close the connection locally. In this case it is probably a consequence of the link/security failure, not the first cause.

## Useful Trace Files From This Session

Temporary traces:

```text
/tmp/sd_gatt_trace.txt
/tmp/sd_hid_pair_trace.txt
/tmp/sd_hid_pair_trace_after_rolefix.txt
/tmp/hid_pair_trace_latest.txt
```

`/tmp/hid_pair_trace_latest.txt` is empty because the trace run was interrupted right after the last flash.

The most useful trace is:

```text
/tmp/sd_hid_pair_trace.txt
```

It shows the current useful failure pattern before the bad role experiment:

```text
[BLE] SMP_HID_SEC_REQ_PENDING
[BLE] CONNECTED
[BLE] SMP_SEC_REQ_TX
Connected, requesting Just Works pairing
[BLE] SMP_SC_REQUEST_RX
[BLE] SMP_MITM_REQUEST_JUST_WORKS_FALLBACK
[BLE] SMP_PAIRING_REQUEST_RX
[BLE] SMP_PAIRING_RESPONSE_TX
[BLE] SMP_SC_PUBKEY_RX
[BLE] SMP_SC_PUBKEY_TX
[BLE] SMP_SC_CONFIRM_TX
[BLE] SMP_SC_RANDOM_RX
[BLE] SMP_SC_RANDOM_TX
[BLE] SMP_SC_DHKEY_CHECK_RX
[BLE] SMP_SC_DHKEY_CHECK_TX
[BLE] LL_ENC_REQ_ACCEPTED_FAST
[BLE] LL_START_ENC_REQ_PENDING
[BLE] LL_ENC_RSP_FAST_TX_OK
[BLE] LL_ENC_RSP_FAST_FOLLOW_TIMEOUT
[BLE] LL_START_ENC_REQ_QUEUED_EARLY
[BLE] PERIPH_TX_PENDING_LL
[BLE] LL_START_ENC_REQ_TX
[BLE] LL_START_ENC_REQ_TX
[BLE] LL_START_ENC_RSP_FAST
[BLE] LL_START_ENC_RSP_RX
Connection encrypted
[BLE] PERIPH_TX_PENDING_LL
[BLE] LL_START_ENC_RSP_TX_ENC
[BLE] LL_START_ENC_RSP_TX_ENC
[BLE] LL_START_ENC_RSP_TX_ENC
[BLE] LL_START_ENC_RSP_TX_ENC
[BLE] BOND_DEFERRED
[BLE] SMP_ID_INFO_RX
[BLE] BOND_DEFERRED
[BLE] SMP_ID_ADDR_RX
[BLE] ATT_RX_READ_TYPE_REQ
[BLE] ATT_RX_UUID_GATT_SERVER_FEATURES
[BLE] ATT_READ_BY_TYPE_RSP_TX
[BLE] LL_PHY_REQ_RX
[BLE] LL_PHY_RSP_TX
```

Important things in this trace:

```text
- SMP reaches DHKey Check, so P-256 and f4/f5/f6/g2-style SC work got far enough for both peers.
- LL_ENC_REQ is accepted and LL_ENC_RSP is sent.
- The local peripheral queues and sends LL_START_ENC_REQ.
- The link reports "Connection encrypted".
- There are repeated encrypted LL_START_ENC_RSP transmissions.
- Bond material arrives and gets deferred for persistence.
- Android then reads GATT server features and performs PHY negotiation.
- Long RX timeout sequences follow in the full trace.
```

That suggests the remaining failure is not "pairing never starts". It is likely in the encryption transition, retransmission handling, bond persistence timing, or encrypted GATT response path after the link claims to be encrypted.

## Bad Experiment To Avoid Repeating

A role-direction patch was tried and broke both Pixel and Snapdragon.

Bad experiment:

```text
Change peripheral after LL_ENC_RSP to wait for central LL_START_ENC_REQ:
connectionEncStartReqPending_ = true;
connectionEncStartReqTxPending_ = false;

Change queued LL_START_ENC_REQ to central-only.

Change response to incoming LL_START_ENC_RSP so only central sends the extra START_ENC_RSP.
```

Why it was reverted:

```text
Zephyr controller tests show the peripheral can send LL_START_ENC_REQ after LTK reply in this flow.
After the bad role change, both Pixel and Snapdragon timed out during pairing.
The current source has that role change reverted.
```

Current expected source state:

```text
In nrf54l15_hal_ble_peripheral_event_rx.inc:
after key derivation in peripheral role:
connectionEncStartReqPending_ = false;
connectionEncStartReqTxPending_ = true;

In nrf54l15_hal_ble_peripheral_event_tx.inc:
queued LL_START_ENC_REQ is not central-only.

In nrf54l15_hal_ble_ll_security.inc:
the kBleLlCtrlStartEncRsp response path is back to the previous peripheral behavior.
```

The current behavior after reverting:

```text
Pixel connects again.
Snapdragon saves/bonds but does not complete usable connection.
```

## Things That Were Fixed Or Improved Already

### HID Security Request

The HID peripheral now requests security instead of relying on Android to infer it.

Expected trace:

```text
[BLE] SMP_HID_SEC_REQ_PENDING
[BLE] CONNECTED
[BLE] SMP_SEC_REQ_TX
```

Relevant paths:

```text
hardware/nrf54l15clean/nrf54l15clean/libraries/Bluefruit52Lib/src/bluefruit.cpp
hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal_parts/nrf54l15_hal_ble_scanning_connections.inc
hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal_parts/nrf54l15_hal_ble_custom_gatt.inc
```

### Android GATT Cache / Modern GATT Service Support

Android/Snapdragon reads modern GATT attributes that were previously missing or incomplete.

Implemented/handled:

```text
GATT Client Supported Features: 0x2B29
GATT Database Hash: 0x2B2A
GATT Server Supported Features: 0x2B3A
```

Relevant paths:

```text
hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal_parts/nrf54l15_hal_internal_gatt_bond.inc
hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal_parts/nrf54l15_hal_ble_att_l2cap.inc
```

Expected trace examples:

```text
[BLE] ATT_RX_UUID_GATT_DB_HASH
[BLE] ATT_RX_UUID_GATT_SERVER_FEATURES
[BLE] ATT_READ_BY_TYPE_RSP_TX
```

### Deferred Bond Flash Writes

Flash writes during an active BLE connection are risky because they can steal enough time to miss BLE events. Bond persistence was adjusted so retention is updated immediately and flash persistence can be deferred.

Expected trace examples:

```text
[BLE] BOND_DEFERRED
[BLE] BOND_FLASH_FLUSHED
```

Relevant path:

```text
hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal_parts/nrf54l15_hal_ble_ll_security.inc
```

### SC Key Generation Pre-Work

The local Secure Connections P-256 keypair generation can be expensive in software. The code now has guard/state for key generation in progress and can prime work earlier.

Relevant path:

```text
hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal_parts/nrf54l15_hal_ble_ll_security.inc
```

### Trace Coverage

Trace coverage was expanded for:

```text
SMP pairing stages
ATT request/response selection
GATT cache attributes
LL encryption control procedure
bond persistence
PHY request/response
```

This is valuable. Keep trace enabled while debugging this bug.

## Current Best Hypotheses

### Hypothesis 1: encrypted LL_START_ENC_RSP retransmit/counter issue

The trace shows repeated:

```text
[BLE] LL_START_ENC_RSP_TX_ENC
```

If the same encrypted packet is retransmitted, it must be retransmitted with exactly the same encrypted payload and same packet counter semantics. If the code re-encrypts the same logical PDU with a new counter or toggled header state before the peer has ACKed it, the MIC can fail.

What to inspect:

```text
nrf54l15_hal_ble_peripheral_event_tx.inc
prepareFollowTxPacket()
transmitImmediateFollowupResponse()
connectionEncTxCounter_
connectionLastTxWasEncrypted_
connectionLastTxEncryptedPayload_
connectionLastTxEncryptedLength_
connectionFreshTxAllowed_
connectionTxSn_
connectionExpectedRxSn_
```

Testing idea:

```text
Add trace counters for every encrypted TX:
- opcode before encryption
- tx counter used
- LL header byte used as CCM AAD
- whether payload was fresh encrypted or retransmitted from connectionLastTxEncryptedPayload_
- whether NESN/SN says peer ACKed previous TX
```

Expected safe behavior:

```text
For an unacked encrypted control PDU, retransmit cached ciphertext without incrementing connectionEncTxCounter_.
Only increment connectionEncTxCounter_ after a fresh encrypted PDU is actually accepted as sent and logically acknowledged according to LL SN/NESN handling.
```

### Hypothesis 2: wrong role/direction or SKD/IV ordering in one encrypted path

The fast ENC_RSP path uses:

```text
peripheral RX direction = 1
peripheral TX direction = 0
SKDm from master, SKDs from slave
IVm from master, IVs from slave
```

This is probably correct because Pixel works. But Snapdragon may be less forgiving if one later path uses the alternate key/session ordering or wrong direction when encrypted GATT traffic starts.

What to inspect:

```text
bleCcmEncryptPayload()
bleCcmDecryptPayload()
connectionEncSessionKey_
connectionEncSessionKeyAlt_
connectionEncAltKeyValid_
connectionEncRxDirection_
connectionEncTxDirection_
connectionEncPrecomputedStartRspTx_
connectionEncPrecomputedPayload_
```

Testing idea:

```text
Trace whether normal session key or alt key is used for every decrypt and encrypt around the first 10 encrypted packets.
If MIC fails, dump:
- rx counter
- tx counter
- direction bits
- SKD/IV fragments used
- LL header byte
- length
- opcode after successful decrypt, if any
```

Do not print actual LTK/STK/session key in user-facing builds. For local debug only, counters and direction are enough first.

### Hypothesis 3: bond database mismatch / stale Android bond state

Error 0x3D can happen if Android uses an old LTK or if the board and phone disagree about the bond.

Why this may not be the whole issue:

```text
Pixel works after bond cleanup.
Snapdragon saves/bonds but does not complete.
The trace reaches new pairing and bond material, so not every attempt is stale-bond-only.
```

Still do this every test:

```text
Forget device from Android Bluetooth settings.
Clear board bond state if testing code supports it.
Reflash or reset board.
Use a fresh device address if there is a way to force it during local debug.
```

Potential future improvement:

```text
Expose a reliable Bluefruit/Core API or diagnostic sketch path to clear all local bonds.
Add an example command in HID/security examples to clear bonds at boot for testing.
```

### Hypothesis 4: connection parameter update timing on Snapdragon

The Snapdragon screenshot showed:

```text
Connection parameters update failed with status 61
interval 7.5ms, latency 0, timeout 5000ms
```

The HID mouse example requests 11.25-20 ms in the sketch for nRF54L15, but Android logs also showed 7.5 ms in one path. It may be Android attempting 7.5 ms first, then settling to 30 ms, or the core responding incorrectly.

Why this is probably secondary:

```text
The link still gets through service discovery and bond events in some attempts.
Error 61 maps better to encryption MIC failure than pure connection parameter rejection.
```

Still worth testing:

```text
Run the HID sketch with a conservative interval on nRF54L15 too:
Bluefruit.Periph.setConnInterval(24, 40); // 30-50 ms

Then compare Snapdragon pairing behavior.
```

If conservative interval makes Snapdragon reliable, keep HID examples conservative or add phone-specific interop notes.

### Hypothesis 5: encrypted ATT/GATT response after link says encrypted

Trace reaches:

```text
Connection encrypted
[BLE] ATT_RX_READ_TYPE_REQ
[BLE] ATT_RX_UUID_GATT_SERVER_FEATURES
[BLE] ATT_READ_BY_TYPE_RSP_TX
```

If the encrypted ATT response is transmitted with a bad counter/header, Android may disconnect with MIC failure.

What to inspect:

```text
nrf54l15_hal_ble_att_l2cap.inc
nrf54l15_hal_ble_peripheral_event_tx.inc
pending L2CAP response promotion
encrypted TX of ATT_READ_BY_TYPE_RSP
```

Testing idea:

```text
Add trace for first encrypted ATT response:
- ATT opcode
- handle range
- LL header
- tx counter
- fresh vs retransmit
- SN/NESN state
```

## Zephyr / Spec Comparison Pointers

Local Zephyr tree:

```text
/home/lolren/Desktop/test_pi_nrf54/ncs-workspace/zephyr
```

Useful Zephyr controller encryption test:

```text
/home/lolren/Desktop/test_pi_nrf54/ncs-workspace/zephyr/tests/bluetooth/controller/ctrl_encrypt/src/main.c
```

Important observation from Zephyr:

```text
The peripheral controller test sends LL_START_ENC_REQ after LTK/key readiness in this flow.
Therefore the reverted "peripheral should only wait for central START_ENC_REQ" change was wrong for this implementation path.
```

Use Zephyr for:

```text
- LL control procedure ordering
- when TX/RX encryption is enabled
- packet counter increment timing
- retransmit handling for encrypted control PDUs
```

Do not blindly copy high-level host behavior. Compare controller-level LLCP behavior.

## Code Areas To Inspect Next

Start here:

```text
hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal_parts/nrf54l15_hal_ble_peripheral_event_tx.inc
```

Focus around:

```text
- queued LL_START_ENC_REQ generation
- pendingTxIsStartEncryptionControlPdu
- promotePendingStartEncryptionAfterEncRsp
- prepareFollowTxPacket()
- transmitImmediateFollowupResponse()
- encrypted follow-up packet retransmission
- connectionEncTxCounter_ increment
- connectionLastTxEncryptedPayload_ reuse
```

Then inspect:

```text
hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal_parts/nrf54l15_hal_ble_peripheral_event_rx.inc
```

Focus around:

```text
- fast LL_ENC_REQ path
- LL_ENC_RSP transmission
- follow-up RX timeout after ENC_RSP
- LL_START_ENC_RSP_RX
- MIC failure/decrypt fallback
```

Then inspect:

```text
hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal_parts/nrf54l15_hal_ble_ll_security.inc
```

Focus around:

```text
- buildLlControlResponse()
- LL_ENC_REQ handling
- LL_START_ENC_REQ handling
- LL_START_ENC_RSP handling
- secure connection key derivation
- bond persistence
```

Also inspect:

```text
hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal_parts/nrf54l15_hal_ble_att_l2cap.inc
hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal_parts/nrf54l15_hal_internal_gatt_bond.inc
```

Focus around:

```text
- GATT Server Supported Features read
- GATT Database Hash read
- Client Supported Features write
- ATT response encrypted transmission
```

## Things Not To Touch Unless There Is A Direct Reason

Do not touch Serial / HardwareSerial for this bug. It had previous regressions and was restored separately.

Do not spend time on generic advertising unless Snapdragon cannot even see the device. Current issue is after connect/pair.

Do not undo the Pixel-working role restoration unless a trace proves a specific role path is wrong.

Do not assume `SC_CONNECTION_ANALYSIS_REPORT.md` is the final root cause. It was the first root cause, not the remaining one.

## Minimal Next Debug Slice

The next useful slice should be small:

```text
Goal: identify whether the first bad Snapdragon packet is an LL encrypted control PDU or an encrypted ATT/GATT response.
```

Add local-only trace fields:

```text
1. On every encrypted TX for the first 16 encrypted packets:
   - event counter
   - LLID
   - plain opcode if LL control or ATT
   - tx counter used
   - whether fresh encryption or cached retransmit
   - header byte used for CCM
   - connectionTxSn_
   - connectionExpectedRxSn_

2. On every encrypted RX/decrypt for the first 16 encrypted packets:
   - event counter
   - LLID
   - rx counter used
   - decrypt ok/fail
   - header byte used for CCM
   - opcode after decrypt if ok

3. On every TX counter increment:
   - old counter
   - new counter
   - reason
```

Then test:

```text
1. Pixel with clean bond.
2. Snapdragon with clean bond.
3. Compare the first divergence after "Connection encrypted".
```

Expected result:

```text
If Pixel and Snapdragon diverge only after encrypted ATT_READ_BY_TYPE_RSP_TX, inspect ATT encryption/retransmit.
If Snapdragon fails immediately around repeated LL_START_ENC_RSP_TX_ENC, inspect encrypted control retransmit/counter handling.
If both traces are identical until Android disconnects with no board MIC fail, inspect phone-side connection parameter / bond persistence / Android GATT cache behavior.
```

## Candidate Fix Direction

Most likely fix direction:

```text
Make encrypted TX retransmission strictly LL-compliant:
- fresh encrypt only once for a new payload
- cache ciphertext, encrypted length, LL header, counter, and plain metadata
- retransmit cached ciphertext while SN/NESN says peer has not ACKed it
- increment tx counter exactly once per fresh encrypted PDU accepted into the link sequence
- never re-encrypt the same logical LL control or ATT response with a new counter before ACK
```

Why this is likely:

```text
Pixel working means the basic cryptographic functions are probably correct.
Snapdragon failing with 0x3D means a stricter controller/host stack is catching a transition or retransmit mismatch.
The trace already shows repeated encrypted LL_START_ENC_RSP transmissions, which is exactly where counter/cache mistakes show up.
```

## Current User-Visible State To Preserve

Do not regress these:

```text
Pixel connects and pairs.
Snapdragon at least saves/bonds the device.
Basic GATT discover works from the Android bridge.
BLE NUS and non-secure BLE paths should not be touched unless tested.
Serial should not be changed.
```

Before making a release, test at least:

```text
1. HID mouse Just Works pairing on Pixel.
2. HID mouse Just Works pairing on Snapdragon.
3. Basic non-secure peripheral/central example.
4. NUS bridge if available.
5. MTU/data length regression test from issue 68/69 area.
```

## Final Notes

The current problem is close to solved but not complete.

The important correction from this session:

```text
Do not keep the role-detour patch. It broke both phones.
Current pushed state restores Pixel and keeps the work needed for Snapdragon investigation.
```

The next person should not start by rewriting pairing. Pairing reaches encryption. The next person should instrument and fix encrypted packet transition/retransmission around `LL_START_ENC_REQ`, `LL_START_ENC_RSP`, and the first encrypted ATT responses.
