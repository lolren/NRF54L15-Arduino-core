# nRF54 Arduino Core 1.0.12

`1.0.12` continues the Arduino IDE 1.8.19 upload compatibility work for
[issue #102](https://github.com/lolren/nrf54-arduino-core/issues/102).

## Arduino IDE 1.8 uploads

- Move UF2 upload defaults from platform-only properties into every board stanza.
- Ensure `upload.uf2_timeout` expands to `12` instead of reaching `upload.py` as
  the literal `{upload.uf2_timeout}` token.
- Board-scope the related UF2 drive labels and pyOCD safe-mode defaults used by
  the same upload recipes.
- Add regression coverage that every `{upload.*}` recipe token has a board-level
  value for all shipped boards.

## Validation

- Upload-helper contract suite passes, including Arduino CLI recipe expansion.
- The full release contract suite and exact-archive feature compile matrix are
  required to pass before publication.

## Install or upgrade

```bash
arduino-cli core update-index
arduino-cli core install "nrf54l15clean:nrf54l15clean@1.0.12"
```

[Full changes since v1.0.11](https://github.com/lolren/nrf54-arduino-core/compare/v1.0.11...v1.0.12)
