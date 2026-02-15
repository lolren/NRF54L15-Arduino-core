/*
  XIAO nRF54L15 board test: blink built-in LED at 500 ms.

  Notes:
  - LED_BUILTIN on this board is active-low.
*/

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);
}

void loop() {
  digitalWrite(LED_BUILTIN, LOW);
  delay(500);
  digitalWrite(LED_BUILTIN, HIGH);
  delay(500);
}

