# XIAO Sense Voice + IMU Android Demo

This folder contains a complete low-power microphone and motion streaming demo
for the **XIAO nRF54LM20A Sense**:

- a board sketch with continuous, gap-free PDM DMA capture;
- a native Android application with persistent listen mode;
- a directly installable, signed debug APK;
- the complete GATT and packet protocol; and
- hardware validation results from a real LM20A Sense board.

The board is a BLE peripheral. When nobody is listening it wakes on a timed
SystemOFF reset, advertises briefly, and returns to SystemOFF. The Android app
is the BLE central: **Connect** places it in listen mode, so it catches the next
board wake, connects, enables notifications, and explicitly requests the
streams. The MIC/IMU power rail remains off until that request arrives.

## Folder Layout

| Path | Contents |
|---|---|
| `firmware/VoiceImuPeripheral/` | Arduino firmware sketch |
| `android/` | Complete Kotlin/Gradle Android project |
| `apk/sense-voice-imu-1.0.0-debug.apk` | Installable Android APK |
| `PROTOCOL.md` | UUIDs, control command, and packet formats |
| `HARDWARE_TEST.md` | Measured two-sided validation results |

## Install And Run

1. Flash `firmware/VoiceImuPeripheral/VoiceImuPeripheral.ino` to the XIAO
   nRF54LM20A Sense using the `xiao_nrf54lm20b` board target, BLE enabled, and
   the 128 MHz CPU profile.
2. Install `apk/sense-voice-imu-1.0.0-debug.apk` on Android 8.0 or newer.
3. Open **Sense Voice + IMU** and grant the nearby-device permission.
4. Press **Connect**. The app remains in listen mode until the board's next
   advertising wake, then connects and starts both streams.
5. **Mute** stops microphone transmission while leaving the 10 Hz IMU stream
   active. **Disconnect** releases the BLE link; the board powers the sensors
   down and resumes its timed SystemOFF cycle.

Install from a development machine with:

```bash
adb install -r apk/sense-voice-imu-1.0.0-debug.apk
```

The debug APK is locally signed for direct installation. It is not a Play
Store release artifact.

## Board Power Behavior

The default firmware constants are:

```cpp
constexpr uint32_t kWakePeriodMs = 10000U;
constexpr uint32_t kAdvertisingWindowMs = 2500U;
constexpr uint32_t kSystemOffSleepMs = kWakePeriodMs - kAdvertisingWindowMs;
```

With no listener, each cycle is approximately 2.5 seconds of connectable
advertising followed by 7.5 seconds of no-retention SystemOFF. Boot time adds a
small amount to the nominal ten-second cadence. A longer `kWakePeriodMs`
increases battery life and worst-case connection latency. A shorter
`kAdvertisingWindowMs` saves more energy but makes discovery less tolerant of
phone scan scheduling.

When connected but no stream is requested, the sensor rail remains off. A
valid control request enables nPM1300 LDO1, initializes the LSM6DS3TR-C, and
starts continuous PDM only when audio is enabled. A Stop request or disconnect
stops PDM, puts the IMU in power-down, ends `Wire1`, and disables LDO1.
If a central connects but does not complete setup or request a stream within 15
seconds, the firmware disconnects it and returns to the SystemOFF cycle. The
Android app also times out failed GATT operations and resumes listen mode.

The Android listen loop is designed for the app in the foreground. Android may
throttle BLE scans after the app is backgrounded or the screen has been off for
a long time. A production always-listening application should move scanning
into a user-visible foreground service and account for the target phone's
battery policy.

## Stream Profile

- Audio capture: 16 kHz, mono, signed 16-bit PDM output.
- Audio transport: independent 20 ms IMA-ADPCM frames, 174 bytes each.
- Audio notification rate: 50 Hz.
- IMU: raw +/-2 g accelerometer and +/-245 dps gyroscope, scaled in Android.
- IMU notification rate: 10 Hz.
- Link setup: 2M PHY, ATT MTU 247, DLE 251, and a peripheral-requested
  7.5-15 ms interval.
- Playback: Android `AudioTrack`, PCM16 mono at 16 kHz, bounded jitter queue.

Ten-hertz IMU was selected deliberately. The clean controller currently sends
one notification per connection event; at a common 15 ms phone interval the
link budget is about 66 notifications per second. Fifty audio plus ten IMU
notifications fit without queue loss. The firmware and Android app both request
the faster interval, and the peripheral request was also verified with an
otherwise untuned Linux central.

This is a custom GATT voice transport, not Bluetooth LE Audio, a headset
profile, or an Android microphone source.

## Build From Source

The checked-in wrapper uses Gradle 8.7, Android Gradle Plugin 8.5.2, Kotlin
1.9.24, compile/target SDK 34, and minimum SDK 26.

```bash
cd android
printf 'sdk.dir=%s\n' "$HOME/Android/Sdk" > local.properties
./gradlew lintDebug testDebugUnitTest assembleDebug
```

`assembleDebug` also copies the versioned APK to `../apk/` and regenerates its
`SHA256SUMS.txt` entry.

## Security Boundary

The demo characteristics intentionally use open GATT permissions so initial
bring-up does not require bonding. Anyone nearby who connects during the brief
advertising window could subscribe to the microphone or IMU. A product should
require encrypted/authenticated characteristic permissions, bonding, an
application authorization policy, and a user-visible capture indicator.
