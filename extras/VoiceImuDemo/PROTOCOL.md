# Voice + IMU GATT Protocol v1

All multibyte integers are little-endian. UUIDs share this base:

`5f24c0xx-7e2b-4b8c-ae59-2d7618b9d1a0`

| `xx` | Characteristic | Properties |
|---|---|---|
| `01` | Service | Primary service |
| `02` | Capabilities | Read, fixed 16 bytes |
| `03` | Control | Write with response, fixed 4 bytes |
| `04` | Status | Read + Notify, fixed 12 bytes |
| `05` | Audio | Notify, fixed 174 bytes |
| `06` | IMU | Notify, fixed 20 bytes |

## Control

| Offset | Type | Meaning |
|---:|---|---|
| 0 | `u8` | Protocol version (`1`) |
| 1 | `u8` | Stream mask: bit 0 audio, bit 1 IMU |
| 2 | `u16` | Client transaction number |

Mask `0` stops both streams and powers the sensors down. Mask `2` keeps only
IMU active. Mask `3` starts audio and IMU. The firmware accepts a stream bit
only after that characteristic's CCCD has notifications enabled; the effective
mask is returned in Status. A write without a matching subscription therefore
cannot power the sensor rail indefinitely.

## Capabilities

| Offset | Type | Meaning |
|---:|---|---|
| 0 | 4 bytes | ASCII `SVIM` |
| 4 | `u8` | Protocol version |
| 5 | `u8` | Codec (`1` = IMA-ADPCM) |
| 6 | `u16` | Audio sample rate (16000) |
| 8 | `u16` | Samples per audio frame (320) |
| 10 | `u16` | IMU rate (10 Hz) |
| 12 | `u16` | Audio packet length (174) |
| 14 | `u16` | IMU packet length (20) |

## Audio Notification

Each packet is independently decodable, so losing one packet damages only 20
ms of audio.

| Offset | Type | Meaning |
|---:|---|---|
| 0 | `u8` | Protocol version |
| 1 | `u8` | Flags; bit 0 marks a discontinuity |
| 2 | `u16` | Frame sequence |
| 4 | `u32` | Counter of the first source sample |
| 8 | `u16` | Decoded sample count (320) |
| 10 | `i16` | Initial ADPCM predictor / first PCM sample |
| 12 | `u8` | Initial IMA step-table index (0-88) |
| 13 | `u8` | Reserved |
| 14 | 160 bytes | 319 ADPCM nibbles, low nibble first; last high nibble is padding |

The decoder emits the predictor as sample zero, then applies the standard
89-entry IMA step table and 16-entry index table for samples 1 through 319.

## IMU Notification

| Offset | Type | Meaning |
|---:|---|---|
| 0 | `u8` | Protocol version |
| 1 | `u8` | Flags, currently zero |
| 2 | `u16` | IMU sequence |
| 4 | `u32` | Most recent audio sample counter |
| 8 | `i16` | Accelerometer X |
| 10 | `i16` | Accelerometer Y |
| 12 | `i16` | Accelerometer Z |
| 14 | `i16` | Gyroscope X |
| 16 | `i16` | Gyroscope Y |
| 18 | `i16` | Gyroscope Z |

Scale accelerometer values by `0.000061 g/LSB` and gyroscope values by
`0.00875 dps/LSB`.

## Status Notification

| Offset | Type | Meaning |
|---:|---|---|
| 0 | `u8` | Protocol version |
| 1 | `u8` | State: bit 0 connected, bit 1 PDM streaming, bit 2 IMU ready |
| 2 | `u16` | Last accepted control transaction |
| 4 | `u16` | Saturating audio notification drop count |
| 6 | `u16` | Saturating IMU notification drop count |
| 8 | `u8` | Effective stream mask |
| 9 | `u8` | Codec |
| 10 | `u8` | Firmware error code; zero means no error |
| 11 | `u8` | Reserved |

## Client Sequence

1. Scan for the service UUID or local name `XIAO Sense Voice`.
2. Connect and discover services.
3. Request ATT MTU 247 and high connection priority.
4. Enable Status, Audio, and IMU notifications serially, waiting for every
   CCCD descriptor-write callback.
5. Write Control mask `3` with response.
6. On Stop, write mask `0` before disconnecting when possible.
