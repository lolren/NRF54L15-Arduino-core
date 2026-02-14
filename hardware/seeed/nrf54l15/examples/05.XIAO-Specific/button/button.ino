/*
  XIAO nRF54L15 button sample.
  Uses PIN_BUTTON and prints state changes.
*/

int g_lastState = HIGH;

void setup() {
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);

  Serial.begin(115200);
  delay(1000);
  Serial.println("XIAO nRF54L15 button sample");
}

void loop() {
  const int state = digitalRead(PIN_BUTTON);
  if (state != g_lastState) {
    g_lastState = state;
    Serial.print("button: ");
    Serial.println(state == LOW ? "pressed" : "released");
  }

  digitalWrite(LED_BUILTIN, state == LOW ? LOW : HIGH);
  delay(10);
}
