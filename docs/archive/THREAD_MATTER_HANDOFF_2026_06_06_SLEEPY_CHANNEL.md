# Thread/Matter Handoff - 2026-06-06 Sleepy Channel Fix

This handoff records the current Thread/Matter state after reviewing
`THREAD_MATTER_SESSION_REPORT_2026_06_06_FINAL.md`, fixing the reported sleepy
child channel issue, and validating the nearby Thread examples.

## Fixed In This Pass

- Added/finished `Nrf54ThreadExperimental::beginAsSleepyChild()`.
- Added `Nrf54ThreadExperimental::setPollPeriod()` and `getPollPeriod()`.
- Sleepy child attach now starts directly as true sleepy MTD
  (`rxOnWhenIdle=false`, `deviceType=false`, `networkData=true`) before
  IP6/Thread enable. This matches OpenThread's expected SED attach path and
  avoids the intentional reattach behavior triggered when changing from rx-on
  MTD to rx-off MTD after attachment.
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
- Propagated MAC ACK Frame Pending status from the nRF54 raw 802.15.4 receive
  path into OpenThread's `mAckedWithFramePending` field. OpenThread depends on
  this flag to enter its data-poll receive window.
- Fixed secure MAC data-poll source matching in both the HAL ACK prefilter and
  the OpenThread platform callback parser. OpenThread data polls are
  security-enabled MAC command frames, so the command byte is after the aux
  security header, not immediately after the source address.

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
  rx_on_when_idle=0 observed
```

The channel bug from the session report is therefore fixed.

## Sleepy Child Stability Status

The direct rx-off attach path is now hardware-tested. After the HAL and
OpenThread platform source-match fixes, a two-board 90 second serial soak showed
the child stable as:

```text
role=child
attached=1
channel=15
pan=0x5D6A
short=0xC801
rx_on_when_idle=0
```

The earlier failure mode was:

```text
Mle: Send Child ID Request
Mac: Sent data poll, fp:no
Attach attempt N unsuccessful
```

Root cause:

- The child was correctly sending secured MAC data-poll command frames.
- The parent HAL prefilter rejected those frames because it expected command ID
  `0x04` immediately after the source address.
- OpenThread's MAC frame builder inserts the aux security header before the
  command byte for the default Thread data-poll path.
- Because the platform callback was not reached, source-match never set Frame
  Pending in the parent ACK, so the child never received the pending Child ID
  response.

Current caveat:

- The 90 second soak proves attach and short-term retention, not production SED
  retention. The next validation should be a 10+ minute Serial-disconnected
  soak plus parent-to-child indirect UDP after the child is already attached.
  Do not regress this by forcing rx-on mode or adding sketch delays.

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
