# BLE Implementation and Qualification Resume Notes

This file tracks the clean core's implemented BLE surface, regression risks,
and release validation. An implementation check mark means the feature exists
within the documented single-link scope; it does not mean complete Bluetooth
Core conformance, Bluetooth SIG qualification, or product qualification.

## Current Implemented Scope

- [x] Legacy advertising: connectable, scannable, non-connectable, directed
- [x] Active/passive scanning and filtered scan callbacks
- [x] Peripheral and central connections on the clean BLE path
- [x] ATT/GATT server and client through the Bluefruit-compatible API
- [x] NUS/BLEUart receive and notify/write paths
- [x] Bluefruit default central/peripheral link verified at MTU 23 / Data
      Length 27, including central connect callback, CCCD enable, and notify
      delivery on two nRF54 boards
- [x] PHY selection and update APIs: 1M, 2M, Coded S2/S8
- [x] Data Length Extension up to 251 bytes when requested
- [x] ATT MTU exchange up to 247 bytes when requested
- [x] LE Secure Connections Just Works fresh pair, bond save/load, encrypted
      reconnect, encrypted notifications, and encrypted writes on two boards
- [x] LE Secure Connections fixed-PIN and Numeric Comparison on the raw HAL
      `BlePairPeripheral` / `BlePairCentral` pair, including asynchronous user
      acceptance/rejection, fresh pairing, bond save/load, encrypted post-pair
      GATT traffic, and reconnect without another prompt
- [x] Bluefruit asynchronous Numeric Comparison through
      `getPendingPairingPasskey()` / `replyPendingPairingPasskey()`, with the
      phone-facing `Security > pairing_numeric_comparison` example
- [x] LE Secure Connections OOB through HAL and Bluefruit APIs in mutual,
      peripheral-to-central one-way, and central-to-peripheral one-way modes.
      Mutual OOB is authenticated; one-way OOB encrypts but remains
      conservatively unauthenticated in the public state.
- [x] Bluefruit security state queries: `BLEConnection::authenticated()`,
      `Bluefruit.Security.isEncrypted()`, and
      `Bluefruit.Security.isAuthenticated()` expose the HAL authenticated-link
      state used by MITM-protected GATT permissions
- [x] Stable local BLE identity: the active over-the-air RPA is kept separate
      from the identity address and the local IRK is derived from the factory
      identity root with `d1(IR, 1, 0)`
- [x] Privacy/RPA primitives: HAL and Bluefruit APIs can generate a resolvable
      private address, set it as the active random address, restore the stable
      identity, and resolve RPAs with the hardware AAR block
- [x] Opt-in local RPA rotation: HAL and Bluefruit APIs can rotate the local
      RPA on a sketch-selected interval before advertising, active scanning,
      or connection initiation while disconnected
- [x] Application-managed privacy resolving list: `Bluefruit.Security` can
      store up to eight peer IRKs and resolve scanned RPAs against that list
      using the hardware AAR path
- [x] Opt-in bonded peer resolving: `Bluefruit.Security` can keep the stored
      bonded peer IRK seeded into the application resolving list after boot and
      after successful pairing with `setBondedPeerResolvingEnabled(true)`.
- [x] SMP phase-3 identity-key distribution in role-correct order: both roles
      exchange Identity Information and Identity Address Information, persist
      the peer IRK/identity only after the distribution completes, and retain
      the stable local identity rather than the active RPA
- [x] Privacy-aware bonded reconnect: after reset each board can use a different
      RPA, resolve its retained peer through AAR, select the saved bond by
      identity, and re-enable encryption without starting a fresh pairing
- [x] Bond identity diagnostics: `Bluefruit.Security` can expose the stored
      peer address, peer identity address, peer IRK, authenticated bond flag,
      and add the bonded peer IRK into the application-managed resolving list
- [x] Negotiated SMP encryption-key sizes from 7 through 16 octets are
      validated; derived, received, persisted, and restored encryption keys
      are reduced to the negotiated size before use
- [x] SMP transactions use the Bluetooth 30-second transaction timer. Expiry
      retires pairing state and blocks further SMP on that physical link until
      reconnect; a single identity-aware peer slot applies exponential
      repeated-attempt delays from 1 through 64 seconds with per-minute decay
- [x] BLE security nonces, OOB records, and LL encryption material use CRACEN
      entropy only. Failure zeroizes the requested material and aborts the
      security step; foreground prefetch keeps normal RNG acquisition away
      from radio response deadlines
- [x] Custom ATT/GATT long-read path: custom characteristic values are read
      directly from bounded storage instead of the fixed 31-byte scratch buffer,
      and writable custom values support contiguous queued ATT Prepare Write /
      Execute Write up to `BleRadio::kCustomGattMaxValueLength`.
- [x] ATT Read Multiple Variable (`0x20/0x21`): the server returns
      little-endian Value Length plus Attribute Value tuples for variable or
      unknown-length multi-handle reads, preserves request handle order, reports
      the first failing handle, and truncates only after the ATT MTU boundary.
