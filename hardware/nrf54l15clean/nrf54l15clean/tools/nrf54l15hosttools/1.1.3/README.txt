Nrf54L15 host tools 1.1.3

What's new in 1.1.3:
- pyOCD 0.44.1 with built-in nrf54lm20a target support
- Setup installs pinned pyOCD into the tool-local runtime; the first recovery
  setup requires access to the configured Python package index

Linux Mint / Ubuntu extra step:
- If upload fails with permission errors, run:
  sudo cp tools/nrf54l15hosttools/1.1.3/setup/60-seeed-xiao-nrf54-cmsis-dap.rules /etc/udev/rules.d/
  sudo udevadm control --reload-rules
  sudo udevadm trigger
  Then unplug and replug the board.

This package is downloaded automatically through Arduino Boards Manager.
