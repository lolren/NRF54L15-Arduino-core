## Bluefruit52Lib Compatibility Layer

`Bluefruit52Lib` is the bundled nRF52/nRF52840 compatibility library for the
supported nRF54L clean-core boards.

It keeps the familiar Bluefruit-style API available on top of the clean nRF54
HAL, including `Bluefruit`, `BLEUart`, `BLEClientUart`, scanner/client
helpers, Device Information, Battery Service, HID, ANCS, security/privacy, and
common advertising helpers.

Examples in Arduino IDE:

- `File -> Examples -> Bluefruit52Lib -> Advertising`
- `File -> Examples -> Bluefruit52Lib -> Central`
- `File -> Examples -> Bluefruit52Lib -> Diagnostics`
- `File -> Examples -> Bluefruit52Lib -> DualRoles`
- `File -> Examples -> Bluefruit52Lib -> HID`
- `File -> Examples -> Bluefruit52Lib -> nRF52Compat`
- `File -> Examples -> Bluefruit52Lib -> Peripheral`
- `File -> Examples -> Bluefruit52Lib -> Projects`
- `File -> Examples -> Bluefruit52Lib -> Security`
- `File -> Examples -> Bluefruit52Lib -> Services`

`nRF52Compat` is the starter pack for direct sketch-porting examples:

- `central_bleuart`
- `central_scan`
- `central_notify`
- `dual_bleuart`
- `beacon`
- `custom_hrm`
- `notify_peripheral`
- `pairing_pin`

These are unchanged upstream-style sketches included locally because they are
known to compile on the nRF54 wrapper and give users concrete migration
starting points.

For the simplest custom notification flow, use `notify_peripheral` together
with `central_notify`.

BLE PHY requests are available through the nRF52840-style connection object:

```cpp
void connect_callback(uint16_t conn_handle) {
  BLEConnection* conn = Bluefruit.Connection(conn_handle);
  conn->requestPHY(BLE_GAP_PHY_2MBPS);  // 1: 1M, 2: 2M, 4: coded
  uint8_t phy = conn->getPHY();
}
```

`requestPHY()` can be called directly from the connect callback. The
compatibility layer queues it safely after the callback returns.

Authenticated pairing state is also exposed for security tests:

```cpp
void connection_secured_callback(uint16_t conn_handle) {
  BLEConnection* conn = Bluefruit.Connection(conn_handle);
  bool encrypted = Bluefruit.Security.isEncrypted(conn_handle);
  bool authenticated = (conn != nullptr) && conn->authenticated();
}
```

`authenticated()` is only true after an encrypted MITM-capable link, such as
fixed-PIN, numeric-comparison, or mutual LE Secure Connections OOB pairing.
Plain encrypted Just Works and one-way OOB links remain `secured()` but not
authenticated.

### Numeric Comparison

Numeric Comparison uses an asynchronous poll/reply API so the link continues to
service connection events while a user decides whether the displayed values
match:

```cpp
uint8_t passkey[6] = {};
bool matchRequest = false;
if (Bluefruit.Security.getPendingPairingPasskey(passkey, &matchRequest) &&
    matchRequest) {
  // Display all six ASCII digits, then reply from the user-input path.
  Bluefruit.Security.replyPendingPairingPasskey(userAccepted);
}
```

Use `Security > pairing_numeric_comparison` for a phone-facing peripheral that
accepts `yes` or `no` over Serial. A rejected comparison stops the security
procedure before link encryption and does not save a bond.

### LE Secure Connections OOB

The OOB API generates the local LE Secure Connections `r/c` record and accepts
the peer record delivered through an application-owned channel such as NFC or a
QR-code workflow:

```cpp
uint8_t localR[16] = {};
uint8_t localC[16] = {};
uint8_t peerR[16] = {};
uint8_t peerC[16] = {};

Bluefruit.Security.generateOobData(localR, localC);
Bluefruit.Security.setOobRemoteData(peerR, peerC);
Bluefruit.Security.setOobFlag(true);
```

The raw `BleOobPairPeripheral` and `BleOobPairCentral` examples support mutual
OOB (`BLE_OOB_MODE=0`), peripheral-to-central only (`1`), and
central-to-peripheral only (`2`). Mutual OOB is reported as authenticated.
One-way OOB completes encrypted pairing but is conservatively not reported as
mutually authenticated. OOB records are one-shot pairing material and are
cleared after completion, failure, or disconnect; generate and exchange fresh
records before another attempt. Legacy-TK OOB is not part of this API.

The underlying SMP engine validates negotiated 7-16 octet encryption-key
sizes, applies the 30-second transaction timeout and a bounded single-peer
repeated-attempt delay, and uses CRACEN entropy without a deterministic
security fallback. Entropy failure aborts the security step.

### Authenticated Signed Writes

