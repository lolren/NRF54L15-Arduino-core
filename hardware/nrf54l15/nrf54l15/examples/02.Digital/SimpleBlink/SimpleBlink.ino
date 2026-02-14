/*
 * Simple Blink Test
 *
 * Uses the Arduino API only so it stays portable across core revisions.
 */

void setup()
{
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH); // XIAO LED is active-low.
}

void loop()
{
  digitalWrite(LED_BUILTIN, LOW);
  delay(500);
  digitalWrite(LED_BUILTIN, HIGH);
  delay(500);
}
