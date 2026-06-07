# Thread/Matter Next-AI Handover - 2026-06-07

This is the current handover for another AI/developer to continue Thread and
Matter work in the nRF54L15 Arduino core.

## Current Repository State

- Repo path: `/home/lolren/Desktop/test_pi_nrf54/NRF54L15-Clean-Arduino-core`
- Branch: `main`
- Current pushed commit: `0d35f99 Advance staged Thread Matter runtime`
- Remote: `https://github.com/lolren/nrf54-arduino-core.git`
- At the time this handover was written, `main` was pushed to `origin/main`.

Do not use older duplicate folders as the source of truth unless you are doing a
manual comparison. The current working repo is the path above.

## Important Rules For The Next AI

- Do not touch Serial/USB/UART unless the bug is explicitly in serial. Serial was
  fragile in earlier work and should not be refactored during Thread/Matter work.
- Do not reintroduce the speculative `nrf54_radio_owner.*` layer. It was rejected
  because it did not consistently arbitrate RADIO ownership.
- Do not claim CRACEN hardware ECC is implemented. Public Nordic support still
  requires unavailable microcode for the P-256 path. Current Matter/Thread ECC is
  software/mbedTLS/staged.
- Do not enable infrastructure mDNS globally just to make a checkbox green.
  `OPENTHREAD_CONFIG_MULTICAST_DNS_ENABLE` must stay off until there is a real
  platform interface and measured memory/power impact.
- Do not break generic Thread builds while enabling Matter features. Thread-only
  builds should not pull Matter SRP/DNS/ECDSA unless explicitly selected.
- Validate after every large change with `arduino-cli compile` and, when possible,
  two-board or OTBR runtime tests.

## What Was Implemented In The Latest Slice

Matter Phase 4 moved from fully deferred to a staged SRP path:

- Matter-stage builds now enable OpenThread DNS client, SRP client, and ECDSA
  config gates.
- Generic Thread-only builds keep DNS/SRP/ECDSA off by default.
- `MatterOnNetworkOnOffLightNode` builds a commissionable `_matterc._udp`
  record and queues it through OpenThread SRP when:
  - Thread is attached.
  - The commissioning window is open.
  - Matter readiness passes.
- When the commissioning window closes, the node requests SRP host/service
  removal via `otSrpClientRemoveHostAndServices()` and keeps the SRP buffers
  alive until the OpenThread SRP callback reports removal.
- Diagnostics now expose whether SRP is only queued, actually registered, or
  waiting for unregister.
- Source wrappers were added for the OpenThread DNS/SRP implementation units
  needed by the staged build.
- Matter credential defaults and RNG paths were centralized and hardened.
- The Thread sleepy-child RX audit/fixes were included in the same pushed commit.

## Most Relevant Files

Matter SRP/DNS-SD:

- `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/openthread-core-user-config.h`
- `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/matter_foundation_target.h`
- `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/matter_foundation_target.cpp`
- `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/matter_onnetwork_onoff_light.h`
- `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/matter_onnetwork_onoff_light.cpp`

OpenThread DNS/SRP wrappers:

- `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/openthread_core_stage/api_dns_api.cpp`
- `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/openthread_core_stage/api_srp_client_api.cpp`
- `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/openthread_core_stage/common_appender.cpp`
- `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/openthread_core_stage/net_dns_client.cpp`
- `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/openthread_core_stage/net_dns_types.cpp`
- `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/openthread_core_stage/net_ip4_types.cpp`
- `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/openthread_core_stage/net_srp_client.cpp`

Matter credentials/RNG cleanup:

- `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/matter_credentials.h`
- `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/matter_credentials.cpp`
- `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/matter_rng.h`
- `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/matter_rng.cpp`
- `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/matter_case_session.cpp`
- `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/matter_pase_commissioning.cpp`

Runtime diagnostics example:

- `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/examples/Matter/MatterOnNetworkOnOffLightCommandSurfaceDemo/MatterOnNetworkOnOffLightCommandSurfaceDemo.ino`

Related handovers:

- `docs/THREAD_MATTER_DNSSD_SRP_HANDOFF_2026_06_07.md`
- `docs/THREAD_MATTER_RX_AUDIT_HANDOFF_2026_06_06.md`
- `docs/MATTER_PLATFORM_BOUNDARY_CLEANUP.md`
- `docs/NRF54L15_FEATURE_MATRIX.md`