- [x] Bluefruit custom characteristic permissions: `BLECharacteristic::setPermission()`
      is propagated to the HAL for custom GATT value reads/writes, including
      encrypted-link and authenticated/MITM access checks for secured
      characteristics. Authenticated bond metadata is retained for reconnects.
- [x] Custom GATT standard descriptors: Bluefruit
      `BLECharacteristic::setUserDescriptor()`,
      `setPresentationFormatDescriptor()`, and `setReportRefDescriptor()` now
      allocate and expose `0x2901`, `0x2904`, and `0x2908` descriptors
      through ATT discovery, Read By Type, Read, and Read Blob. User
      Description (`0x2901`) descriptors can be written and read back when the
      parent characteristic write permission allows it; Presentation Format
      (`0x2904`) and Report Reference (`0x2908`) remain read-only.
- [x] `Bluefruit52Lib > Diagnostics > gatt_edge_cases` exposes one manual
      regression sketch for 244-byte long reads, queued prepare/execute writes,
      MTU-sensitive reads, writable User Description descriptors, and readable
      Presentation Format / Report Reference descriptors.
- [x] Bonded CCCD persistence: CCCD writes are saved in the BLE bond storage
      page, restored only for the matching bonded peer/local identity, and
      cleared with the bond record. This covers Service Changed, Battery
      Level, and custom notify/indicate characteristics.
- [x] Basic Bluefruit HID peripheral plumbing: `BLEHidAdafruit` and
      `BLEHidGamepad` now create HID Information, Report Map, Protocol Mode,
      Control Point, Report Reference descriptors, keyboard/mouse boot
      reports, and notify keyboard, mouse, consumer-control, and gamepad
      reports instead of only returning `connected()`. Host Protocol Mode
      writes now update the active report/boot path and are visible through
      `BLEHidAdafruit::protocolMode()`, `isBootProtocolMode()`, and
      `setProtocolModeCallback()`. Host keyboard LED output reports now update
      `keyboardLedState()` before the optional `setKeyboardLedCallback()`
      callback fires, and central HID sketches can write Boot Keyboard Output
      LEDs through `BLEClientHidAdafruit::setKeyboardLedState()`.
- [x] Bluefruit disconnect reason helpers: central/peripheral disconnect
      callbacks now receive common HCI-style reason codes, and sketches can
      inspect the last disconnect with `Bluefruit.getLastDisconnectReason()`
      plus a readable `Bluefruit.disconnectReasonName()`.
- [x] Low-power BLE advertising current is now close to the Zephyr reference
      for the msfujino AdvCurrent test from discussion #71

## Regression-Sensitive Areas

- [ ] Plain non-secure central/peripheral must remain independent from the LE
      Secure Connections path. Issue #68 showed this can regress when security
      changes leak into the unencrypted link setup.
- [ ] Central setup must stay foreground-pumped until the deferred central
      connect callback has run and any active central sync procedure has
      completed. Letting the background connection service take over too early
      can stall the default 23/27 link before CCCD or service discovery
      completes.
- [ ] Default Bluefruit MTU/Data Length behavior must stay Bluefruit-compatible:
      if a sketch does not request a larger value, the user-visible default
      should remain MTU 23 and Data Length 27. Issue #68 covers this.
- [ ] BLEUart/NUS web-device CLI bridge must keep both RX and TX working.
- [ ] Serial and HardwareSerial are not part of BLE but have repeatedly been
      broken by timing changes; do not touch UART code while resuming BLE unless
      the BLE test proves UART is involved.
- [ ] Low-power advertising must not keep RF_SW, HFXO, VPR, Thread, Matter, or
      Zigbee active while idle unless the selected sketch/profile explicitly
      needs them.

## Remaining BLE Compliance Work

- [ ] Broader authenticated-pairing interoperability: passkey, Numeric
      Comparison, and NFC/QR OOB workflows against phone/desktop peers, plus a
      larger malformed/timeout/retry matrix
- [ ] Signing/CSRK distribution and signed writes, locally generated legacy
      LTK/EDIV/Rand distribution, a multi-peer repeated-attempt policy, and a
      multi-peer bond database
- [ ] Automatic controller-enforced resolving-list/allow-list policy and
      privacy/bond-database behavior against a broad phone/desktop matrix
- [ ] Formal ATT/GATT edge cases: host-app interop for long read/write and
      prepare/execute write, descriptor host-app interop, and broader
      error-code coverage
- [ ] HID host interop: phone/desktop pairing behavior, OS report parsing,
      boot-protocol switching against real hosts, and gamepad host behavior
      still need real-host validation.
- [ ] LL control procedure collision handling under hostile/interleaved LL
      procedure timing
- [ ] Multi-link stress: simultaneous central/peripheral with mixed MTU/DLE/PHY
      settings
- [ ] Extended advertising and scan-response interoperability beyond local
      smoke tests
- [ ] Connection power profile work; current low-power focus has been
      advertising, not long connected-idle soaks
