# Zigbee2MQTT Integration

For the remaining stack work, implementation order, and complete validation
matrix, see [Zigbee Full-Support Handoff](ZIGBEE_FULL_SUPPORT_HANDOFF.md).

The Zigbee HA examples use honest `CleanCore` manufacturer/model strings. A
stock Zigbee2MQTT install can still interview them and generate exposes, but it
will mark them as generated/unsupported until Zigbee2MQTT has a converter for
the model.

This repo ships a temporary external converter for the example model IDs:

```text
extras/zigbee2mqtt/cleancore_nrf54_examples.mjs
```

## Install Through The Zigbee2MQTT UI

1. Enable external converters in Zigbee2MQTT if your install requires it.
   Zigbee2MQTT 2.11+ disables external JavaScript by default.
2. Open Zigbee2MQTT.
3. Go to Settings > Dev console > External converters.
4. Create or replace `cleancore_nrf54_examples.mjs`.
5. Paste the contents of `extras/zigbee2mqtt/cleancore_nrf54_examples.mjs`.
6. Save, restart Zigbee2MQTT if requested, then re-interview the device.

## Install Over MQTT

If the broker accepts MQTT control messages, the converter can be saved without
opening the UI:

```bash
python3 - <<'PY'
import json
from pathlib import Path

name = "cleancore_nrf54_examples.mjs"
code = Path("extras/zigbee2mqtt/cleancore_nrf54_examples.mjs").read_text()
payload = json.dumps({"name": name, "code": code})
Path("/tmp/cleancore_z2m_converter_payload.json").write_text(payload)
PY

mosquitto_pub -h 192.168.1.100 -u lolren -P lolren \
  -t zigbee2mqtt/bridge/request/converter/save \
  -f /tmp/cleancore_z2m_converter_payload.json

mosquitto_sub -h 192.168.1.100 -u lolren -P lolren \
  -t 'zigbee2mqtt/bridge/response/converter/save' -C 1 -W 10
```

Then re-interview the device:

```bash
mosquitto_pub -h 192.168.1.100 -u lolren -P lolren \
  -t zigbee2mqtt/bridge/request/device/interview \
  -m '{"id":"0xd0acf9feff59226e"}'
```

Replace the IEEE address with the one printed by the sketch or shown in
Zigbee2MQTT.

## Current Test Result

Tested against the Home Assistant/Zigbee2MQTT instance at
`192.168.1.100` using XIAO nRF54L15:

- Before loading a converter, `ZigbeeSleepyOnOffButton` joined and completed
  interview, but Zigbee2MQTT used a generated definition and reported
  `supported:false`, which is expected for a new custom model.
- The converter was accepted through
  `zigbee2mqtt/bridge/request/converter/save` with `status:ok`.
- After flashing `ZigbeeHaOnOffLightJoinable`, Zigbee2MQTT reported
  `model_id=X54-JOIN-LIGHT`, `definition.source=external`,
  `supported=true`, and `interview_completed=true`.
- MQTT state was published at `zigbee2mqtt/0xd0acf9feff59226e` with
  `state` and `linkquality` values.

## Covered Example Model IDs

- `X54-JOIN-LIGHT`
- `X54-PORCH-LIGHT`
- `X54-JOIN-DIM`
- `X54-DESK-LAMP`
- `X54-RGB-MOOD`
- `X54-RGBW-CEIL`
- `X54-JOIN-TEMP`
- `X54-TEMP-BATT`
- `X54-SLEEP-T15`
- `X54-SLEEP-T60`
- `X54-TEMP-HUM`
- `X54-CLIMATE-BATT`
- `X54-SLEEP-CL15`
- `X54-SLEEP-CL60`
- `X54-BUTTON-REMOTE`

## Notes

- The converter is intentionally outside the Arduino build path. It is for
  Zigbee2MQTT/Home Assistant integration only.
- The generated device support in Zigbee2MQTT is useful for smoke testing, but
  the external converter is the cleaner path for normal HA entities.
- The converter should eventually be upstreamed to Zigbee2MQTT if these example
  model IDs become stable API.