## Build Commands That Passed

Run from repo root:

```bash
arduino-cli compile \
  --fqbn nrf54l15clean:nrf54l15clean:xiao_nrf54l15:clean_thread=stage,clean_matter=off \
  hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/examples/Thread/OpenThreadRoleStageProbe
```

```bash
arduino-cli compile \
  --fqbn nrf54l15clean:nrf54l15clean:xiao_nrf54l15:clean_thread=stage,clean_matter=stage \
  hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/examples/Matter/MatterOnOffLightFoundationCompileTarget
```

```bash
arduino-cli compile \
  --fqbn nrf54l15clean:nrf54l15clean:xiao_nrf54l15:clean_thread=stage,clean_matter=stage \
  hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/examples/Matter/MatterOnNetworkOnOffLightCommandSurfaceDemo
```

```bash
arduino-cli compile \
  --fqbn nrf54l15clean:nrf54l15clean:xiao_nrf54l15:clean_thread=stage,clean_matter=stage \
  hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/examples/Matter/MatterOnNetworkOnOffLightNodeDemo
```

Also run before committing:

```bash
git diff --check
```

## Flashing And Serial Test Basics

List boards:

```bash
arduino-cli board list
ls -l /dev/serial/by-id/ || true
```

Compile/upload the command surface demo:

```bash
arduino-cli upload -p /dev/ttyACM0 \
  --fqbn nrf54l15clean:nrf54l15clean:xiao_nrf54l15:clean_thread=stage,clean_matter=stage \
  hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/examples/Matter/MatterOnNetworkOnOffLightCommandSurfaceDemo
```

Open serial:

```bash
arduino-cli monitor -p /dev/ttyACM0 -c baudrate=115200
```

The command surface accepts:

```text
help
state
thread-stats
discovery
open-window 300
close-window
bundle
manual
qr
identity
pin <setup-pin>
discriminator <value>
vendor <id>
product <id>
dataset-hex <ot-tlv-hex>
forget-dataset
factory-reset
on
off
toggle
identify <seconds>
stop-identify
```

## Matter SRP Runtime Test Plan

### Test 1: No external SRP server

Goal: prove the local SRP path queues cleanly and does not break without a real
Thread border router.

1. Flash `MatterOnNetworkOnOffLightCommandSurfaceDemo`.
2. Open serial at 115200.
3. Send:

```text
state
thread-stats
discovery
open-window 300
discovery
close-window
discovery
```

Expected compile/config diagnostics:

```text
matter_cmd_demo discovery_dns_client=1
matter_cmd_demo discovery_ecdsa=1
matter_cmd_demo discovery_srp_client=1
matter_cmd_demo discovery_register_capable=1
```

Expected without a real SRP server:

```text
matter_cmd_demo discovery_publish_srp_queued=1
matter_cmd_demo discovery_publish_srp_autostart=0
matter_cmd_demo discovery_publish_srp_host_registered=0
matter_cmd_demo discovery_publish_srp_service_registered=0
```

This means the service is queued locally, not proven visible on the network.
Do not claim real discovery from this test alone.

### Test 2: Real Thread border router / SRP server

Goal: prove the `_matterc._udp` record is actually registered and removed.

Requirements:

- A real Thread border router or OTBR with SRP server in Thread Network Data.
- The Arduino board must use the same Thread dataset as that network.

Steps:

1. Get the active dataset TLV hex from the border router or reference Thread
   network.
2. Flash `MatterOnNetworkOnOffLightCommandSurfaceDemo`.
3. On serial, send:

```text
dataset-hex <ot-tlv-hex>
state
thread-stats
open-window 300
discovery
```

Expected if attached and SRP server is visible:

```text
matter_cmd_demo readiness_thread_attached=1
matter_cmd_demo discovery_ready=1
matter_cmd_demo discovery_publish_active=1
matter_cmd_demo discovery_publish_srp_client=1
matter_cmd_demo discovery_publish_srp_queued=1
matter_cmd_demo discovery_publish_srp_autostart=1
matter_cmd_demo discovery_publish_srp_host_registered=1
matter_cmd_demo discovery_publish_srp_service_registered=1
matter_cmd_demo discovery_publish_srp_last_error=0
matter_cmd_demo discovery_publish_blocker=srp_publication_queued
```

