# Third-Party Notices

This project includes third-party components under their respective licenses.

## Bundled in repository

The following Python dependencies are vendored under `hardware/nrf54l15/nrf54l15/tools/pydeps`:

- `west`
- `python-dateutil`
- `pykwalify`
- `ruamel.yaml`
- `docopt`

Use and redistribution of these components remains subject to their upstream licenses.

## Downloaded at build time (not bundled in release archive)

Bootstrap scripts may download external toolchains/workspaces during build setup, including:

- Nordic nRF Connect SDK (NCS)
- Zephyr SDK

These are not shipped inside this repository's Boards Manager release archive and remain under their upstream licenses and terms.

## Recommendation

When publishing releases, keep upstream `LICENSE`/notice files intact and link to upstream repositories in release notes.
