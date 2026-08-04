# nRF54 Arduino Core 1.0.11

`1.0.11` completes the Arduino IDE 1.8.x upload compatibility fix for
[issue #102](https://github.com/lolren/nrf54-arduino-core/issues/102).

## Arduino IDE 1.8 uploads

- Use `{runtime.platform.path}` directly for the upload helper, UF2 emitters,
  and programmer recipes.
- Avoid nested custom `{tools.*}` properties that Arduino IDE 1.8.19 leaves
  unresolved.
- Do not pass the unused OpenOCD path token to the checked uploader wrapper.
- Add regression coverage for the expanded legacy-IDE upload command.

## Validation

- Upload-helper contract suite passes, including Arduino CLI recipe expansion.
- The full release contract suite and exact-archive feature compile matrix are
  required to pass before publication.

## Install or upgrade

```bash
arduino-cli core update-index
arduino-cli core install "nrf54l15clean:nrf54l15clean@1.0.11"
```

[Full changes since v1.0.10](https://github.com/lolren/nrf54-arduino-core/compare/v1.0.10...v1.0.11)
