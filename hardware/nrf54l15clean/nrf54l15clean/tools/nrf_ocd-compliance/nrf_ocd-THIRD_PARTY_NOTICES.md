# nRF OCD Third-Party Notices

Project-owned nRF OCD source is distributed under the adjacent
[MIT license](nrf_ocd-MIT.txt), except where a component or adaptation uses the
terms below.

## pyOCD

The following nRF OCD `v0.3.3` source paths contain material adapted from or
behavior directly derived from pyOCD `0.44.1`:

- `src/flash_algo_nrf54l.c`
- `src/target_nrf54l.c`
- `src/target_nrf54lm20a.c`
- `src/hid_libusb.c`

Those portions are distributed under the
[Apache License 2.0](../../LICENSES/Apache-2.0.txt). pyOCD is maintained at
<https://github.com/pyocd/pyOCD>.

## libusb

The Linux x86-64 `nrf_ocd` executable statically links unmodified libusb
`1.0.27`, distributed under LGPL-2.1-or-later. The exact corresponding source,
complete license text, and an offline relink script accompany the executable;
see the [corresponding-source guide](README.md).

The nRF OCD application source and libusb source remain separate works in the
provided archives, and recipients may modify either before relinking.
