# XIAO nRF54LM20A Support TODO

This tracks board-specific follow-up so LM20A work does not get mixed into unrelated BLE, Thread, or Matter changes.

## Current Release

- [x] Rename the public board entry to XIAO nRF54LM20A while keeping the early `xiao_nrf54lm20b` identifier for compatibility.
- [x] Route USB `Serial` to the SAMD11 bridge UART on P1.11/P1.10.
- [x] Keep header `Serial1` on D6/D7 (P1.08/P1.09).
- [x] Add P3 GPIO support to digital, analog, Wire, and HardwareSerial helpers.
- [x] Remove install-time pyOCD site-packages mutation. LM20A upload now uses a runtime pyOCD target hook instead, which is safer on Windows and managed Python installs.
- [x] Verify compile, upload, and USB serial output on an attached XIAO nRF54LM20A.
- [x] Add and hardware-validate the nPM1300 PMIC API for the internal P1.17/P1.18 I2C bus.
- [x] Add LM20A PMIC examples for system monitoring and IMU/MIC rail control.
- [x] Add and hardware-test LM20A IMU and PDM microphone examples.

## Follow-Up

- [ ] Decide whether the old `xiao_nrf54lm20b` folder and FQBN should remain forever as a compatibility alias or be migrated with a second visible board entry.
