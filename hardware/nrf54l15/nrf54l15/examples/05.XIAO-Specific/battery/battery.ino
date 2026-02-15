/*
  XIAO nRF54L15 battery monitor sample.

  Default input pin is A0. If your board routes battery via divider to another pin,
  override BATTERY_ADC_PIN and BATTERY_DIVIDER_RATIO before including this sketch.
*/

#ifndef BATTERY_ADC_PIN
#define BATTERY_ADC_PIN A0
#endif

#ifndef BATTERY_DIVIDER_RATIO
#define BATTERY_DIVIDER_RATIO 2.0f
#endif

#ifndef ADC_REF_VOLTAGE
#define ADC_REF_VOLTAGE 3.3f
#endif

void setup() {
  Serial.begin(115200);
  delay(1000);
  analogReadResolution(12);
  Serial.println("XIAO nRF54L15 battery monitor sample");
}

void loop() {
  const int raw = analogRead(BATTERY_ADC_PIN);
  const float pinVoltage = (static_cast<float>(raw) / 4095.0f) * ADC_REF_VOLTAGE;
  const float batteryVoltage = pinVoltage * BATTERY_DIVIDER_RATIO;

  Serial.print("ADC raw=");
  Serial.print(raw);
  Serial.print("  pin=");
  Serial.print(pinVoltage, 3);
  Serial.print(" V  battery~=");
  Serial.print(batteryVoltage, 3);
  Serial.println(" V");
  delay(1000);
}