- [ ] Bluetooth PTS/BQB-style test matrix. The core is not a qualified
      controller.

## Automated Two-Board Gate

Run the complete release-candidate gate described in
[`TWO_BOARD_RELEASE_GATE.md`](TWO_BOARD_RELEASE_GATE.md):

```bash
python3 scripts/run_two_board_release_gate.py --profile full
```

The security/privacy portion requires positive Numeric Comparison, responder
rejection without encryption or bond save, mutual OOB, both one-way OOB
directions, valid timed RPA rotation, identity-key distribution, and an
identity-resolved encrypted reconnect without re-pairing. Retain the generated
`measurements/two_board_release_gate_<timestamp>/` directory with the release
evidence. A passing gate is hardware regression evidence for these two board
families, not Bluetooth PTS/BQB qualification or broad host interoperability.

## Resume Test Matrix

Run these before changing BLE again:

- [ ] Compile `Bluefruit52Lib` BLEUART peripheral and central examples
- [ ] Compile `Bluefruit52Lib` HID keyboard, mouse, keyscan, camera shutter,
      and gamepad examples
- [ ] Compile and run the non-secure central/peripheral MTU/DLE pair at:
      MTU 23/Data Length 27, MTU 128/Data Length 132, MTU 247/Data Length 251
- [ ] Compile and run `BlePairPeripheral` + `BlePairCentral` fresh pair
- [ ] Compile and run `BlePairPeripheral` + `BlePairCentral` with
      `-DBLE_PAIR_USE_STATIC_PIN=1`
- [ ] Compile and run `BlePairPeripheral` + `BlePairCentral` with
      `-DBLE_PAIR_USE_NUMERIC_COMPARISON=1` and confirm both boards log the
      same six-digit value before encrypted traffic resumes
- [ ] Repeat Numeric Comparison with one responder rejecting; confirm pairing
      fails, encryption stays off, and neither board saves a bond
- [ ] Compile and run `BleOobPairPeripheral` + `BleOobPairCentral` with
      `BLE_OOB_MODE=0`, `1`, and `2`; exchange only the records requested by
      each mode and confirm encrypted UART traffic. Require authenticated state
      for mutual OOB and unauthenticated state for both one-way modes.
- [ ] Compile and run `BleResolvablePrivateAddress`; confirm `result=PASS`,
      the printed RPA resolves directly and through `resolving_list_match=yes`
      at index `0`, phones still see the `X54-RPA` advertiser, and the
      sketch-selected RPA rotation interval does not interrupt an active
      connection
- [ ] Compile and run `BlePairPeripheral` + `BlePairCentral` with
      `-DBLE_PAIR_USE_PRIVACY=1`; verify both identity PDUs, retained identity
      and IRK, changed RPAs after reset, successful AAR resolution, and an
      encrypted reconnect with no new prompt or bond save
- [ ] Compile and run `Bluefruit52Lib > Diagnostics > bond_identity_probe`;
      pair with a phone/desktop host, confirm authenticated fixed-PIN pairing,
      peer bond address logging, and `bonded_peer_resolver_refreshed=yes` when
      the host distributes an IRK
- [ ] Compile and run `Bluefruit52Lib > Diagnostics > gatt_edge_cases`;
      request MTU 23 and MTU 247, read the 244-byte characteristic, write a
      244-byte value through prepare/execute write, read it back, and verify
      the writable `0x2901` descriptor round-trips.
- [ ] Reboot both boards and confirm bonded encrypted reconnect without clearing
      storage
- [ ] Run BLEUart/NUS against Makerdiary Web Device CLI and verify RX and TX
- [ ] Run AdvCurrent in `PowerProfile: WFI`, VPR off, Thread off, Matter off,
      Zigbee off, and compare against the Zephyr reference
- [ ] Repeat one compile/install test from a freshly installed board package,
      not only from the local source tree

## Current Power Finding From Discussion #71

The latest reporter result on May 17, 2026 says the large current spikes were
caused by a loose PPK2/battery-pad connection. With that fixed, v0.7.11 is at
roughly the same level as Zephyr, but the Arduino path still had avoidable
charge before each foreground connectable/scannable advertising event.

Root cause found in the Arduino path:

- Bluefruit foreground advertising calls `advertiseInteractEvent()`.
- `advertiseInteractEvent()` applies the BLE random advertising delay before
  starting the radio event.
- Bluefruit did not move `next_adv_due_us_` into the future until after the
  radio event returned.
- During the random delay, `delay()` saw advertising as overdue and refused WFI,
  so the CPU could busy-spin for up to the random advertising delay before TX.

Fix applied:

- Bluefruit foreground advertising now applies the BLE random advertising delay
  in the scheduler and calls the radio without the extra internal random-delay
  sleep. This preserves random spacing between advertising events but removes
  the visible pre-TX CPU plateau.

Do not remove the random advertising delay to chase current. It is part of BLE
advertising timing behavior. The correct fix is to sleep through it.
