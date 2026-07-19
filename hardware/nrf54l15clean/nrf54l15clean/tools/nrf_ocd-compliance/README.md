# nRF OCD Corresponding Source

The platform's Linux `tools/nrf_ocd` executable is nRF OCD `v0.3.7`. It was
built from nRF OCD source and statically linked with libusb `1.0.27`. Most nRF
OCD code is project-owned MIT work; the target, flash-algorithm, and USB
transport portions identified in
[`nrf_ocd-THIRD_PARTY_NOTICES.md`](nrf_ocd-THIRD_PARTY_NOTICES.md) are adapted
from pyOCD under Apache-2.0. This directory accompanies the executable so
recipients can inspect, modify, and relink the complete work.

| File | Role | SHA-256 |
|---|---|---|
| `../nrf_ocd` | Distributed Linux x86-64 executable | `9013a6ad6a1bae2788750470502c863b3dfd4ff321f442e2f36c846deacfb31c` |
| `open-nrf-ocd-v0.3.7-source.tar.gz` | Exact application source from the annotated `v0.3.7` release tag | `b1130ef1b05df1b2cd656189d2f5c7333e86b86055c03ed86ab380b00e405b95` |
| `libusb-1.0.27.tar.bz2` | Corresponding libusb source | `ffaa41d741a8a3bee244ac8e54a72ea05bf2879663c098c82fc5757853441575` |

The application archive records commit
`90c1845462050a6ab68ed73eba4ea0a8a8330872` and contains the exact source used
for the published `v0.3.7` binaries, including command-free post-reset
teardown, bounded SWD transfer recovery, and their regression tests.

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
