import {
  battery,
  commandsOnOff,
  forcePowerSource,
  humidity,
  light,
  onOff,
  temperature,
} from "zigbee-herdsman-converters/lib/modernExtend";

const vendor = "CleanCore";

const mains = [forcePowerSource({powerSource: "Mains (single phase)"})];
const batteryPowered = [forcePowerSource({powerSource: "Battery"})];
const batteryExpose = battery({
  voltage: true,
  percentage: true,
  voltageReporting: true,
  percentageReporting: true,
});

const onOffLight = (model, description) => ({
  zigbeeModel: [model],
  model,
  vendor,
  description,
  extend: [
    onOff({powerOnBehavior: false, configureReporting: true}),
    ...mains,
  ],
});

const dimmableLight = (model, description) => ({
  zigbeeModel: [model],
  model,
  vendor,
  description,
  extend: [
    light({powerOnBehavior: false, configureReporting: true}),
    ...mains,
  ],
});

const colorLight = (model, description) => ({
  zigbeeModel: [model],
  model,
  vendor,
  description,
  extend: [
    light({
      color: {modes: ["hs"]},
      colorTemp: {range: [153, 500]},
      powerOnBehavior: false,
      configureReporting: true,
    }),
    ...mains,
  ],
});

const temperatureSensor = (model, description) => ({
  zigbeeModel: [model],
  model,
  vendor,
  description,
  extend: [
    temperature({reporting: true}),
    batteryExpose,
    ...batteryPowered,
  ],
});

const climateSensor = (model, description) => ({
  zigbeeModel: [model],
  model,
  vendor,
  description,
  extend: [
    temperature({reporting: true}),
    humidity({reporting: true}),
    batteryExpose,
    ...batteryPowered,
  ],
});

export default [
  onOffLight("X54-JOIN-LIGHT", "nRF54L15 Clean joinable on/off light example"),
  onOffLight("X54-PORCH-LIGHT", "nRF54L15 Clean porch on/off light example"),

  dimmableLight("X54-JOIN-DIM", "nRF54L15 Clean joinable dimmable light example"),
  dimmableLight("X54-DESK-LAMP", "nRF54L15 Clean dimmable desk lamp example"),
  colorLight("X54-RGB-MOOD", "nRF54L15 Clean RGB mood light example"),
  colorLight("X54-RGBW-CEIL", "nRF54L15 Clean RGBW ceiling light example"),

  temperatureSensor("X54-JOIN-TEMP", "nRF54L15 Clean joinable temperature sensor example"),
  temperatureSensor("X54-TEMP-BATT", "nRF54L15 Clean temperature battery sensor example"),
  temperatureSensor("X54-SLEEP-T15", "nRF54L15 Clean sleepy temperature sensor, 15 second report"),
  temperatureSensor("X54-SLEEP-T60", "nRF54L15 Clean sleepy temperature sensor, 60 second report"),

  climateSensor("X54-TEMP-HUM", "nRF54L15 Clean temperature/humidity sensor example"),
  climateSensor("X54-CLIMATE-BATT", "nRF54L15 Clean climate battery sensor example"),
  climateSensor("X54-SLEEP-CL15", "nRF54L15 Clean sleepy climate sensor, 15 second report"),
  climateSensor("X54-SLEEP-CL60", "nRF54L15 Clean sleepy climate sensor, 60 second report"),

  {
    zigbeeModel: ["X54-BUTTON-REMOTE"],
    model: "X54-BUTTON-REMOTE",
    vendor,
    description: "nRF54L15 Clean sleepy on/off button remote example",
    extend: [
      onOff({
        powerOnBehavior: false,
        configureReporting: true,
        description: "Latched button state",
      }),
      commandsOnOff({commands: ["on", "off", "toggle"], bind: true}),
      batteryExpose,
      ...batteryPowered,
    ],
  },
];