Bonded peers can exchange CSRKs during SMP and use an ATT Signed Write Command
while the reconnect is not encrypted. On the server, expose the standard
authenticated-signed-write property and select a signed permission:

```cpp
BLECharacteristic signedValue(
    0xFFF1, CHR_PROPS_READ | CHR_PROPS_AUTH_SIGNED_WRITES);
signedValue.setPermission(SECMODE_OPEN, SECMODE_SIGNED_NO_MITM);
```

After the client discovers that characteristic, queue a signed command with:

```cpp
const uint8_t value[] = {0x01, 0x02, 0x03};
uint16_t written = peerCharacteristic.writeSigned(value, sizeof(value));
```

The clean HAL uses AES-CMAC with the retained local CSRK, persists the next
sender counter before making the command eligible for transmission, verifies
the peer signature in constant time, and rejects stale or replayed counters
before changing the GATT value. The current policy is deliberately bounded to
the single retained bond. `writeSigned()` returns zero when the characteristic
does not advertise the property, no signing bond exists, the link is encrypted,
or the value does not fit the negotiated ATT MTU.

For bonded peers, CCCD subscriptions are restored automatically when the saved
bond identity matches the reconnecting peer. This lets notify/indicate
characteristics keep the same Bluefruit callback behavior after a bonded
reconnect without requiring the central to rewrite every CCCD immediately.

Bond identity helpers are available for host privacy debugging:

```cpp
ble_gap_addr_t identity = {};
uint8_t peerIrk[16] = {};
bool hasIdentity = Bluefruit.Security.getBondPeerIdentityAddress(&identity);
bool hasIrk = Bluefruit.Security.getBondPeerIrk(peerIrk);
bool inResolver = Bluefruit.Security.addBondedPeerIrkToResolvingList();
```

If you want the stored bonded peer IRK kept in the resolving list automatically
after boot and after pairing completes:

```cpp
Bluefruit.Security.setBondedPeerResolvingEnabled(true);
```

The privacy helpers keep the stable local identity address distinct from the
active over-the-air RPA. The default local IRK is derived from the factory
identity root, and privacy can be enabled with an application-selected rotation
interval:

```cpp
ble_gap_addr_t identity = {};
uint8_t localIrk[16] = {};
Bluefruit.Security.getLocalIdentityAddress(&identity);
Bluefruit.Security.getLocalIdentityIrk(localIrk);
Bluefruit.Security.setPrivacyEnabled(true, 15UL * 60UL * 1000UL, true);
```

During bonding, the clean SMP path distributes and receives Identity
Information plus Identity Address Information in role-correct order. A retained
bond stores the peer IRK and identity address, allowing AAR resolution and an
encrypted reconnect after either board starts with a new RPA. The public
resolving list remains application-managed and holds up to eight IRKs; automatic
multi-bond controller policy and allow-list enforcement are outside this
compatibility layer's current scope.

Use `Diagnostics > bond_identity_probe` to pair with a phone or desktop host
and print the saved peer address, identity address, IRK presence, and
authenticated bond flag.

Use `Diagnostics > gatt_descriptor_helpers` to inspect custom GATT descriptor
helpers. It exposes readable `0x2901`, `0x2904`, and `0x2908` descriptors plus a
writable `0x2901` User Description descriptor that can be written and read back
from a BLE scanner.

Custom characteristic storage follows the configured length contract.
`setMaxLen()` bounds variable values, `setFixedLen()` requires that exact value
length, `setFixedLen(0)` returns to variable-length behavior, and `setBuffer()`
uses the supplied buffer size as the maximum. The HAL applies the same bounds to
local updates, normal peer writes, and prepare/execute writes. A service-level
`setPermission()` is inherited as the minimum security for its characteristics
and descriptors.

Client characteristic `read()` follows a full value through ATT Read and Read
Blob responses until the caller's buffer is full or the value ends. Paged
characteristic discovery also determines each characteristic's complete handle
range before searching for descriptors; this prevents a descriptor from being
assigned to the neighboring characteristic.

`BLEBas::write(level)` updates the readable Battery Level without implicitly
notifying. `BLEBas::notify(level)` is the explicit notification path, can resend
the same value, and returns false when there is no connected subscriber with the
Battery Level CCCD enabled.

Enable BLE UART TX coalescing with `bleuart.bufferTXD(true)`. Small `write()`
calls accumulate up to the current `ATT_MTU - 3` notification payload; a full
packet sends automatically, while `flushTXD()` sends a partial packet on demand.
If notification backpressure makes a flush fail, the unsent bytes remain
buffered for a later retry. Disabling buffered mode clears a pending partial
packet.

### Apple Notification Center Service

