# BLE HID / LE Secure Connections Handoff - 2026-06-26

> Historical debugging record for 0.9.208-0.9.211. The described failure was
> superseded by the 0.9.220-0.9.221 interoperability fixes and the 1.0.0-rc1
> two-board security/privacy gate. See `docs/BLE_COMPLIANCE_RESUME.md` and
> `docs/TWO_BOARD_RELEASE_GATE.md` for current status.

## Current Status

BLE HID pairing is still not complete.

The current local code can get the phone to connect at the link layer and it can complete enough SMP work that Android sends repeated `LL_ENC_REQ`, but the link never reaches encrypted state. Do not call this fixed until the serial trace shows:

```text
APP_STATE connected=1 handle=0 secured=1
```

Seeing `bonded=1` is not enough. The current failing trace often toggles `bonded=1`, then times out with:

```text
APP_DISCONNECT handle=0 reason=0x08 name=connection timeout
```

## Repository State

Worktree used for this handoff:

```text
/home/lolren/Desktop/test_pi_nrf54/NRF54L15-Clean-Arduino-core
```

Installed core used for hardware test:

```text
/home/lolren/.arduino15/packages/nrf54l15clean/hardware/nrf54l15clean/0.9.208
```

Board used for HID test:

```text
Seeed XIAO nRF54L15
Probe UID: 761FDE87
Serial/upload port during test: /dev/ttyACM1
```

Diagnostic sketch:

```text
/home/lolren/Desktop/test_pi_nrf54/diagnostics/blehid_mouse_pair_trace/blehid_mouse_pair_trace.ino
```

Android BLE bridge that can be used if needed:

```text
http://192.168.1.209:8787
```

## Important Finding

The stale untracked report `SC_CONNECTION_ANALYSIS_REPORT.md` says the root cause is "no SMP security request". That is no longer the best explanation. Current traces show:

- Android does connect at the LL level.
- SMP/pairing advances far enough that `LL_ENC_REQ` is received.
- `LL_ENC_RSP` is transmitted by the peripheral.
- Android does not advance to `LL_START_ENC_REQ`.
- The connection eventually times out.

So the active failure is in the LL encryption-start transition, not in the initial SMP security request trigger.

## Current Code Changes

The current commit contains debug/experimental LE SC changes in these files:

```text
hardware/nrf54l15clean/nrf54l15clean/libraries/Bluefruit52Lib/src/bluefruit.cpp
hardware/nrf54l15clean/nrf54l15clean/libraries/Bluefruit52Lib/src/bluefruit.h
hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal.h
hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal_parts/nrf54l15_hal_ble_peripheral_event_rx.inc
hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal_parts/nrf54l15_hal_ble_peripheral_event_tx.inc
hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal_parts/nrf54l15_hal_internal_ble_timing.inc
```

Changes made:

- Added `AdafruitBluefruit::debugPrintEncryptionCounters(Stream& out)`.
- Exposed `BleRadio::prefetchConnectionSecurityMaterial(uint32_t spinLimit)`.
- Bluefruit peripheral idle path now prefetches SKDs/IVs before `LL_ENC_REQ`.
- Added detailed encryption debug counters for `LL_ENC_REQ`, `LL_ENC_RSP`, fast path rejects, timing lag, and key material.
- Added a fast `LL_ENC_REQ -> LL_ENC_RSP` path so the first encryption response can be sent inside the timing-sensitive connection event.
- Added a fast duplicate/repeated `LL_ENC_REQ` retransmit path while waiting for `LL_START_ENC_REQ`.
- Added a separate `kBleConnEncRspTxenAfterRxUs = 110U`.
- Removed the experimental MD bit from the fast `LL_ENC_RSP` header because it did not help and may confuse peer sequencing.

## Last Confirmed Test Result

After the latest duplicate-ENCREQ fast retransmit patch, the board was flashed successfully:

```text
Sketch uses 146928 bytes (9%) of program storage space.
Global variables use 45308 bytes (29%) of dynamic memory.
Upload complete
```

Android was asked to connect/pair. Serial trace still did not show `secured=1`.

Important final counter dump:

```text
ENCDBG encReq=121 encRspTxOk=121 fastSeen=121 fastTxOk=121 fastNoEnd=0 fastBusy=0 fastAck=0 fastBuild=0 fastHdr=0x0F fastLen=23 fastNesn=1 fastSn=1 fastPeerAck=0 fastNew=0 fastSmpAck=0 startReq=0 startReqDec=0 followSeen=0 followTimeout=151 txTimeout=1 encRspLag=0 encRspLagMax=0 followBudget=1842 encRspHdr=0x0B encRspPlainLen=13 encRspAirLen=13 encRspFresh=0 txHdr=0x05 txPlainLen=0 txAirLen=0 prefetchUse=6 prefetchFill=12 hwRnd=18 fbRnd=0 skdm=BC8E320D83AB2E85 ivm=C0CDD811 skds=9197E3C605C1C0F4 ivs=C72A8499 stk=5371B7E3EE4D7662789F8F16DB3B28B2
```

