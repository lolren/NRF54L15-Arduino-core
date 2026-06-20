# Zigbee Full-Support Handoff

Date: 2026-06-20

This document is the continuation plan for taking the current bare-metal
Zigbee implementation from interoperable examples to a robust Zigbee PRO /
Zigbee 3.0 implementation. It also records how to use the local Home Assistant
and Zigbee2MQTT installation for repeatable testing.

## Executive Summary

The core already contains substantially more than a raw 802.15.4 demo:

- IEEE 802.15.4 transmit, receive, CCA, energy detect, MAC ACK generation,
  frame-pending support, buffered IRQ receive, association, orphan notification,
  coordinator realignment, and beacon helpers.
- Zigbee NWK and APS frame codecs for the currently supported direct paths.
- AES-CCM* NWK/APS security, the Zigbee Alliance link key, install-code key
  derivation, network-key transport/update/switch handling, replay counters,
  persistent network state, secure rejoin, and end-device timeout negotiation.
- Coordinator, router, and end-device examples.
- ZDO descriptors, address discovery, bind/unbind, leave, permit-join, and
  management-table responses.
- ZCL global commands, reporting, binding, Identify, Groups, Scenes, On/Off,
  Level, Color, Temperature, Humidity, Basic, and Power Configuration support.
- Home Assistant and Zigbee2MQTT examples, an external converter, and local
  serial/MQTT validation scripts.

This is still not a complete Zigbee PRO stack. The current design is a protocol
toolkit used by several example-specific runtimes. Full support requires a
single event-driven runtime with complete MAC retry behavior, NWK routing and
broadcast handling, APS reliability and fragmentation, broader ZDO/ZCL
coverage, OTA, persistent table management, security hardening, and long
interoperability testing.

Nordic's reference implementation is not simply Zephyr code that can be copied.
The nRF Connect SDK Zigbee path uses the precompiled third-party ZBOSS stack.
Nordic documents ZBOSS as providing the mandatory Zigbee 3.0 and Zigbee PRO
features, all roles, ZCL, FOTA, Base Device Behavior, Green Power Proxy Basic,
and additional device definitions. The current CleanCore implementation is an
independent bare-metal stack and must implement and validate those behaviors
itself.

Reference:

- <https://docs.nordicsemi.com/bundle/ncs-2.9.1/page/nrf/protocols/zigbee/supported_features.html>
- <https://docs.nordicsemi.com/bundle/addon-zigbee-r23-latest/page/release-notes.html>

## Current Source Map

| Layer | Main files | Current role |
|---|---|---|
| IEEE 802.15.4 radio | `src/nrf54l15_hal_parts/nrf54l15_hal_802154_rawradio.inc` | PHY configuration, TX/RX, CCA/ED, MAC ACKs, buffered receive |
| MAC/NWK/APS/ZDO/ZCL | `src/zigbee_stack.h`, `src/zigbee_stack.cpp` | Frame codecs, device model, descriptors, ZCL handlers |
| Commissioning | `src/zigbee_commissioning.h`, `src/zigbee_commissioning.cpp` | Scan, association, steering, secure rejoin, trust-center commands |
| Security | `src/zigbee_security.h`, `src/zigbee_security.cpp` | CCM*, keys, secured NWK/APS frames |
| Persistence | `src/zigbee_persistence.h`, `src/zigbee_persistence.cpp` | Network identity, keys, counters, reporting and bindings |
| Examples | `examples/Zigbee/` | Coordinator/router/end-device, lights, sensors, sleepy devices |
| Host validation | `scripts/zigbee_*.py` | Two-board serial and Home Assistant/Zigbee2MQTT tests |
| Zigbee2MQTT converter | `extras/zigbee2mqtt/cleancore_nrf54_examples.mjs` | CleanCore example model definitions |

The Zigbee sources currently total about 13,600 lines. `zigbee_stack.cpp` alone
is over 7,300 lines. Split it by protocol layer before adding substantial new
behavior:

```text
zigbee/
  mac_codec.cpp
  nwk_codec.cpp
  nwk_routing.cpp
  nwk_broadcast.cpp
  aps_codec.cpp
  aps_delivery.cpp
  aps_fragmentation.cpp
  zdo.cpp
  zcl_foundation.cpp
  zcl_clusters_lighting.cpp
  zcl_clusters_measurement.cpp
  zcl_clusters_security.cpp
  zcl_ota.cpp
  device_runtime.cpp
```