`BLEAncs` subscribes to both Notification Source and Data Source. Attribute
getters send the complete Control Point request, reassemble a Data Source
response that spans multiple notifications, validate its command/UID/attribute
prefix, and return the requested bytes. The implemented helpers cover app ID and
display name, title, subtitle, message, decimal message size, date, positive and
negative action labels, and the two notification actions. Start with
`Services > ancs`; ANCS still depends on iOS authorization, pairing, and the
phone exposing the service after connection.

HID Protocol Mode changes are visible to sketches. `BLEHidAdafruit` switches
keyboard/mouse notifications between Report and Boot characteristics when a host
writes Protocol Mode:

```cpp
blehid.setProtocolModeCallback([](uint16_t conn, uint8_t mode) {
  (void) conn;
  Serial.println(mode == BLE_HID_PROTOCOL_MODE_BOOT ? "Boot" : "Report");
});
uint8_t mode = blehid.protocolMode();
```

Keyboard LED output reports are also visible. Hosts write NumLock, CapsLock,
ScrollLock, Compose, and Kana bits to the HID output report or boot keyboard
output report. Sketches can use a callback or poll the last received state:

```cpp
blehid.setKeyboardLedCallback([](uint16_t conn, uint8_t leds) {
  (void) conn;
  Serial.print("Keyboard LEDs: 0x");
  Serial.println(leds, HEX);
});
uint8_t leds = blehid.keyboardLedState();
```

Central HID sketches can also drive a keyboard peripheral's Boot Keyboard
Output report after discovery:

```cpp
BLEClientHidAdafruit hid;
if (hid.keyboardPresent()) {
  hid.setKeyboardLedState(KEYBOARD_LED_CAPSLOCK);
  uint8_t lastSent = hid.keyboardLedState();
}
```

For generic `0x2A4D` HID reports, central discovery reads the `0x2908` Report
Reference descriptor instead of assuming the first matching UUID is a gamepad.
Keyboard, consumer-control, mouse, and gamepad notification subscription paths
all require an encrypted link.

Disconnect callbacks receive common HCI-style reason codes. The most recent
drop can also be inspected later:

```cpp
uint8_t reason = 0;
bool remote = false;
if (Bluefruit.getLastDisconnectReason(&reason, &remote)) {
  Serial.print(Bluefruit.disconnectReasonName(reason));
  Serial.println(remote ? " from peer" : " local/timeout");
}
```

The broader Bluefruit menus now ship the practical wrapper examples by role:

- `Advertising`: `adv_advanced`, `beacon`, `eddystone_url`
- `Central`: `central_bleuart_multi`, `central_custom_hrm`, `central_hid`, `central_pairing`, `central_scan_advanced`, `central_throughput`
- `Diagnostics`: `bond_identity_probe`, `gatt_descriptor_helpers`, `gatt_edge_cases`, `throughput`, `rssi_callback`, `rssi_poll`
- `DualRoles`: `dual_bleuart`
- `HID`: `blehid_keyboard`, `blehid_mouse`, `blehid_gamepad`, `blehid_camerashutter`
- `Projects`: `rssi_proximity_central`, `rssi_proximity_peripheral`
- `Security`: `pairing_numeric_comparison`, `pairing_passkey`, `pairing_pin`, `clearbonds`
- `Services`: `bleuart`, `bleuart_multi`, `custom_hrm`, `custom_htm`, `client_cts`, `ancs`

The supported surface is the shipped example set above plus the documented
security/privacy helpers. Common BLE UART, scanner, custom notify, central
discovery, LE Secure Connections, and privacy flows are validated by the nRF54
two-board gate and are the recommended starting point for nRF52 sketch ports.
This compatibility statement is not Bluetooth SIG qualification or a claim of
complete nRF52 Bluefruit behavioral parity.

### Known compatibility limits

Dynamic GATT read/write authorization callbacks are not implemented. The clean
HAL does not yet expose the request/reply transaction needed to let a callback
approve or reject an ATT operation before the value changes. If
`setReadAuthorizeCallback()` or `setWriteAuthorizeCallback()` is configured,
`BLECharacteristic::begin()` returns `ERROR_NOT_SUPPORTED` instead of silently
creating an unprotected characteristic. Use `setPermission()` for static GATT
access control.

Nordic secure DFU is not implemented by this core. `BLEDfu::begin()` returns
`ERROR_NOT_SUPPORTED`, and the bundled examples do not advertise a non-working
DFU service.

Directed advertising is not implemented. Service Changed is present as a GATT
characteristic and its bonded CCCD can be retained, but
`configServiceChanged()` does not track a database-change epoch or schedule
schema-range indications.

The compatibility layer keeps one retained bond. Automatic multi-bond policy,
controller-enforced allow-list behavior, locally generated full legacy
LTK/EDIV/Rand distribution, complete nRF52 API parity, and Bluetooth PTS/BQB
qualification are outside the documented `1.0.0` scope.