Interpretation:

- `fastBusy=0`: the latest duplicate ENCREQ fast retransmit path is working mechanically.
- `fastTxOk=121`: every observed ENCREQ got a fast ENCRSP/retx attempt.
- `startReq=0`: Android never sent `LL_START_ENC_REQ`.
- `encRspLag=0`: software thinks it is not late relative to the chosen target.
- `followTimeout=151`: after ENCRSP, the peripheral waits for a follow-up packet but receives nothing.
- Key material is nonzero, so this is not simply missing RNG/STK material.

## Strongest Current Hypothesis

`LL_ENC_RSP` is being transmitted but Android is not accepting it. The likely causes are:

1. `LL_ENC_RSP` is not actually landing at the correct BLE T_IFS on-air timing.
2. The sequence bits in the fast/retransmit path are still wrong for the central's view.
3. The fast response is using correct-looking local state but the RADIO event timestamp is too imprecise.

The key code smell: `waitRadioEndBudgeted()` records `rxEndTimestampUs` using `bleTimingUs()` when firmware notices `EVENTS_END`/`EVENTS_PHYEND`, not when the radio hardware actually ended the packet. That can be late by polling latency:

```cpp
if (radio->EVENTS_PHYEND != 0U || radio->EVENTS_END != 0U) {
  if (endTimestampUs != nullptr) {
    *endTimestampUs = bleTimingUs();
  }
  return true;
}
```

So even when `encRspLag=0`, the actual air timing can still be late because the base timestamp is late.

## How To Install Local Repo Changes Into The Installed Core

Use exact file copies. Do not rsync the whole library tree because OpenThread/Matter symlinks and generated folders can conflict.

```bash
cd /home/lolren/Desktop/test_pi_nrf54/NRF54L15-Clean-Arduino-core

install -m 0644 hardware/nrf54l15clean/nrf54l15clean/libraries/Bluefruit52Lib/src/bluefruit.cpp \
  /home/lolren/.arduino15/packages/nrf54l15clean/hardware/nrf54l15clean/0.9.208/libraries/Bluefruit52Lib/src/bluefruit.cpp

install -m 0644 hardware/nrf54l15clean/nrf54l15clean/libraries/Bluefruit52Lib/src/bluefruit.h \
  /home/lolren/.arduino15/packages/nrf54l15clean/hardware/nrf54l15clean/0.9.208/libraries/Bluefruit52Lib/src/bluefruit.h

install -m 0644 hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal.h \
  /home/lolren/.arduino15/packages/nrf54l15clean/hardware/nrf54l15clean/0.9.208/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal.h

install -m 0644 hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal_parts/nrf54l15_hal_ble_peripheral_event_rx.inc \
  /home/lolren/.arduino15/packages/nrf54l15clean/hardware/nrf54l15clean/0.9.208/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal_parts/nrf54l15_hal_ble_peripheral_event_rx.inc

install -m 0644 hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal_parts/nrf54l15_hal_ble_peripheral_event_tx.inc \
  /home/lolren/.arduino15/packages/nrf54l15clean/hardware/nrf54l15clean/0.9.208/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal_parts/nrf54l15_hal_ble_peripheral_event_tx.inc

install -m 0644 hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal_parts/nrf54l15_hal_internal_ble_timing.inc \
  /home/lolren/.arduino15/packages/nrf54l15clean/hardware/nrf54l15clean/0.9.208/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal_parts/nrf54l15_hal_internal_ble_timing.inc
```

If the next machine installs a newer package version, adjust `0.9.208` in those paths.

## How To Compile And Flash The Diagnostic Sketch

```bash
arduino-cli compile --clean \
  --fqbn nrf54l15clean:nrf54l15clean:xiao_nrf54l15 \
  --upload \
  --port /dev/ttyACM1 \
  /home/lolren/Desktop/test_pi_nrf54/diagnostics/blehid_mouse_pair_trace
```

If the board moved, find it:

```bash
arduino-cli board list
```

or:

```bash
nrf_ocd list
```

## How To Monitor And Test

Open monitor:

```bash
arduino-cli monitor -p /dev/ttyACM1 -c baudrate=115200
```

Then on Android:

1. Forget/remove the old paired HID device if Android has a stale bond.
2. Open Bluetooth settings.
3. Tap the HID mouse device.
4. Accept pairing if Android shows a prompt.
5. Keep the monitor open until either `secured=1` or a timeout.

The success condition is:

```text
APP_STATE connected=1 handle=0 secured=1
```

If it stalls, type this into the serial monitor:

```text
E
```

The sketch prints an `ENCDBG ...` line with the LL encryption counters.

## Current Debug Counter Meanings

Useful fields in `ENCDBG`:

- `encReq`: main path count of `LL_ENC_REQ`.
- `encRspTxOk`: count of `LL_ENC_RSP` observed as transmitted.
- `fastSeen`: fast path saw `LL_ENC_REQ`.
- `fastTxOk`: fast path sent/retransmitted `LL_ENC_RSP`.
- `fastBusy`: fast path rejected because encryption state was already busy.
- `fastAck`: fast path rejected because SN/NESN did not allow a new response.
- `startReq`: `LL_START_ENC_REQ` observed in normal path.
- `startReqDec`: encrypted/decrypted `LL_START_ENC_REQ` observed.
- `followSeen`: same-event follow-up RX ended after ENCRSP.
- `followTimeout`: follow-up RX timed out after ENCRSP.
- `txTimeout`: radio TX timeout count.
- `encRspLag`: firmware scheduling lag relative to intended ENCRSP TX target.
- `encRspHdr`: last ENCRSP data-channel header byte.
- `encRspFresh`: 1 for fresh ENCRSP, 0 for retransmitted ENCRSP.
- `skdm/ivm/skds/ivs/stk`: key material snapshots; useful to prove material is not all zero.

## Recommended Next Debug Slice

Do not spend more time on SMP unless traces stop before `LL_ENC_REQ`. Current failure is after SMP.

Next slice should focus on proving whether Android receives and accepts ENCRSP:

1. Add first-fast-ENCRSP counters separate from retransmit counters:
   - first TX header
   - first TX timestamp target
   - first TX arm time
   - first TX lag
   - first TX SN/NESN
2. Add a memory trace ring for received LL control opcodes while `connectionEncStartReqPending_` is true:
   - event counter
   - channel
   - hdr
   - opcode
   - new/duplicate bit
   - peer ACK bit
3. Sweep `kBleConnEncRspTxenAfterRxUs` around likely values:
   - 70
   - 80
   - 90
   - 100
   - 110
   - 120
4. Record whether any setting ever produces `startReq > 0`.
5. If no setting produces `startReq`, implement a hardware-captured RX end timestamp instead of software `bleTimingUs()` polling.

## Why Hardware Timestamp Is Likely Needed

Current TX scheduling is:

```cpp
rxEndTimestampUs = bleTimingUs(); // when firmware noticed END
txTarget = rxEndTimestampUs + kBleConnEncRspTxenAfterRxUs;
bleTriggerTxAtTargetUsCritical(radio_, txTarget);
```

This can be internally consistent while still being late on air. The correct fix is to anchor ENCRSP TX to an actual radio event timestamp or to use a pure RADIO shortcut path where possible.

Possible approaches:

- Use GRTC capture/PPI/DPPI if the nRF54L15 exposes publish/subscribe for RADIO END/PHYEND into GRTC capture or a timer.
- If hardware capture is unavailable, estimate true packet end from `ADDRESS` timestamp plus exact PDU airtime.
- Revisit `prearmFastEncRspTifs`, but only if PACKETPTR can be safely replaced before TIFS. Earlier attempts with that path caused TX timeouts, so it needs careful handling.

## Known Test Outcomes From This Session

Baseline:

```text
APP_CONNECT handle=0
APP_CONNECT interval=36 latency=0 timeout=500 mtu=23 dle=27
APP_STATE connected=1 handle=0 secured=0 bonded=1
APP_DISCONNECT handle=0 reason=0x08 name=connection timeout
```

Normal-path-only test:

```text
encReq=0
```

Interpretation: the normal path misses timing-critical ENCREQ unless a fast hook is present.

Fast first ENCRSP without duplicate fast retransmit:

```text
fastSeen > 0
fastTxOk small
fastBusy very high
startReq=0
```

Interpretation: first ENCRSP was attempted, but repeated ENCREQ packets were falling through busy state.

Latest fast ENCRSP with duplicate retransmit:

```text
fastSeen=121
fastTxOk=121
fastBusy=0
startReq=0
```

Interpretation: retransmission behavior is mechanically better, but Android still does not accept ENCRSP enough to move to START_ENC.

## Do Not Touch Unless Necessary

- Do not revert or rewrite HardwareSerial/USB Serial. Serial was recently stabilized and should not be touched for this HID pairing issue.
- Do not broadly refactor Bluefruit while debugging this. Keep changes constrained to LL encryption timing and counters.
- Do not claim success from `bonded=1`; only `secured=1` matters.

## Likely Files For Next Work

```text
hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal_parts/nrf54l15_hal_ble_peripheral_event_rx.inc
hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal_parts/nrf54l15_hal_ble_peripheral_event_tx.inc
hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal_parts/nrf54l15_hal_internal_ble_timing.inc
hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/nrf54l15_hal_parts/nrf54l15_hal_internal_crypto_service.inc
hardware/nrf54l15clean/nrf54l15clean/libraries/Bluefruit52Lib/src/bluefruit.cpp
```

## Minimum Acceptance Test For The Next Session

The next session should not finish until it has at least one monitor trace showing:

```text
APP_CONNECT handle=0
APP_STATE connected=1 handle=0 secured=1
```

Then verify HID behavior by moving/clicking if the diagnostic sketch sends mouse reports.