The split must be behavior-preserving and covered by host tests before feature
work continues.

## Confirmed Gaps

### 1. MAC Reliability

The radio HAL performs one CCA check before transmission. It does not provide a
complete unslotted CSMA-CA transaction with randomized backoff, configurable
`macMinBE`/`macMaxBE`, retry limits, or a reusable retry queue.

Required:

- Randomized backoff and CCA retry state machine.
- MAC retry count and ACK timeout policy.
- Duplicate sequence suppression.
- Broadcast handling and passive acknowledgement behavior where required.
- Promiscuous/sniffer mode kept separate from normal filtering.
- Event-driven timers rather than sketch-level busy loops.
- Radio statistics for CCA busy, no ACK, retry success, CRC failure, filter
  rejection, RX queue overflow, and late ACK.
- Two-board collision tests with an intentional interferer.

### 2. NWK Routing And Broadcasts

The current NWK codec explicitly rejects source-route and multicast frames.
There are management-table APIs, but tables are largely sketch-configured and
are not maintained by a production routing engine.

Required:

- Route Request, Route Reply, Route Record, Network Status, Link Status, and
  Network Report/Update command codecs.
- Route discovery, route aging, repair, failure handling, and retry queues.
- Neighbor aging and link-cost updates from LQI/RSSI.
- Many-to-one routing and concentrator behavior.
- Source-route subframe parse/build and relay.
- Zigbee broadcast transaction table and duplicate suppression.
- Broadcast radius handling and rebroadcast jitter.
- Group/multicast forwarding.
- Child, sibling, parent, and previous-child relationship maintenance.
- Coordinator/router address allocation and conflict handling.
- Channel-change and PAN ID conflict recovery.
- Multi-hop tests with at least three devices; two boards cannot validate
  production routing.

### 3. APS Reliability

APS data and ACK codecs exist, but there is no general APS delivery manager.

Required:

- APS outstanding-transaction table.
- APS ACK timeout and retransmission.
- Duplicate APS counter suppression.
- Binding-based fan-out to group and extended destinations.
- APS fragmentation/reassembly for payloads larger than one MAC frame.
- Extended timeout handling for sleepy end devices.
- Group-address delivery and multicast integration.
- Delivery status callbacks and application transaction IDs.
- Queue limits, backpressure, and deterministic failure reporting.

### 4. Security And Zigbee 3.0 Base Device Behavior

The crypto primitives and several trust-center commands are implemented, but
full Zigbee 3.0 security behavior needs validation as a system.

Required:

- Formal key-table abstraction: network keys, alternate key, link keys,
  install-code-derived keys, and trust-center policy.
- Atomic frame-counter reservation. Do not write flash on every packet; reserve
  counter ranges so power loss can never reuse a transmitted nonce.
- Per-neighbor incoming frame counters rather than only broad global counters.
- Key update and key-switch soak tests across reset and power loss.
- Install-code-only joining tested against Zigbee2MQTT-compatible coordinator
  behavior.
- Secure and unsecured rejoin policy tests.
- Device leave/remove/reset policy and factory-new transition tests.
- Negative tests: replay, wrong MIC, stale key sequence, unknown source IEEE,
  malformed auxiliary header, and counter rollback.
- Base Device Behavior commissioning state machine, including retry budgets,
  backoff, network steering, finding and binding, and consistent status events.

### 5. Coordinator And Trust Center

The coordinator examples can form a network and admit current examples, but a
production coordinator requires:

- Persistent device/address table.
- Deterministic short-address allocation and reuse policy.
- Full trust-center device state and authorization policy.
- Child aging, timeout, removal, and rejoin handling.
- Network key rotation and broadcast delivery.
- Permit-join policy with expiry and application callbacks.
- Coordinator restart recovery without forcing all children to rejoin.
- Capacity limits and clear errors when tables are full.

### 6. ZDO Completeness

The implementation covers common descriptor, binding, leave, permit-join, and
management requests. Add and validate:

- Correct paging and total-entry counts for all management responses.
- Discovery Cache and Complex/User Descriptor behavior if claimed.
- Network Update and channel-management requests.
- Robust End Device Bind behavior rather than a minimal response.
- Device and Service discovery under concurrent requests.
- ZDO transaction timeout/retry manager.
- Correct status codes for malformed, unsupported, unauthorized, and
  out-of-capacity requests.

### 7. ZCL Foundation

Global read/write/discover/reporting support exists. Complete:

- All required scalar data types, strings, arrays, structures, sets, bags, time,
  date, IEEE address, security key, floating point, and signed integer widths.
- Type-aware length validation and endian-safe serialization.
- Structured reads/writes and manufacturer-specific attributes.
- Attribute access callbacks and per-endpoint attribute stores.
- Default-response rules for every command path.
- Reporting scheduler with min/max interval, reportable change, sleepy-device
  behavior, retry policy, and persistence.
- Multi-endpoint and manufacturer-specific command tests.

### 8. ZCL Cluster Coverage

Current clusters are Basic, Power Configuration, Identify, Groups, Scenes,
On/Off, On/Off Switch Configuration, Level, Color, Temperature, and Humidity.

High-value next clusters:

1. Illuminance Measurement.
2. Pressure Measurement.
3. Occupancy Sensing.
4. IAS Zone and IAS Warning Device.
5. Electrical Measurement.
6. Metering.
7. Thermostat and Fan Control.
8. Door Lock.
9. Time.
10. Poll Control.
11. Diagnostics.
12. OTA Upgrade.

Each cluster needs server and client command coverage, attributes, reporting,
Zigbee2MQTT conversion, examples, and malformed-frame tests. Do not mark a
cluster complete because only its constants or one command exist.

### 9. OTA Upgrade

`kZigbeeClusterOtaUpgrade` is declared, but there is no complete OTA client or
server.

Required:

- Query Next Image, Image Block/Page, Upgrade End, and Image Notify.
- OTA image header parser and validation.
- Manufacturer/image type/hardware range matching.
- Block retry, resume after reset, and persisted progress.
- Signature/hash validation and atomic boot handoff.
- Integration with the core's flash layout and boot process.
- Zigbee2MQTT OTA test with a deliberately interrupted transfer.

### 10. Green Power, Touchlink, And Optional Features

These are not required for the first production milestone. Keep them explicitly
out of scope until Zigbee PRO routing, APS reliability, security, ZCL
foundation, and OTA are complete:

- Green Power Proxy Basic.
- Touchlink/inter-PAN commissioning.
- WWAH.
- NCP/RCP host mode.

## Recommended Implementation Order

### Phase Z0: Freeze And Measure The Baseline

- Compile every Zigbee example on all supported MCU classes.
- Run the current two-board join, rejoin, light, button, and sleepy-climate
  tests.
- Run Home Assistant and Zigbee2MQTT interview/state tests.
- Record flash, RAM, join time, report latency, packet loss, and idle current.
- Capture known-good packets with an 802.15.4 sniffer and Wireshark.
- Add the results to a dated test report.

Exit condition: reproducible baseline with logs and packet captures.

### Phase Z1: Split The Monolith And Add Host Unit Tests

- Split `zigbee_stack.cpp` by layer without changing public behavior.
- Add native host tests for every frame codec and crypto known-answer vector.
- Add fuzz-style malformed-length tests.
- Add CI compilation for all Zigbee examples, not only five legacy examples.

Exit condition: identical two-board and HA behavior after the split.

### Phase Z2: MAC Transaction Engine

- Add event-driven CSMA-CA, retry, ACK timeout, duplicate filtering, and stats.
- Make the engine reusable by Zigbee and Thread without changing either
  protocol's policy layer.

Exit condition: collision/retry and induced packet-loss tests pass.

### Phase Z3: NWK Routing Engine

- Implement command codecs, neighbor maintenance, broadcast table, route
  discovery/repair, many-to-one routing, source routing, and channel changes.

Exit condition: three-node multi-hop traffic survives router loss and repair.

### Phase Z4: APS Delivery Manager

- Add ACK/retry, duplicate suppression, bindings, groups, fragmentation, and
  sleepy-device timeout handling.

Exit condition: reliable bidirectional maximum-size application transfers pass
under loss, reset, and sleepy polling.

