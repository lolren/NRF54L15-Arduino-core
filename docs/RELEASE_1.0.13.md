# nRF54 Arduino Core 1.0.13

`1.0.13` corrected the battery-powered nPM1300 timer sequence reported in
[issue #107](https://github.com/lolren/nrf54-arduino-core/issues/107), but its
VBUS guard left the issue's USB-powered case unresolved.

## Corrected in 1.0.14

Release `1.0.14` keeps true PMIC Hibernate for VBUS-absent battery power and
uses the nRF54 GRTC System OFF wake-reset path while VBUS is present. This is
required because Nordic documents VBUS-disconnected as a precondition for
`TASKENTERHIBERNATE`, while GRTC compare is a valid nRF54 System OFF wake
source. Both paths cold-reset after the requested delay.

## nPM1300 timed hibernate

- Program the wake timer as `TIMERHIBYTE`/`TIMERMIDBYTE`/`TIMERLOBYTE` plus
  `TIMERTARGETSTROBE`, matching the nPM13xx driver sequence.
- Wait 1 ms after the timer-load strobe before writing `TASKENTERHIBERNATE`.
- Return `false` while USB/VBUS is present instead of issuing a PMIC
  Ship/Hibernate command that the nPM1300 rejects.
- Document that PMIC Ship/Hibernate testing must be done from VBAT/battery
  power with USB/VBUS disconnected.

## Validation

- Core I/O regression suite passes.
- LM20A PDM/board contract suite passes.
- `nPM1300_TimedHibernate` compiles for XIAO nRF54LM20A.
- USB-connected XIAO nRF54LM20A hardware probe confirms `VBUSINSTATUS=0x21`
  and the timed-hibernate API returns `false` on the required VBUS-present guard.

## Install or upgrade

```bash
arduino-cli core update-index
arduino-cli core install "nrf54l15clean:nrf54l15clean@1.0.13"
```

[Full changes since v1.0.12](https://github.com/lolren/nrf54-arduino-core/compare/v1.0.12...v1.0.13)