Then close the window:

```text
close-window
discovery
```

Expected during removal:

```text
matter_cmd_demo discovery_publish_srp_remove_pending=1
```

Expected after OpenThread callback confirms removal:

```text
matter_cmd_demo discovery_publish_srp_remove_pending=0
matter_cmd_demo discovery_publish_srp_queued=0
matter_cmd_demo discovery_publish_srp_host_registered=0
matter_cmd_demo discovery_publish_srp_service_registered=0
```

If removal stays pending forever, inspect `onSrpClientCallback()` in
`matter_onnetwork_onoff_light.cpp` and verify OpenThread is being processed by
`Nrf54MatterOnNetworkOnOffLightNode::process()`.

### Test 3: Verify service externally

On an OTBR, useful commands depend on the installed OTBR version. Try:

```bash
sudo ot-ctl state
sudo ot-ctl netdata show
sudo ot-ctl srp server state
sudo ot-ctl srp server service
```

If OTBR advertising proxy is enabled, also try from the LAN side:

```bash
avahi-browse -rt _matterc._udp
dns-sd -B _matterc._udp local
```

Expected service type:

```text
_matterc._udp
```

Expected TXT fragments from the Arduino record include:

```text
D=<discriminator>
VP=<vendor+product>
CM=1
DT=257
DN=<device-name>
SII=5000
SAI=300
SAT=4000
T=0
```

If the Arduino serial says registered but OTBR cannot show the service, check:

- Whether the OTBR SRP server is actually running.
- Whether the Arduino attached to the OTBR network or formed its own partition.
- Whether the dataset TLV was imported correctly.
- Whether `discovery_publish_srp_autostart=1`.
- Whether `discovery_publish_srp_last_error` is nonzero.

## Common Failure Meanings

- `thread_not_attached`: the board did not join the intended Thread network.
- `readiness_unknown` or a readiness blocker: Matter wrapper did not reach its
  local ready state. Use `state` and `thread-stats`.
- `openthread_srp_requires_ecdsa`: SRP was enabled without ECDSA. This should
  not happen in Matter-stage builds after commit `0d35f99`.
- `openthread_mdns_srp_disabled`: wrong build profile or config gates disabled.
- `srp_publish_failed`: inspect `discovery_publish_srp_last_error`.
- `srp_unpublish_pending`: unregister has been requested and OpenThread still
  owns the SRP buffers.
- `discovery_publish_srp_queued=1` with registered flags `0`: queued locally,
  but not yet registered with a server.
- `discovery_publish_srp_autostart=0`: no SRP server selected from Thread
  Network Data yet.

OpenThread error values are defined under:

```text
hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/openthread/error.h
```

## How To Add And Read SWD Memory Counters

Use serial first when possible. Use SWD counters when serial logging changes
timing or power.

Add counters to a sketch or source file:

```cpp
__attribute__((used, section(".noinit")))
volatile uint32_t gDebugMarkers[32] = {0};
```

Update counters in the suspected path:

```cpp
gDebugMarkers[0]++;
gDebugMarkers[1] = static_cast<uint32_t>(someState);
```

Build with a fixed build path so the ELF is easy to inspect:

```bash
rm -rf /tmp/nrf54-matter-build
arduino-cli compile \
  --build-path /tmp/nrf54-matter-build \
  --fqbn nrf54l15clean:nrf54l15clean:xiao_nrf54l15:clean_thread=stage,clean_matter=stage \
  hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/examples/Matter/MatterOnNetworkOnOffLightCommandSurfaceDemo
```

Find the symbol:

```bash
arm-none-eabi-nm -n /tmp/nrf54-matter-build/*.elf | rg 'gDebugMarkers|gSleepyMarkers'
```

List probes:

```bash
pyocd list
```

Read memory:

```bash
pyocd commander -u <probe_uid>
```

Inside `pyocd commander`:

```text
halt
read32 <symbol_address> 32
go
exit
```

The address is not fixed. Always resolve it from the current ELF/map.

Existing marker example:

- `ThreadExperimentalSleepyChild` uses `gSleepyMarkers`.

## Thread-Specific Context

The last Thread RX audit fixed and preserved:

- Sleepy-child UDP RX buffer overflow fix.
- Direct rx-off sleepy child attach path.
- ACK frame-pending propagation into `mAckedWithFramePending`.

Do not reintroduce a two-phase rx-on attach workaround unless a new hardware
test proves direct rx-off attach is broken.

Thread runtime still needs:

- Two-board sleepy child indirect traffic soak.
- Clean settings-wipe MeshCoP joiner/commissioner runs.
- Wrong-PSKd negative joiner proof.
- Reboot persistence proof.
- Reference network attach against OTBR, Zephyr/NCS, or Nordic OpenThread CLI.

## Matter-Specific Context

The current Matter code is still staging/foundation support, not production
Matter.

Works at staging level:

- On/off light data model.
- Manual and QR onboarding code generation.
- PASE/CASE demo state machines compile.
- Software secp256r1 path.
- CRACEN-backed random generation wrapper for Matter sensitive random bytes.
- Staged `_matterc._udp` SRP queue/unregister path.
- Thread dataset import/export surfaces.

Not finished:

- Full CHIP secure-session integration.
- Real PASE/CASE interop with a commissioner.
- Fabric storage.
- Operational `_matter._tcp` SRP registration.
- Infrastructure mDNS/DNS-SD.
- CHIP DNSSD platform bridge.
- BLE rendezvous commissioning.
- Home Assistant / Apple Home / Google Home commissioning proof.

## Next Best Implementation Order

1. Re-run the four compile commands from this handover.
2. Hardware-test `MatterOnNetworkOnOffLightCommandSurfaceDemo` without a border
   router and confirm the expected local queued SRP state.
3. Hardware-test against a real OTBR/SRP server using `dataset-hex`.
4. If registered flags never go high, debug OpenThread SRP autostart/server
   selection before changing Matter code.
5. If unregister remains pending, debug `onSrpClientCallback()` and whether
   `process()` is being called often enough.
6. Once `_matterc._udp` is proven visible externally, implement operational
   `_matter._tcp` registration only after there is real fabric/CASE state to
   publish.
7. Add a CHIP DNSSD adapter layer so discovery is not hardwired into the on/off
   light wrapper.
8. Only after discovery works, resume full commissioner/Home Assistant tests.

## Minimum Regression Matrix After Any Fix

Compile:

```bash
arduino-cli compile --fqbn nrf54l15clean:nrf54l15clean:xiao_nrf54l15:clean_thread=stage,clean_matter=off \
  hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/examples/Thread/OpenThreadRoleStageProbe

arduino-cli compile --fqbn nrf54l15clean:nrf54l15clean:xiao_nrf54l15:clean_thread=stage,clean_matter=stage \
  hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/examples/Matter/MatterOnOffLightFoundationCompileTarget

arduino-cli compile --fqbn nrf54l15clean:nrf54l15clean:xiao_nrf54l15:clean_thread=stage,clean_matter=stage \
  hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/examples/Matter/MatterOnNetworkOnOffLightCommandSurfaceDemo

arduino-cli compile --fqbn nrf54l15clean:nrf54l15clean:xiao_nrf54l15:clean_thread=stage,clean_matter=stage \
  hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/examples/Matter/MatterOnNetworkOnOffLightNodeDemo
```

Static:

```bash
git diff --check
```

Runtime:

- Command surface: `state`, `thread-stats`, `open-window 300`, `discovery`,
  `close-window`, `discovery`.
- If two boards are connected, also rerun the Thread sleepy parent/child or UDP
  soak path touched by any radio/platform change.

## What A Passing SRP Fix Looks Like

A real pass is not just a compile pass.

A real pass means:

- Arduino attaches to the intended Thread network.
- Arduino sees/selects an SRP server.
- `_matterc._udp` is registered externally.
- OTBR or LAN-side DNS-SD tooling sees the service.
- Closing the commissioning window removes the SRP record.
- Reopening the window registers it again.
- Generic Thread-only build still compiles and does not pull Matter SRP/DNS.
- No Serial/BLE regression is introduced.

Until those are all true, keep the README wording experimental.
