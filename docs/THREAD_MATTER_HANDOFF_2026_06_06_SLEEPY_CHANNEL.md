# Thread/Matter Handoff - 2026-06-06 Sleepy Channel Fix

This handoff records the current Thread/Matter state after reviewing
`THREAD_MATTER_SESSION_REPORT_2026_06_06_FINAL.md`, fixing the reported sleepy
child channel issue, and validating the nearby Thread examples.

## Fixed In This Pass

- Added/finished `Nrf54ThreadExperimental::beginAsSleepyChild()`.
- Added `Nrf54ThreadExperimental::setPollPeriod()` and `getPollPeriod()`.
- Sleepy child attach now starts as receiver-on MTD (`rxOnWhenIdle=true`,
  `deviceType=false`, `networkData=true`) before IP6/Thread enable.
- After the child reaches child role and remains there for the settle interval,
  `maybeSwitchToSleepyMode()` switches to true sleepy MTD
  (`rxOnWhenIdle=false`, `deviceType=false`).
- The original report issue is fixed: the sleepy child no longer remains on
  radio channel 0. Hardware logs show it attaches on channel 15 with PAN ID
  `0x5D6A`.
- Poll period configuration is deferred until the sleepy mode is actually
  active, avoiding `otLinkSetPollPeriod()` calls before there is an OpenThread
  instance or before rx-off mode is active.
- Thread wrapper timing now uses `otPlatTimeGet() / 1000ULL` for staged Thread
  state-machine timing instead of Arduino `millis()`.
- Added a minimal fixed-dataset parent example:
  `examples/Thread/ThreadExperimentalSleepyParent`.
- Updated the sleepy child example:
  `examples/Thread/ThreadExperimentalSleepyChild`.
- Fixed `ThreadExperimentalReconnectStress`: it now uses `restart(false)` for
  detach/reattach instead of `stop()` followed by a second `beginAsRouter()`.
  `stop()` intentionally does not clear `beginCalled_`, so the old example path
  failed to restart.

## Validation Run

Repository:

```text
/home/lolren/Desktop/test_pi_nrf54/NRF54L15-Clean-Arduino-core
```

Boards present during two-board checks:

```text
/dev/ttyACM0
/dev/ttyACM1
```

Compile checks:

```bash
arduino-cli compile --fqbn nrf54l15clean:nrf54l15clean:xiao_nrf54l15:clean_thread=stage \
  hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/examples/Thread/ThreadExperimentalSleepyChild

arduino-cli compile --fqbn nrf54l15clean:nrf54l15clean:xiao_nrf54l15:clean_thread=stage \
  hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/examples/Thread/ThreadExperimentalSleepyParent

arduino-cli compile --fqbn nrf54l15clean:nrf54l15clean:xiao_nrf54l15:clean_thread=stage \
  hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/examples/Thread/ThreadExperimentalReconnectStress
```

Result:

```text
ThreadExperimentalSleepyChild: PASS
ThreadExperimentalSleepyParent: PASS
ThreadExperimentalReconnectStress: PASS
```

MeshCoP compile coverage:

```bash
python3 scripts/thread_meshcop_validation.py compile
```

Result:

```text
compile commissioner: PASS
compile joiner: PASS
compile restore: PASS
compile wrong-pskd: PASS
```

UDP two-board soak:

```bash
python3 scripts/test_thread_udp_soak.py \
  --port1 /dev/ttyACM0 \
  --port2 /dev/ttyACM1 \
  --timeout 35 \
  --require-unicast safe \
  --require-downlink none \
  --require-multicast none
```

Result:

```text
board1=/dev/ttyACM0 role=leader rloc16=0xD400
board2=/dev/ttyACM1 role=child  rloc16=0xD401

Uplink child-to-leader: 8,16,31,63,95,127,191,255,512 PASS
Downlink leader-to-child: 8,16,31,63,95,127,191,255,512 PASS
Multicast: 8,16,31,63,95,127,191,255,512 PASS
Required safe uplink sizes: PASS
```

Sleepy parent/child hardware observation from this pass:

```text
Parent:
  role=leader
  attached=1
  channel=15
  pan=0x5D6A
  rx_on_when_idle=1

Child:
  role=child observed
  attached=1 observed
  channel=15 observed
  pan=0x5D6A observed
  rx_on_when_idle=1 during attach
  rx_on_when_idle=0 after sleepy switch observed
```

The channel bug from the session report is therefore fixed.

## Remaining Sleepy Child Issue

The API and channel attach path are fixed, but true SED operation is not yet
production-stable. In hardware logs, the child attaches, later switches from
mode `0x0d` to `0x05` (`rx-on:no`, `ftd:no`, `full-net:yes`), then can detach.

Likely next target:

```text
hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/openthread_platform_nrf54l15.cpp
```

Spec and code path to inspect:

- `openthread/platform/radio.h` says that after a Data Request ACK with Frame
  Pending set, the radio platform should keep the receiver on until a bounded
  data-poll timeout.
- OpenThread uses `OPENTHREAD_CONFIG_MAC_DATA_POLL_TIMEOUT`, default 100 ms.
- In `third_party/openthread-core/src/core/mac/mac.cpp`, data-poll TX done with
  ACK frame pending calls `StartOperation(kOperationWaitingForData)`, then
  `PerformNextOperation()` starts `mLinks.Receive(mRadioChannel)`.
- In this platform, `otPlatRadioReceive()` can start a non-buffered receive when
  `radioRxOnWhenIdle=false`, so the receive path exists.
- The remaining work is to prove the platform catches the parent response during
  that rx-off data-poll window and does not prematurely cancel or sleep the
  radio.

Do not fix this by keeping rx-on forever or adding sketch delays. That would
hide the bug and destroy SED power behavior.

Suggested next debug instrumentation:

- Add snapshot counters for:
  - data-poll TX frame detected
  - TX ACK frame pending detected
  - `kOperationWaitingForData` receive entered via `otPlatRadioReceive()`
  - non-buffered rx-off receive start success/failure
  - receive-at timeout versus normal poll timeout
- Expose those counters in `OpenThreadPlatformSkeletonSnapshot`.
- Add them to `ThreadExperimentalSleepyChild` serial print and optional
  `.noinit` marker array.
- Run parent/child for at least 10 minutes with Serial disconnected for power
  and connected for one diagnostic run.

## Matter Status

No Matter runtime behavior was changed in this pass. Matter remains staged:

- Compile-only/foundation examples exist.
- Thread dataset/readiness surfaces exist.
- Real commissioner interop is not proven.
- mDNS/SRP and production commissioning transport remain unfinished.

Do not claim Matter is commissionable until Home Assistant/Apple/Google
commissioning has been validated end-to-end.

## Next Slices

1. Finish Thread sleepy data-poll receive stability.
2. Add SED retention test: parent up, child rx-off, 10+ minute retention, then
   parent-to-child indirect UDP message.
3. Add measured SED current test with Serial disabled and XIAO RF switch
   behavior confirmed.
4. Harden radio ownership across Thread/Zigbee/raw 802.15.4/BLE stage modes.
5. Continue Matter only after Thread attach/reconnect/SED retention are stable,
   because Matter-on-Thread depends on those lower layers.

