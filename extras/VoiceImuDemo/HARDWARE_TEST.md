# Hardware Validation

Validated on 12 July 2026 with:

- XIAO nRF54LM20A Sense probe UID `3377B9D6`;
- Linux BlueZ central adapter `68:34:21:E5:84:DF`;
- custom core built from the same source tree; and
- high-priority 7.5-15 ms connection interval, MTU 247, and DLE path.

## Timed Sleep And Discovery

Serial output across cold resets:

```text
VoiceImuPeripheral 1.0
Advertising as XIAO Sense Voice
No listener; entering timed SystemOFF
VoiceImuPeripheral 1.0
Advertising as XIAO Sense Voice
No listener; entering timed SystemOFF
```

Three consecutive wake windows were observed. BlueZ discovered the board at
RSSI values between approximately -44 and -53 dBm with the expected advertised
service UUID. After a stream client disconnected, the board returned to the
same timed SystemOFF cycle.

A second central deliberately connected without enabling notifications or
writing Control. After 15 seconds the board printed:

```text
Idle central; disconnecting for low power
No listener; entering timed SystemOFF
VoiceImuPeripheral 1.0
Advertising as XIAO Sense Voice
```

The central observed a remote disconnect and the board resumed its cold-wake
cycle, so an incomplete phone negotiation cannot hold the radio awake forever.
The same result was verified after writing Control mask `3` without enabling
the Audio or IMU CCCDs: the effective mask stayed zero, the sensor rail stayed
off, and the board remotely disconnected the central after 15 seconds.

## GATT And Stream

The host waited through SystemOFF, connected during the next wake, negotiated
MTU 247, enabled all three CCCDs, and wrote Control mask `3`. The host did not
request a faster connection interval; the peripheral's preferred-parameter
request moved the link into its 6-12 unit range. An eight-second run produced:

```text
audio packets: 402
audio sequence: 0..401
audio sequence gaps: 0
audio packet length: 174
IMU packets: 79
IMU sequence: 0..78
IMU sequence gaps: 0
IMU packet length: 20
firmware audio drops: 0
firmware IMU drops: 0
firmware error: 0
```

A final four-second smoke run on the exact flashed binary delivered 196 audio
and 38 IMU notifications with zero sequence gaps, zero drops, and error zero.

Audio predictors varied from -290 through 300 without stimulating the
microphone deliberately. All six raw IMU axes changed and the accelerometer Z
axis remained near 1 g at rest, confirming that notifications carried live
sensor data rather than constants.

The same test first exposed and then verified a Bluefruit 128-bit server UUID
byte-order correction: advertised and discovered GATT UUIDs now match exactly.

## Android Artifact

- Unit tests: passed.
- Debug build: passed without compiler warnings.
- Package: `com.lolren.sensevoiceimu.debug`.
- Minimum SDK: 26.
- Target SDK: 34.
- APK Signature Scheme v2: verified.

No Android device was visible through `adb` during this build, so installation
and speaker playback still require a phone-side acceptance test. The complete
GATT exchange and sensor stream were exercised with the Linux central.