### Phase Z5: Security And Base Device Behavior

- Harden keys/counters, finish commissioning policy, and add negative tests.

Exit condition: join/rejoin/key-rotation/power-loss matrix passes without nonce
reuse or accepting replayed frames.

### Phase Z6: ZDO And ZCL Foundation

- Complete transaction management, missing data types, reporting, endpoint
  stores, status handling, and manufacturer-specific paths.

Exit condition: automated descriptor, discovery, reporting, and malformed-frame
matrix passes.

### Phase Z7: High-Value Clusters

- Implement the cluster list above in small independently tested slices.
- Update the Zigbee2MQTT converter for each stable model.

Exit condition: Home Assistant creates the expected entities and commands,
reports, persistence, and error paths pass.

### Phase Z8: OTA

- Implement and test resumable, validated OTA with safe boot handoff.

Exit condition: interrupted OTA resumes and installs correctly; invalid images
are rejected without damaging the running image.

### Phase Z9: Soak, Power, And Interoperability

- 24-hour router/end-device/sleepy-device tests.
- Coordinator and device restart loops.
- Packet-loss and RF-interference tests.
- Table exhaustion tests.
- Multiple endpoints and mixed vendor devices.
- Power comparison against Nordic ZBOSS/Zephyr reference applications using
  equivalent intervals, TX power, RF switch policy, payloads, and LEDs.

Exit condition: no leaks, stalls, counter rollback, or permanent join failure.

### Phase Z10: Certification Readiness

- Build a Zigbee specification requirement matrix.
- Run official or accredited Zigbee test tooling.
- Document unsupported optional features honestly.

Do not claim Zigbee 3.0 certification based only on Zigbee2MQTT interoperability.

## Local Home Assistant Connection

Local services:

```text
Home Assistant: http://192.168.1.100:8123
Zigbee2MQTT UI through HA ingress:
  http://192.168.1.100:8123/45df7312_zigbee2mqtt
MQTT broker used by the existing tests:
  192.168.1.100:1883
```

The Home Assistant REST API does not accept Basic Authentication. The helper
script performs the same local login flow as the frontend:

1. `POST /auth/login_flow`
2. Submit the local Home Assistant username/password to the returned flow.
3. Exchange the authorization code at `POST /auth/token`.
4. Use the temporary bearer token with `/api/*`.

Use environment variables so the password is not committed or placed in the
command arguments:

```bash
cd /home/lolren/Desktop/test_pi_nrf54/NRF54L15-Clean-Arduino-core

export HA_URL=http://192.168.1.100:8123
export HA_USERNAME=lolren
export HA_PASSWORD=lolren

python3 scripts/home_assistant_zigbee2mqtt.py status
python3 scripts/home_assistant_zigbee2mqtt.py entities
python3 scripts/home_assistant_zigbee2mqtt.py \
  state switch.zigbee2mqtt_bridge_permit_join
```

Open and close joining:

```bash
python3 scripts/home_assistant_zigbee2mqtt.py permit-join --seconds 180
python3 scripts/home_assistant_zigbee2mqtt.py close-join
```

Interview or remove a device:

```bash
python3 scripts/home_assistant_zigbee2mqtt.py \
  interview 0xd0acf9feff59226e

python3 scripts/home_assistant_zigbee2mqtt.py \
  remove 0xd0acf9feff59226e --force
```

Publish any Zigbee2MQTT request through Home Assistant:

```bash
python3 scripts/home_assistant_zigbee2mqtt.py publish \
  zigbee2mqtt/bridge/request/device/interview \
  '{"id":"0xd0acf9feff59226e"}'
```

If a long-lived Home Assistant token is available, use it instead of the
username/password flow:

```bash
export HA_TOKEN='...'
unset HA_USERNAME HA_PASSWORD
python3 scripts/home_assistant_zigbee2mqtt.py status
```

The script was validated against the local instance on 2026-06-20. The API
returned `API running.` and exposed the Zigbee2MQTT bridge entities, including
`switch.zigbee2mqtt_bridge_permit_join`.

## Direct MQTT Validation

Direct MQTT is better than polling Home Assistant when packet-by-packet
Zigbee2MQTT events are required:

```bash
mosquitto_sub \
  -h 192.168.1.100 -p 1883 \
  -u lolren -P lolren \
  -v \
  -t 'zigbee2mqtt/bridge/#' \
  -t 'zigbee2mqtt/#' \
  -t 'homeassistant/#'
```

Open joining:

```bash
mosquitto_pub \
  -h 192.168.1.100 -p 1883 \
  -u lolren -P lolren \
  -t zigbee2mqtt/bridge/request/permit_join \
  -m '{"value":true,"time":180}'
```

The existing end-to-end MQTT validator is:

```text
scripts/zigbee_sleepy_ha_mqtt_validation.py
```

The existing direct Home Assistant validator is:

```text
scripts/zigbee_sleepy_ha_validation.py
```

## Existing Hardware Validation Scripts

| Script | Purpose |
|---|---|
| `zigbee_ha_router_validation.py` | Coordinator/router join, interview, bind, and light commands |
| `zigbee_light_matrix_validation.py` | Compile/flash/test on/off, dimmable, RGB, and RGBW lights |
| `zigbee_rejoin_regression.py` | Join, reset, secure rejoin, and command delivery |
| `zigbee_sleepy_button_validation.py` | Sleepy button join, bind/report, and sleep |
| `zigbee_sleepy_climate_validation.py` | Temperature/humidity/power reporting and sleep |
| `zigbee_sleepy_ha_mqtt_validation.py` | Zigbee2MQTT join/interview/entity traffic over MQTT |
| `zigbee_sleepy_ha_validation.py` | Home Assistant API join/entity validation |

Before another session runs them, fix their old absolute defaults. Several
still reference `/home/lolren/Desktop/Nrf54L15`, which no longer exists as the
active repository. Derive the repository from `Path(__file__).resolve()` and
default output under this repository's `.build/`.

Python syntax validation:

```bash
python3 -m py_compile scripts/zigbee_*.py
```

Hardware tests require `pyserial`, `arduino-cli`, and the relevant connected
boards. MQTT tests also require `mosquitto_pub` and `mosquitto_sub`.

## Minimum Regression Matrix After Every Major Change

1. Compile all Zigbee examples.
2. Raw 802.15.4 ping/pong.
3. Coordinator plus router join.
4. End-device join and transport key.
5. Secure rejoin after reset.
6. On/Off, Level, Color HS, and Color Temperature commands.
7. Attribute read/write/discover.
8. Configure reporting and report reception.
9. Binding and group delivery.
10. Sleepy child data poll and indirect delivery.
11. Leave without rejoin.
12. Leave with rejoin.
13. Zigbee2MQTT interview.
14. Home Assistant entity creation and control.
15. Reset and power-loss persistence.
16. Invalid MIC and replay rejection.
17. Frame-counter monotonicity.
18. Idle and active power comparison.
19. Thread raw-radio regression, because Thread and Zigbee share the radio HAL.

## Definition Of Full Support

Do not tick "full Zigbee" until all of these are true:

- Coordinator, router, and end-device roles are maintained by one production
  runtime rather than example-specific loops.
- Multi-hop routing, repair, broadcasts, many-to-one, and source routing work.
- APS ACK/retry, binding, groups, and fragmentation work.
- Zigbee 3.0 commissioning and security survive reset and power loss.
- Required ZDO and ZCL foundation behavior is implemented.
- The selected production clusters are complete and interoperable.
- OTA is safe, resumable, and validated.
- Sleepy devices poll and receive indirect traffic reliably.
- Home Assistant/Zigbee2MQTT tests pass repeatedly.
- Mixed-vendor and RF-loss soak tests pass.
- Power is measured against an equivalent Nordic reference.
- Certification is either completed or the README clearly states that the
  stack is interoperable but not certified.

## First Task For The Next Zigbee Session

Start with Phase Z0 and Phase Z1, not another cluster:

1. Make all validation scripts path-independent.
2. Add all Zigbee examples to the compile matrix.
3. Add native codec/security tests.
4. Split `zigbee_stack.cpp` by layer without behavior changes.
5. Capture a complete current baseline against the local Zigbee2MQTT instance.
6. Then implement the MAC transaction engine and NWK routing.

This order gives later ZCL and OTA work a reliable transport instead of
embedding more functionality in example-specific loops.
