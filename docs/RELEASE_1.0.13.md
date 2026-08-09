# nRF54 Arduino Core 1.0.13

`1.0.13` fixes the nPM1300 timed-hibernate helper behavior reported in
[issue #107](https://github.com/lolren/nrf54-arduino-core/issues/107).

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
