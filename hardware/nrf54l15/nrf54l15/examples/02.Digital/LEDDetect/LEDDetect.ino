/*
 * LED Detection Test for XIAO nRF54L15
 *
 * Cycles through likely LED-connected pins so you can confirm wiring.
 */

static const uint8_t kTestPins[] = {
  LED_BUILTIN,
  PIN_D11,
  PIN_D12,
  PIN_D0,
};

static void allOff()
{
  for (size_t i = 0; i < (sizeof(kTestPins) / sizeof(kTestPins[0])); ++i) {
    digitalWrite(kTestPins[i], HIGH); // Keep active-low LED off by default.
  }
}

void setup()
{
  for (size_t i = 0; i < (sizeof(kTestPins) / sizeof(kTestPins[0])); ++i) {
    pinMode(kTestPins[i], OUTPUT);
  }
  allOff();
}

void loop()
{
  for (size_t i = 0; i < (sizeof(kTestPins) / sizeof(kTestPins[0])); ++i) {
    allOff();
    digitalWrite(kTestPins[i], LOW);
    delay(500);
    digitalWrite(kTestPins[i], HIGH);
    delay(200);
  }
}
