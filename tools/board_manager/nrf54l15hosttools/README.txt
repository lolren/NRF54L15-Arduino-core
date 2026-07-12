Nrf54L15 host tools

This package is downloaded automatically through Arduino Boards Manager.

What it provides:
- pinned pyOCD bootstrap requirements for the advanced recovery uploader
- a tool-local pyOCD runtime install path that does not touch the system Python
  environment
- Linux and Windows helper scripts for the host-side setup path
- CMSIS-DAP udev rules for XIAO nRF54L15 and XIAO nRF54LM20A on Linux

Linux helper usage:
- `setup/install_linux_host_deps.sh --udev` installs only `/dev/hidraw*` and `/dev/ttyACM*` access rules
- `setup/install_linux_host_deps.sh --python` installs only the pyOCD Python side into the local tool runtime
- `setup/install_linux_host_deps.sh --all` installs both

Normal compile and default upload should work without this package being used
directly. It exists so recovery and protected-target workflows do not depend on
manually locating setup files in the repository.

Recovery dependency installation:
- the first pyOCD recovery setup requires access to the configured Python
  package index
- dependencies are installed into the tool-local runtime and do not modify the
  system Python environment
- binary dependency wheels are intentionally not redistributed in this archive;
  this keeps their native third-party license delivery with the package index

Licenses:
- `LICENSE` covers the project-owned bootstrap and setup files
- `THIRD_PARTY_NOTICES.md` documents the dependency-installation boundary
