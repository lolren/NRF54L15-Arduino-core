# Third-Party Notices

The platform [MIT license](LICENSE) applies to project-owned contributions
unless a file or component states different terms. File-local copyright and
license notices take precedence for imported or adapted material.

Major bundled components include:

| Component | License or notice |
|---|---|
| Adafruit Bluefruit52Lib sources and examples, except where a file states different terms | [MIT](libraries/Bluefruit52Lib/LICENSE) |
| Signove IEEE 11073 helper in the Bluefruit custom HTM example | File-local LGPL-2.1-or-later notice and [license text](libraries/Bluefruit52Lib/examples/Services/custom_htm/LICENSE) |
| Adafruit SPIFlash-derived compatibility API and source | [MIT](libraries/Adafruit_SPIFlash/LICENSE) |
| TinyUSB-derived HID compatibility definitions | [MIT](LICENSES/TinyUSB-MIT.txt) and retained file-local attribution |
| Arduino-derived core and example files | File-local Apache-2.0, LGPL-2.1-or-later, or MIT notices; full Apache/LGPL texts are in [LICENSES](LICENSES) |
| Arm CMSIS-Core-derived compatibility headers and system templates | File-local [Apache-2.0](LICENSES/Apache-2.0.txt) notices and required modification notices |
| Espressif Arduino Preferences API | File-local [Apache-2.0](LICENSES/Apache-2.0.txt) notices; the storage backend is project-owned nRF54 RRAM code |
| Arduino/ESP EEPROM compatibility API | File-local [LGPL-2.1-or-later](LICENSES/LGPL-2.1-or-later.txt) attribution; the storage backend is project-owned nRF54 RRAM code |
| Adafruit SoftwareTimer and debug compatibility helpers | File-local BSD-3-Clause or MIT notices with retained attribution and modification notices |
| Nordic Semiconductor headers and support code | File-local BSD-3-Clause or Apache-2.0 notices |
| Nordic SDC/MPSL binary components | [Nordic 5-Clause license](libraries/Nrf54L15-Clean-Implementation/third_party/nordic_sdc/LICENSE), [attribution](libraries/Nrf54L15-Clean-Implementation/third_party/nordic_sdc/LICENSE-ATTRIBUTION.txt), and [version record](libraries/Nrf54L15-Clean-Implementation/third_party/nordic_sdc/VERSION) |
| OpenThread public headers | [BSD-3-Clause](libraries/Nrf54L15-Clean-Implementation/src/openthread-LICENSE.txt) and [notice](libraries/Nrf54L15-Clean-Implementation/src/openthread-NOTICE.txt) |
| Bundled OpenThread core | [BSD-3-Clause](libraries/Nrf54L15-Clean-Implementation/third_party/openthread-core/openthread-LICENSE.txt) and [notice](libraries/Nrf54L15-Clean-Implementation/third_party/openthread-core/openthread-NOTICE.txt) |
| ConnectedHomeIP scaffold | [Apache-2.0](libraries/Nrf54L15-Clean-Implementation/third_party/connectedhomeip/connectedhomeip-LICENSE.txt) and mandatory [Matter SDK notice](libraries/Nrf54L15-Clean-Implementation/third_party/connectedhomeip/connectedhomeip-NOTICE.txt) |
| Mbed TLS within the OpenThread source set | [Apache-2.0](libraries/Nrf54L15-Clean-Implementation/third_party/openthread-core/third_party/mbedtls/repo/LICENSE) |
| Reduced AES block primitive adapted from tiny-AES-c | [Unlicense](LICENSES/Unlicense.txt) |
| Microsoft UF2 conversion utility | [MIT](tools/uf2/LICENSE.txt) |
| nRF OCD native uploader | Project-owned portions use [MIT](tools/nrf_ocd-compliance/nrf_ocd-MIT.txt), identified pyOCD-derived portions use Apache-2.0, and statically linked libusb 1.0.27 uses LGPL-2.1-or-later; see the [third-party notice](tools/nrf_ocd-compliance/nrf_ocd-THIRD_PARTY_NOTICES.md) and [corresponding source/relink guide](tools/nrf_ocd-compliance/README.md) |
| Legacy recovery host-tools bootstrap | Project-owned files use its [MIT license](tools/nrf54l15hosttools/1.1.3/LICENSE); Python dependencies are installed separately from the user's configured package index and are not redistributed in the platform |

This notice highlights the principal imported components; it is not a
substitute for the notices retained in individual files and subdirectories.
