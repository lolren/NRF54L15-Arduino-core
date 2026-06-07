# Thread/Matter DNS-SD and SRP Handoff - 2026-06-07

## Current Status

Matter Phase 4 moved from "fully deferred" to a staged SRP implementation:

- Matter-stage builds now enable the OpenThread DNS client, SRP client, and ECDSA config path.
- Generic Thread-only builds still keep DNS/SRP/ECDSA off by default so basic Thread examples do not inherit the extra code size and runtime surface.
- `MatterOnNetworkOnOffLightNode` now builds a commissionable `_matterc._udp` record and queues it through the OpenThread SRP client when the commissioning window is open and Thread is attached.
- When the commissioning window closes, the node now requests SRP host/service removal and keeps the SRP buffers alive until OpenThread reports removal through the SRP callback.
- The current implementation uses mbedTLS/software ECC for the OpenThread ECDSA path. It does not depend on CRACEN hardware ECC microcode.
- Infrastructure mDNS/DNS-SD is still not enabled because there is no platform network interface or CHIP DNSSD adapter for it yet.
- Operational `_matter._tcp` registration is still not implemented because full commissioning/fabric state is not complete yet.

## Code Paths Changed

- OpenThread config:
  - `hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/src/openthread-core-user-config.h`
  - Adds staged `NRF54L15_CLEAN_OPENTHREAD_DNS_SD_ENABLE`, `NRF54L15_CLEAN_OPENTHREAD_SRP_CLIENT_ENABLE`, `NRF54L15_CLEAN_OPENTHREAD_MDNS_ENABLE`, and `NRF54L15_CLEAN_OPENTHREAD_DNSSD_SERVER_ENABLE` gates.
  - Enables `OPENTHREAD_CONFIG_DNS_CLIENT_ENABLE`, `OPENTHREAD_CONFIG_SRP_CLIENT_ENABLE`, and `OPENTHREAD_CONFIG_ECDSA_ENABLE` only for staged Matter builds.

- Matter capability diagnostics:
  - `matter_foundation_target.h`
  - `matter_foundation_target.cpp`
  - Discovery capabilities now report DNS client and ECDSA availability.
  - Commissionable registration is considered possible if infrastructure mDNS/DNSSD exists or if OpenThread SRP plus ECDSA is available.

- Matter SRP publication:
  - `matter_onnetwork_onoff_light.h`
  - `matter_onnetwork_onoff_light.cpp`
  - Adds persistent SRP host, service, TXT, and subtype storage.
  - Adds SRP callback/autostart hooks.
  - Adds `srpClientEnabled`, `srpServiceQueued`, `srpAutoStartEnabled`, and `srpLastError` diagnostics.
  - Adds `srpRemovePending`, `srpHostRegistered`, and `srpServiceRegistered` diagnostics.
  - Queues the commissionable record only while Thread is attached and the commissioning window is open.

- OpenThread source wrappers:
  - `src/openthread_core_stage/api_dns_api.cpp`
  - `src/openthread_core_stage/api_srp_client_api.cpp`
  - `src/openthread_core_stage/net_dns_client.cpp`
  - `src/openthread_core_stage/net_dns_types.cpp`
  - `src/openthread_core_stage/net_srp_client.cpp`
  - `src/openthread_core_stage/common_appender.cpp`
  - `src/openthread_core_stage/net_ip4_types.cpp`

These wrappers were needed because enabling SRP/DNS pulled real OpenThread symbols that were not previously linked into the Arduino core.

## Validation Already Run

All validation below passed locally with `arduino-cli`.

```bash
arduino-cli compile \
  --fqbn nrf54l15clean:nrf54l15clean:xiao_nrf54l15:clean_thread=stage,clean_matter=off \
  hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/examples/Thread/OpenThreadRoleStageProbe
```

Result: generic Thread still compiles without pulling staged Matter SRP by default.

```bash
arduino-cli compile \
  --fqbn nrf54l15clean:nrf54l15clean:xiao_nrf54l15:clean_thread=stage,clean_matter=stage \
  hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/examples/Matter/MatterOnOffLightFoundationCompileTarget
```

Result: Matter foundation target compiles with the new discovery diagnostics.

```bash
arduino-cli compile \
  --fqbn nrf54l15clean:nrf54l15clean:xiao_nrf54l15:clean_thread=stage,clean_matter=stage \
  hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/examples/Matter/MatterOnNetworkOnOffLightCommandSurfaceDemo
```

Result: Matter command-surface example links with OpenThread SRP/DNS symbols.

```bash
arduino-cli compile \
  --fqbn nrf54l15clean:nrf54l15clean:xiao_nrf54l15:clean_thread=stage,clean_matter=stage \
  hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/examples/Matter/MatterOnNetworkOnOffLightNodeDemo
```

Result: Matter on-network node demo links with OpenThread SRP/DNS symbols.

## Runtime Test Needed

This still needs a real Thread border router or SRP server in Thread Network Data.

Use a Matter-stage build of:

```text
hardware/nrf54l15clean/nrf54l15clean/libraries/Nrf54L15-Clean-Implementation/examples/Matter/MatterOnNetworkOnOffLightCommandSurfaceDemo
```

Expected diagnostics once Thread is attached and a commissioning window is open:

```text
discovery_srp=1
discovery_dns_client=1
discovery_ecdsa=1
publish_ready=1
publish_active=1
publish_srp_client=1
publish_srp_queued=1
publish_srp_autostart=1
publish_srp_host_registered=1
publish_srp_service_registered=1
publish_srp_last_error=0
```

If no SRP server is present in Thread Network Data, `publish_srp_queued=1` can still be true while the service is only queued locally. That is expected and should not be claimed as real network discovery.

## Remaining Work For Full Matter Discovery

- Validate commissionable `_matterc._udp` SRP registration against an OTBR or another real Thread border router.
- Confirm the record appears from the commissioner side while the commissioning window is open.
- Validate clean unregister semantics when the commissioning window closes. The current staged path requests SRP host/service removal and tracks `srpRemovePending`, but server-side removal still needs runtime validation.
- Implement operational `_matter._tcp` registration after fabric/CASE commissioning state exists.
- Implement a CHIP DNSSD platform bridge so discovery is not tied only to the current on-network light wrapper.
- Keep infrastructure mDNS separate from Thread SRP. Do not enable `OPENTHREAD_CONFIG_MULTICAST_DNS_ENABLE` globally until a platform interface exists and memory/power impact has been measured.
- Add negative tests: no Thread attach, no SRP server, commissioning window closed, identity changed while window open, repeated open/close cycles, and reboot with saved Thread dataset.

## Important Caveats

- This is not full Matter commissioning.
- This does not make Home Assistant, Apple Home, or Google Home commissioning work by itself.
- This does not add BLE rendezvous commissioning.
- This does not add production fabric storage or operational CASE validation.
- This is a necessary discovery-layer step that makes staged on-network Matter bring-up less fake and easier to test against a real Thread border router.
