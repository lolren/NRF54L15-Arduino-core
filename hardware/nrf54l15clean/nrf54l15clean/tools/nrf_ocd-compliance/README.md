# nRF OCD Corresponding Source

The platform's Linux `tools/nrf_ocd` executable is nRF OCD `v0.3.3`. It was
built from nRF OCD source and statically linked with libusb `1.0.27`. Most nRF
OCD code is project-owned MIT work; the target, flash-algorithm, and USB
transport portions identified in
[`nrf_ocd-THIRD_PARTY_NOTICES.md`](nrf_ocd-THIRD_PARTY_NOTICES.md) are adapted
from pyOCD under Apache-2.0. This directory accompanies the executable so
recipients can inspect, modify, and relink the complete work.

| File | Role | SHA-256 |
|---|---|---|
| `../nrf_ocd` | Distributed Linux x86-64 executable | `b5ed26567cf4fe5b9f3d9ea24c3f483029121539c521168e271a0f76a86dbe48` |
| `open-nrf-ocd-v0.3.3-compliance-source.tar.gz` | `v0.3.3` application source with redistribution-only license and attribution metadata | `8de82fa7270fda04c2c1bfdcbc70672891af0a5ebb98d1bf3b97124c58542959` |
| `libusb-1.0.27.tar.bz2` | Corresponding libusb source | `ffaa41d741a8a3bee244ac8e54a72ea05bf2879663c098c82fc5757853441575` |

The application archive records commit `9e2ce3a1ba480c43cbb5f6b43a3d0acc47d83c92`
as its executable-code baseline. Its added headers, license files, and removal
of stale build output do not alter executable C statements.

Project-owned nRF OCD portions are provided under the adjacent
[MIT license](nrf_ocd-MIT.txt). The identified pyOCD-derived portions use the
platform's complete [Apache-2.0 text](../../LICENSES/Apache-2.0.txt). libusb is
provided under LGPL-2.1-or-later; its source archive includes the upstream
`COPYING` and `AUTHORS` files, and the platform also includes the complete
[LGPL-2.1 text](../../LICENSES/LGPL-2.1-or-later.txt).

Run the following on a Linux build host with a C compiler, Autotools-compatible
shell tools, `make`, `tar`, and bzip2:

```bash
bash rebuild-linux-x86_64.sh
```

The rebuilt executable is written under the printed temporary work directory.
Set `NRF_OCD_CC` and `NRF_OCD_AR` to select another compatible compiler and
archiver. The script uses only the corresponding source archives shipped here;
it does not download build inputs.
