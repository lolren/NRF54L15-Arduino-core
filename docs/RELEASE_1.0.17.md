# nRF54 Arduino Core 1.0.17

`1.0.17` adds the experimental J-Link upload option requested in
[issue #106](https://github.com/lolren/nrf54-arduino-core/issues/106).

## Experimental J-Link upload

- Add **Tools -> Upload Method -> J-Link via pyOCD (Experimental)** to every
  supported board, including the Nordic nRF54L15 DK.
- Restrict probe discovery to pyOCD's `jlink` plug-in so this method cannot
  silently select an attached CMSIS-DAP, ST-Link, or Picoprobe.
- Disable pyOCD's J-Link target-power control and keep reset within the same
  pyOCD J-Link session.
- Normalize the leading zeroes that USB descriptors can expose on J-Link serial
  numbers while retaining automatic selection when exactly one J-Link exists.
- Never fall back from this explicit J-Link mode to `open-nrf-ocd` or another
  transport.
- Report a focused setup hint when SEGGER's required J-Link host driver/library
  is missing or no J-Link is connected.
- Add an experimental **Upload Using Programmer** route for standalone J-Link
  models without a serial/VCOM interface.
- Reject ambiguous multi-J-Link selection unless `NRF54L15_JLINK_UID` identifies
  the intended SEGGER serial number.

This remains in the Arduino core's existing pyOCD upload layer. `open-nrf-ocd`
continues to implement the open CMSIS-DAP transport and does not embed SEGGER's
proprietary J-Link protocol or libraries.

## Validation status

- Host regressions cover transport restriction, serial normalization, power
  options, reset behavior, no-fallback behavior, and every board-menu entry.
- Arduino CLI recipe expansion confirms that selecting `clean_upload=jlink`
  reaches the pyOCD runner with `--probe-type jlink` and no unresolved
  properties.
- The regular CMSIS-DAP upload command remains on its existing explicit
  no-reset path.
- No J-Link hardware was available to the maintainer for this release. Both an
  onboard nRF54L15 DK J-Link and a standalone J-Link still require user testing.

## Install or upgrade

Refresh the package index and install `1.0.17` from Arduino Boards Manager.
Install SEGGER's official J-Link Software and Documentation Pack separately
before selecting the experimental upload method.
